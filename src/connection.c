/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#ifdef HAVE_SIGNALFD
#include <sys/signalfd.h>
#include <signal.h>
#endif

#include "compat.h"

#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "httpp/httpp.h"

#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "xslt.h"
#include "fserve.h"
#include "sighandler.h"
#include "slave.h"

#include "yp.h"
#include "source.h"
#include "format.h"
#include "format_mp3.h"
#include "event.h"
#include "admin.h"
#include "auth.h"

#define CATMODULE "connection"

/* Two different major types of source authentication.
   Shoutcast style is used only by the Shoutcast DSP
   and is a crazy version of HTTP.  It looks like :
     Source Client -> Connects to port + 1
     Source Client -> sends encoder password (plaintext)\r\n
     Icecast -> reads encoder password, if ok, sends OK2\r\n, else disconnects
     Source Client -> reads OK2\r\n, then sends http-type request headers
                      that contain the stream details (icy-name, etc..)
     Icecast -> reads headers, stores them
     Source Client -> starts sending MP3 data
     Source Client -> periodically updates metadata via admin.cgi call

   Icecast auth style uses HTTP and Basic Authorization.
*/

static int  shoutcast_source_client (client_t *client);
static int  http_client_request (client_t *client);
static int  _handle_get_request (client_t *client);
static int  _handle_source_request (client_t *client);
static int  _handle_stats_request (client_t *client);

struct banned_entry
{
    char ip[16]; // may want to expand later for ipv6
    time_t timeout;
};

typedef struct
{
    time_t file_recheck;
    time_t file_mtime;
    avl_tree *contents;
    int  (*compare)(void *arg, void *a, void *b);
    void (*add_new_entry)(avl_tree *t, const char *ip, time_t now);
    char *filename;
} cache_file_contents;

static spin_t _connection_lock;
static volatile unsigned long _current_id = 0;
thread_type *conn_tid;
int sigfd;

static int ssl_ok;
#ifdef HAVE_OPENSSL
static SSL_CTX *ssl_ctx;
#endif

int header_timeout;

struct _client_functions shoutcast_source_ops =
{
    shoutcast_source_client,
    client_destroy
};

struct _client_functions http_request_ops =
{
    http_client_request,
    client_destroy
};

struct _client_functions http_req_get_ops =
{
    _handle_get_request,
    client_destroy
};
struct _client_functions http_req_source_ops =
{
    _handle_source_request,
    client_destroy
};

struct _client_functions http_req_stats_ops =
{
    _handle_stats_request,
    client_destroy
};

/* filtering client connection based on IP */
cache_file_contents banned_ip, allowed_ip;
struct banned_entry *ban_entry_removal;

/* filtering listener connection based on useragent */
cache_file_contents useragents;

int connection_running = 0;


static int compare_pattern (void *arg, void *a, void *b)
{
    const char *value = (const char *)a;
    const char *pattern = (const char *)b;
#ifdef HAVE_FNMATCH_H
    int x;

    switch ((x = fnmatch (pattern, value, FNM_NOESCAPE)))
    {
        case FNM_NOMATCH:
            x = strcmp (pattern, value);
        case 0:
            break;
        default:
            INFO0 ("fnmatch failed");
            return -1;
    }
    return x;
#else
    return strcmp (pattern, value);
#endif
}

static int compare_banned_ip (void *arg, void *a, void *b)
{
    struct banned_entry *this = (struct banned_entry *)a;
    struct banned_entry *that = (struct banned_entry *)b;
    if (that->timeout)
        if (ban_entry_removal == NULL || that->timeout < ban_entry_removal->timeout)
            ban_entry_removal = that; // identify possible removal
    return compare_pattern (NULL, &this->ip[0] , &that->ip[0]);
}

static int free_filtered_line (void*x)
{
    free (x);
    return 1;
}


void connection_initialize(void)
{
    thread_spin_create (&_connection_lock);

    banned_ip.contents = NULL;
    allowed_ip.contents = NULL;
    useragents.contents = NULL;

    conn_tid = NULL;
    connection_running = 0;
#ifdef HAVE_OPENSSL
    SSL_load_error_strings();                /* readable error messages */
    SSL_library_init();                      /* initialize library */
#endif
}

void connection_shutdown(void)
{
    thread_spin_destroy (&_connection_lock);
}

static unsigned long _next_connection_id(void)
{
    unsigned long id;

    thread_spin_lock (&_connection_lock);
    id = _current_id++;
    thread_spin_unlock (&_connection_lock);

    return id;
}


#ifdef HAVE_OPENSSL
static void get_ssl_certificate (ice_config_t *config)
{
    ssl_ok = 0;

    ssl_ctx = SSL_CTX_new (SSLv23_server_method());

    do
    {
        if (config->cert_file == NULL)
            break;
        if (SSL_CTX_use_certificate_file (ssl_ctx, config->cert_file, SSL_FILETYPE_PEM) <= 0)
        {
            WARN1 ("Invalid cert file %s", config->cert_file);
            break;
        }
        if (SSL_CTX_use_PrivateKey_file (ssl_ctx, config->cert_file, SSL_FILETYPE_PEM) <= 0)
        {
            WARN1 ("Invalid private key file %s", config->cert_file);
            break;
        }
        if (!SSL_CTX_check_private_key (ssl_ctx))
        {
            ERROR1 ("Invalid %s - Private key does not match cert public key", config->cert_file);
            break;
        }
        ssl_ok = 1;
        INFO1 ("SSL certificate found at %s", config->cert_file);
        return;
    } while (0);
    INFO0 ("No SSL capability on any configured ports");
}


/* handlers for reading and writing a connection_t when there is ssl
 * configured on the listening port
 */
int connection_read_ssl (connection_t *con, void *buf, size_t len)
{
    int bytes = SSL_read (con->ssl, buf, len);

    if (bytes < 0)
    {
        switch (SSL_get_error (con->ssl, bytes))
        {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return -1;
        }
        con->error = 1;
    }
    return bytes;
}

int connection_send_ssl (connection_t *con, const void *buf, size_t len)
{
    int bytes = SSL_write (con->ssl, buf, len);

    if (bytes < 0)
    {
        switch (SSL_get_error (con->ssl, bytes))
        {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return -1;
        }
        con->error = 1;
    }
    else
        con->sent_bytes += bytes;
    return bytes;
}
#else

/* SSL not compiled in, so at least log it */
static void get_ssl_certificate (ice_config_t *config)
{
    ssl_ok = 0;
    INFO0 ("No SSL capability");
}
#endif /* HAVE_OPENSSL */


/* handlers (default) for reading and writing a connection_t, no encrpytion
 * used just straight access to the socket
 */
int connection_read (connection_t *con, void *buf, size_t len)
{
    int bytes = sock_read_bytes (con->sock, buf, len);
    if (bytes == 0)
        con->error = 1;
    if (bytes == -1 && !sock_recoverable (sock_error()))
        con->error = 1;
    return bytes;
}

int connection_send (connection_t *con, const void *buf, size_t len)
{
    int bytes = sock_write_bytes (con->sock, buf, len);
    if (bytes < 0)
    {
        if (!sock_recoverable (sock_error()))
            con->error = 1;
    }
    else
        con->sent_bytes += bytes;
    return bytes;
}

static void add_generic_text (avl_tree *t, const char *ip, time_t now)
{
    char *str = strdup (ip);
    if (str)
        avl_insert (t, str);
}

static void add_banned_ip (avl_tree *t, const char *ip, time_t now)
{
    struct banned_entry *entry = calloc (1, sizeof (struct banned_entry));
    snprintf (&entry->ip[0], sizeof (entry->ip), "%s", ip);
    if (now)
        entry->timeout = now;
    avl_insert (t, entry);
}

void connection_add_banned_ip (const char *ip, int duration)
{
    time_t timeout = 0;
    if (duration > 0)
        timeout = time(NULL) + duration;

    if (banned_ip.contents)
    {
        global_lock();
        add_banned_ip (banned_ip.contents, ip, timeout);
        global_unlock();
    }
}

void connection_release_banned_ip (const char *ip)
{
    if (banned_ip.contents)
    {
        global_lock();
        avl_delete (banned_ip.contents, (void*)ip, free_filtered_line);
        global_unlock();
    }
}

void connection_stats (void)
{
    if (banned_ip.contents)
        stats_event_args (NULL, "banned_IPs", "%ld", (long)banned_ip.contents->length);
}

/* function to handle the re-populating of the avl tree containing IP addresses
 * for deciding whether a connection of an incoming request is to be dropped.
 */
static void recheck_cached_file (cache_file_contents *cache, time_t now)
{
    if (now >= cache->file_recheck)
    {
        struct stat file_stat;
        FILE *file = NULL;
        int count = 0;
        avl_tree *new_ips;
        char line [MAX_LINE_LEN];

        cache->file_recheck = now + 10;
        if (cache->filename == NULL)
        {
            if (cache->contents)
            {
                avl_tree_free (cache->contents, free_filtered_line);
                cache->contents = NULL;
            }
            return;
        }
        if (stat (cache->filename, &file_stat) < 0)
        {
            WARN2 ("failed to check status of \"%s\": %s", cache->filename, strerror(errno));
            return;
        }
        if (file_stat.st_mtime == cache->file_mtime)
            return; /* common case, no update to file */

        cache->file_mtime = file_stat.st_mtime;

        file = fopen (cache->filename, "r");
        if (file == NULL)
        {
            WARN2("Failed to open file \"%s\": %s", cache->filename, strerror (errno));
            return;
        }

        new_ips = avl_tree_new (cache->compare, NULL);

        while (get_line (file, line, MAX_LINE_LEN))
        {
            if(!line[0] || line[0] == '#')
                continue;
            count++;
            cache->add_new_entry (new_ips, line, 0);
        }
        fclose (file);
        INFO2 ("%d entries read from file \"%s\"", count, cache->filename);

        if (cache->contents) avl_tree_free (cache->contents, free_filtered_line);
        cache->contents = new_ips;
    }
}


/* return 0 if the passed ip address is not to be handled by icecast, non-zero otherwise */
static int accept_ip_address (char *ip)
{
    void *result;
    time_t now = time(NULL);

    recheck_cached_file (&banned_ip, now);

    if (banned_ip.contents)
    {
        ban_entry_removal = NULL;
        if (avl_get_by_key (banned_ip.contents, ip, &result) == 0)
        {
            struct banned_entry *entry = result;
            if (entry->timeout)
            {
                if (entry->timeout > now)
                {
                    /* we may need to extend the timeout, for repeat offenders */
                    if (now+900 > entry->timeout)
                        entry->timeout = now + 900;
                    return 0;
                }
            }
            else
            {
                DEBUG1 ("%s is banned", ip);
                return 0;
            }
        }
        if (ban_entry_removal)
        {
            /* we have identified the entry with the earliest timeout, but has it expired */
            if (ban_entry_removal->timeout <= now)
            {
                INFO1 ("removing %s from ban list for now", &ban_entry_removal->ip[0]);
                avl_delete (banned_ip.contents, &ban_entry_removal->ip[0], free_filtered_line);
            }
            ban_entry_removal = NULL;
        }
    }
    recheck_cached_file (&allowed_ip, now);
    if (allowed_ip.contents)
    {
        if (avl_get_by_key (allowed_ip.contents, ip, &result) == 0)
        {
            DEBUG1 ("%s is allowed", ip);
            return 1;
        }
        else
        {
            DEBUG1 ("%s is not allowed", ip);
            return 0;
        }
    }
    return 1;
}


int connection_init (connection_t *con, sock_t sock, const char *addr)
{
    if (con)
    {
        struct sockaddr_storage sa;
        socklen_t slen = sizeof (sa);

        con->con_time = time(NULL);
        con->sock = sock;
        if (sock == SOCK_ERROR)
            return -1;
        con->id = _next_connection_id();
        con->discon_time = con->con_time + header_timeout;
        if (addr)
        {
            con->ip = strdup (addr);
            return 0;
        }
        if (getpeername (sock, (struct sockaddr *)&sa, &slen) == 0)
        {
            char *ip;
#ifdef HAVE_GETNAMEINFO
            char buffer [200] = "unknown";
            getnameinfo ((struct sockaddr *)&sa, slen, buffer, 200, NULL, 0, NI_NUMERICHOST);
            if (strncmp (buffer, "::ffff:", 7) == 0)
                ip = strdup (buffer+7);
            else
                ip = strdup (buffer);
#else
            int len = 30;
            ip = malloc (len);
            strncpy (ip, inet_ntoa (sa.sin_addr), len);
#endif
            if (accept_ip_address (ip))
            {
                con->ip = ip;
                return 0;
            }
            free (ip);
        }
        memset (con, 0, sizeof (connection_t));
    }
    return -1;
}


/* prepare connection for interacting over a SSL connection
 */
void connection_uses_ssl (connection_t *con)
{
#ifdef HAVE_OPENSSL
    con->ssl = SSL_new (ssl_ctx);
    SSL_set_accept_state (con->ssl);
    SSL_set_fd (con->ssl, con->sock);
#endif
}

#ifdef HAVE_SIGNALFD
void connection_close_sigfd (void)
{
    close (sigfd);
}
#endif

static sock_t wait_for_serversock (void)
{
#ifdef HAVE_POLL
    struct pollfd ufds [global.server_sockets + 1];
    int i, ret;

    for(i=0; i < global.server_sockets; i++) {
        ufds[i].fd = global.serversock[i];
        ufds[i].events = POLLIN;
        ufds[i].revents = 0;
    }
#ifdef HAVE_SIGNALFD
    ufds[i].fd = sigfd;
    ufds[i].events = POLLIN;
    ufds[i].revents = 0;
    ret = poll(ufds, i+1, -1);
#else
    ret = poll(ufds, global.server_sockets, 333);
#endif

    if (ret <= 0)
        return SOCK_ERROR;
    else {
        int dst;
#ifdef HAVE_SIGNALFD
        if (ufds[i].revents & POLLIN)
        {
            struct signalfd_siginfo fdsi;
            int ret  = read (sigfd, &fdsi, sizeof(struct signalfd_siginfo));
            if (ret == sizeof(struct signalfd_siginfo))
            {
                switch (fdsi.ssi_signo)
                {
                    case SIGINT:
                    case SIGTERM:
                        global.running = ICE_HALTING;
                        connection_running = 0;
                        DEBUG0 ("signalfd received a termination");
                        break;
                    case SIGHUP:
                        global.schedule_config_reread = 1;
                        connection_running = 0;
                        INFO0 ("HUP received, reread scheduled");
                        break;
                    default:
                        WARN1 ("unexpected signal (%d)", fdsi.ssi_signo);
                }
            }
        }
#endif
        for(i=0; i < global.server_sockets; i++) {
            if(ufds[i].revents & POLLIN)
                return ufds[i].fd;
            if(ufds[i].revents & (POLLHUP|POLLERR|POLLNVAL))
            {
                if (ufds[i].revents & (POLLHUP|POLLERR))
                {
                    sock_close (global.serversock[i]);
                    WARN0("Had to close a listening socket");
                }
                global.serversock[i] = SOCK_ERROR;
            }
        }
        /* remove any closed sockets */
        for(i=0, dst=0; i < global.server_sockets; i++)
        {
            if (global.serversock[i] == SOCK_ERROR)
                continue;
            if (i!=dst)
                global.serversock[dst] = global.serversock[i];
            dst++;
        }
        global.server_sockets = dst;
        return SOCK_ERROR;
    }
#else
    fd_set rfds;
    struct timeval tv, *p=NULL;
    int i, ret;
    sock_t max = SOCK_ERROR;

    FD_ZERO(&rfds);

    for(i=0; i < global.server_sockets; i++) {
        FD_SET(global.serversock[i], &rfds);
        if (max == SOCK_ERROR || global.serversock[i] > max)
            max = global.serversock[i];
    }

    tv.tv_sec = 0;
    tv.tv_usec = 333000;

    ret = select(max+1, &rfds, NULL, NULL, &tv);
    if(ret < 0) {
        return SOCK_ERROR;
    }
    else if(ret == 0) {
        return SOCK_ERROR;
    }
    else {
        for(i=0; i < global.server_sockets; i++) {
            if(FD_ISSET(global.serversock[i], &rfds))
                return global.serversock[i];
        }
        return SOCK_ERROR; /* Should be impossible, stop compiler warnings */
    }
#endif
}


static client_t *accept_client (void)
{
    client_t *client;
    sock_t sock, serversock = wait_for_serversock ();

    if (serversock == SOCK_ERROR)
        return NULL;

    sock = sock_accept (serversock, NULL, 0);
    if (sock == SOCK_ERROR)
    {
        if (sock_recoverable (sock_error()))
            return NULL;
        WARN2 ("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
        thread_sleep (500000);
        return NULL;
    }
    global_lock ();
    client = client_create (sock);
    if (client)
    {
        connection_t *con = &client->connection;
        int i;
        for (i=0; i < global.server_sockets; i++)
        {
            if (global.serversock[i] == serversock)
            {
                client->server_conn = global.server_conn[i];
                client->server_conn->refcount++;
                if (client->server_conn->ssl && ssl_ok)
                    connection_uses_ssl (con);
                if (client->server_conn->shoutcast_compat)
                    client->ops = &shoutcast_source_ops;
                else
                    client->ops = &http_request_ops;
                break;
            }
        }
        global_unlock ();
        if (sock_set_blocking (con->sock, 0) || sock_set_nodelay (con->sock))
        {
            WARN0 ("failed to set tcp options on client connection, dropping");
            client_destroy (client);
            client = NULL;
        }
        return client;
    }
    global_unlock ();
    sock_close (sock);
    thread_sleep (1000);
    return NULL;
}


/* shoutcast source clients are handled specially because the protocol is limited. It is
 * essentially a password followed by a series of headers, each on a separate line.  In here
 * we get the password and build a http request like a native source client would do
 */
static int shoutcast_source_client (client_t *client)
{
    do
    {
        connection_t *con = &client->connection;
        if (con->error || con->discon_time <= client->worker->current_time.tv_sec)
            break;

        if (client->shared_data)  /* need to get password first */
        {
            refbuf_t *refbuf = client->shared_data;
            int remaining = PER_CLIENT_REFBUF_SIZE - 2 - refbuf->len, ret, len;
            char *buf = refbuf->data + refbuf->len;
            char *esc_header;
            refbuf_t *r, *resp;
            char header [128];

            if (remaining == 0)
                break;

            ret = client_read_bytes (client, buf, remaining);
            if (ret == 0 || con->error)
                break;
            if (ret < 0)
                return 0;

            buf [ret] = '\0';
            len = strcspn (refbuf->data, "\r\n");
            if (refbuf->data [len] == '\0')  /* no EOL yet */
                return 0;

            refbuf->data [len] = '\0';
            snprintf (header, sizeof(header), "source:%s", refbuf->data);
            esc_header = util_base64_encode (header);

            len += 1 + strspn (refbuf->data+len+1, "\r\n");
            r = refbuf_new (PER_CLIENT_REFBUF_SIZE);
            snprintf (r->data, PER_CLIENT_REFBUF_SIZE,
                    "SOURCE %s HTTP/1.0\r\n" "Authorization: Basic %s\r\n%s",
                    client->server_conn->shoutcast_mount, esc_header, refbuf->data+len);
            r->len = strlen (r->data);
            free (esc_header);
            client->respcode = 200;
            resp = refbuf_new (30);
            snprintf (resp->data, 30, "OK2\r\nicy-caps:11\r\n\r\n");
            resp->len = strlen (resp->data);
            resp->associated = r;
            client->refbuf = resp;
            refbuf_release (refbuf);
            client->shared_data = NULL;
            INFO1 ("emulation on %s", client->server_conn->shoutcast_mount);
        }
        format_generic_write_to_client (client);
        if (client->pos == client->refbuf->len)
        {
            refbuf_t *r = client->refbuf;
            client->shared_data = r->associated;
            client->refbuf = NULL;
            r->associated = NULL;
            refbuf_release (r);
            client->ops = &http_request_ops;
            client->pos = 0;
        }
        client->schedule_ms = client->worker->time_ms + 100;
        return 0;
    } while (0);

    refbuf_release (client->shared_data);
    client->shared_data = NULL;
    return -1;
}


static int http_client_request (client_t *client)
{
    refbuf_t *refbuf = client->shared_data;
    int remaining = PER_CLIENT_REFBUF_SIZE - 1 - refbuf->len, ret = -1;

    if (remaining && client->connection.discon_time > client->worker->current_time.tv_sec)
    {
        char *buf = refbuf->data + refbuf->len;

        ret = client_read_bytes (client, buf, remaining);
        if (ret > 0)
        {
            char *ptr;

            buf [ret] = '\0';
            refbuf->len += ret;
            if (memcmp (refbuf->data, "<policy-file-request/>", 23) == 0)
            {
                fbinfo fb;
                fb.mount = "/flashpolicy";
                fb.flags = FS_USE_ADMIN;
                fb.fallback = NULL;
                fb.limit = 0;
                client->respcode = 200;
                refbuf_release (refbuf);
                client->shared_data = NULL;
                client->check_buffer = format_generic_write_to_client;
                fserve_setup_client_fb (client, &fb);
                return 0;
            }
            /* find a blank line */
            do
            {
                buf = refbuf->data;
                ptr = strstr (buf, "\r\n\r\n");
                if (ptr)
                {
                    ptr += 4;
                    break;
                }
                ptr = strstr (buf, "\n\n");
                if (ptr)
                {
                    ptr += 2;
                    break;
                }
                ptr = strstr (buf, "\r\r\n\r\r\n");
                if (ptr)
                {
                    ptr += 6;
                    break;
                }
                client->schedule_ms = client->worker->time_ms + 100;
                return 0;
            } while (0);
            client->refbuf = client->shared_data;
            client->shared_data = NULL;
            client->connection.discon_time = 0;
            client->parser = httpp_create_parser();
            httpp_initialize (client->parser, NULL);
            if (httpp_parse (client->parser, refbuf->data, refbuf->len))
            {
                recheck_cached_file (&useragents, client->worker->current_time.tv_sec);
                if (useragents.contents)
                {
                    const char *agent = httpp_getvar (client->parser, "user-agent");
                    void *result;

                    if (agent && avl_get_by_key (useragents.contents, (char *)agent, &result) == 0)
                    {
                        INFO1 ("dropping client because useragent is %s", agent);
                        return -1;
                    }
                }

                /* headers now parsed, make sure any sent content is next */
                if (strcmp("ICE",  httpp_getvar (client->parser, HTTPP_VAR_PROTOCOL)) &&
                        strcmp("HTTP", httpp_getvar (client->parser, HTTPP_VAR_PROTOCOL)))
                {
                    ERROR0("Bad HTTP protocol detected");
                    return -1;
                }
                auth_check_http (client);
                switch (client->parser->req_type)
                {
                    case httpp_req_get:
                        refbuf->len = PER_CLIENT_REFBUF_SIZE;
                        client->ops = &http_req_get_ops;
                        break;
                    case httpp_req_source:
                        client->pos = ptr - refbuf->data;
                        client->ops = &http_req_source_ops;
                        break;
                    case httpp_req_stats:
                        refbuf->len = PER_CLIENT_REFBUF_SIZE;
                        client->ops = &http_req_stats_ops;
                        break;
                    default:
                        WARN0("unhandled request type from client");
                        client_send_400 (client, "unknown request");
                        return 0;
                }
                return client->ops->process(client);
            }
            /* invalid http request */
            return -1;
        }
        if (ret && client->connection.error == 0)
        {
            client->schedule_ms = client->worker->time_ms + 100;
            return 0;
        }
    }
    refbuf_release (refbuf);
    client->shared_data = NULL;
    return -1;
}


static void *connection_thread (void *arg)
{
    ice_config_t *config;

#ifdef HAVE_SIGNALFD
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
    sigfd = signalfd(-1, &mask, 0);
#endif
    banned_ip.filename = NULL;
    banned_ip.file_mtime = 0;
    banned_ip.add_new_entry = add_banned_ip;
    banned_ip.compare = compare_banned_ip;
    allowed_ip.filename = NULL;
    allowed_ip.file_mtime = 0;
    allowed_ip.add_new_entry = add_generic_text;
    allowed_ip.compare = compare_pattern;
    useragents.filename = NULL;
    useragents.file_mtime = 0;
    useragents.add_new_entry = add_generic_text;
    useragents.compare = compare_pattern;

    connection_running = 1;
    INFO0 ("connection thread started");

    config = config_get_config ();
    /* setup the banned/allowed IP filenames from the xml */
    if (config->banfile)
        banned_ip.filename = strdup (config->banfile);
    if (config->allowfile)
        allowed_ip.filename = strdup (config->allowfile);
    if (config->agentfile)
        useragents.filename = strdup (config->agentfile);

    get_ssl_certificate (config);
    if (config->chuid == 0)
        connection_setup_sockets (config);
    header_timeout = config->header_timeout;
    config_release_config ();

    while (connection_running)
    {
        client_t *client = accept_client ();
        if (client)
        {
            /* do a small delay here so the client has chance to send the request after
             * getting a connect. This also prevents excessively large number of new
             * listeners from joining at the same time */
            thread_sleep (3000);
            client_add_worker (client);
            stats_event_inc (NULL, "connections");
        }
    }
#ifdef HAVE_OPENSSL
    SSL_CTX_free (ssl_ctx);
#endif
    if (banned_ip.contents)  avl_tree_free (banned_ip.contents, free_filtered_line);
    if (allowed_ip.contents) avl_tree_free (allowed_ip.contents, free_filtered_line);
    if (useragents.contents) avl_tree_free (useragents.contents, free_filtered_line);
    banned_ip.contents = NULL;
    allowed_ip.contents = NULL;
    useragents.contents = NULL;
    free (banned_ip.filename);
    free (allowed_ip.filename);
    free (useragents.filename);

    INFO0 ("connection thread finished");

    return NULL;
}

void connection_thread_startup ()
{
#ifdef HAVE_SIGNALFD
    sigset_t mask;
    sigfillset(&mask);
    pthread_sigmask (SIG_SETMASK, &mask, NULL);
#endif
    connection_running = 0;
    while (conn_tid)
        thread_sleep (100001);

    conn_tid = thread_create ("connection", connection_thread, NULL, THREAD_ATTACHED);
}

void connection_thread_shutdown ()
{
    if (conn_tid)
    {
        connection_running = 0;
        INFO0("shutting down connection thread");
        thread_join (conn_tid);
        conn_tid = NULL;
    }
}


/* Called when activating a source. Verifies that the source count is not
 * exceeded and applies any initial parameters.
 */
int connection_complete_source (source_t *source)
{
    client_t *client = source->client;
    const char *contenttype = httpp_getvar (client->parser, "content-type");
    mount_proxy *mountinfo;
    format_type_t format_type;
    ice_config_t *config;

    /* setup format handler */
    if (contenttype != NULL)
    {
        format_type = format_get_type (contenttype);

        if (format_type == FORMAT_ERROR)
        {
            WARN1("Content-type \"%s\" not supported, dropping source", contenttype);
            return -1;
        }
    }
    else
    {
        WARN0("No content-type header, falling back to backwards compatibility mode "
                "for icecast 1.x relays. Assuming content is mp3.");
        format_type = FORMAT_TYPE_GENERIC;
    }

    format_plugin_clear (source->format, client);
    source->format->type = format_type;
    source->format->mount = source->mount;
    if (format_get_plugin (source->format, client) < 0)
    {
        WARN1 ("plugin format failed for \"%s\"", source->mount);
        return -1;
    }

    config = config_get_config();
    mountinfo = config_find_mount (config, source->mount);
    source_update_settings (config, source, mountinfo);
    INFO1 ("source %s is ready to start", source->mount);
    config_release_config();

    return 0;
}


static int _check_pass_http(http_parser_t *parser, 
        const char *correctuser, const char *correctpass)
{
    /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
    const char *header = httpp_getvar(parser, "authorization");
    char *userpass, *tmp;
    char *username, *password;

    if(header == NULL)
        return 0;

    if(strncmp(header, "Basic ", 6))
        return 0;

    userpass = util_base64_decode(header+6);
    if(userpass == NULL) {
        WARN1("Base64 decode of Authorization header \"%s\" failed",
                header+6);
        return 0;
    }

    tmp = strchr(userpass, ':');
    if(!tmp) {
        free(userpass);
        return 0;
    }
    *tmp = 0;
    username = userpass;
    password = tmp+1;

    if(strcmp(username, correctuser) || strcmp(password, correctpass)) {
        free(userpass);
        return 0;
    }
    free(userpass);

    return 1;
}

static int _check_pass_icy(http_parser_t *parser, const char *correctpass)
{
    const char *password;

    password = httpp_getvar(parser, HTTPP_VAR_ICYPASSWORD);
    if(!password)
        return 0;

    if (strcmp(password, correctpass))
        return 0;
    else
        return 1;
}

static int _check_pass_ice(http_parser_t *parser, const char *correctpass)
{
    const char *password;

    password = httpp_getvar(parser, "ice-password");
    if(!password)
        password = "";

    if (strcmp(password, correctpass))
        return 0;
    else
        return 1;
}

int connection_check_admin_pass(http_parser_t *parser)
{
    int ret;
    ice_config_t *config = config_get_config();
    char *pass = config->admin_password;
    char *user = config->admin_username;
    const char *protocol;

    if(!pass || !user) {
        config_release_config();
        return 0;
    }

    protocol = httpp_getvar (parser, HTTPP_VAR_PROTOCOL);
    if (protocol && strcmp (protocol, "ICY") == 0)
        ret = _check_pass_icy (parser, pass);
    else 
        ret = _check_pass_http (parser, user, pass);
    config_release_config();
    return ret;
}

int connection_check_relay_pass(http_parser_t *parser)
{
    int ret;
    ice_config_t *config = config_get_config();
    char *pass = config->relay_password;
    char *user = config->relay_username;

    if(!pass || !user) {
        config_release_config();
        return 0;
    }

    ret = _check_pass_http(parser, user, pass);
    config_release_config();
    return ret;
}


/* return 0 for failed, 1 for ok
 */
int connection_check_pass (http_parser_t *parser, const char *user, const char *pass)
{
    int ret;
    const char *protocol;

    if(!pass) {
        WARN0("No source password set, rejecting source");
        return -1;
    }

    protocol = httpp_getvar(parser, HTTPP_VAR_PROTOCOL);
    if(protocol != NULL && !strcmp(protocol, "ICY")) {
        ret = _check_pass_icy(parser, pass);
    }
    else {
        ret = _check_pass_http(parser, user, pass);
        if (!ret)
        {
            ice_config_t *config = config_get_config_unlocked();
            if (config->ice_login)
            {
                ret = _check_pass_ice(parser, pass);
                if(ret)
                    WARN0("Source is using deprecated icecast login");
            }
        }
    }
    return ret;
}


static int _handle_source_request (client_t *client)
{
    const char *uri = httpp_getvar (client->parser, HTTPP_VAR_URI);

    INFO1("Source logging in at mountpoint \"%s\"", uri);
    if (uri[0] != '/')
    {
        WARN0 ("source mountpoint not starting with /");
        client_send_401 (client, NULL);
        return 0;
    }
    switch (auth_check_source (client, uri))
    {
        case 0:         /* authenticated from config file */
            return source_startup (client, uri);
        case 1:         /* auth pending */
            break;
        default:        /* failed */
            INFO1("Source (%s) attempted to login with invalid or missing password", uri);
            client_send_401 (client, NULL);
    }
    return 0;
}


static int _handle_stats_request (client_t *client)
{
    const char *uri = httpp_getvar (client->parser, HTTPP_VAR_URI);

    if (connection_check_admin_pass (client->parser) == 0)
    {
        auth_add_listener (uri, client);
        return 0;
    }
    stats_add_listener (client, STATS_ALL);
    return 0;
}

static void check_for_filtering (ice_config_t *config, client_t *client, char *uri)
{
    char *pattern = config->access_log.exclude_ext;
    char *extension = strrchr (uri, '.');
    const char *type = httpp_get_query_param (client->parser, "type");

    if ((extension && strcmp (extension+1, "flv") == 0) || 
        (type && strcmp (type, ".flv") == 0))
    {
        client->flags |= CLIENT_WANTS_FLV;
        DEBUG0 ("listener has flv extension so flag it for special handling");
    }
    if (extension == NULL || uri == NULL)
        return;

    extension++;
    if (pattern == NULL)
        return;
    while (*pattern)
    {
        int len = strcspn (pattern, " ");
        if (strncmp (extension, pattern, len) == 0 && extension[len] == '\0')
        {
            client->flags |= CLIENT_SKIP_ACCESSLOG;
            return;
        }
        pattern += len;
        len = strspn (pattern, " "); /* find next pattern */
        pattern += len;
    }
}


static int _handle_get_request (client_t *client)
{
    int port;
    char *serverhost = NULL;
    int serverport = 0;
    aliases *alias;
    ice_config_t *config;
    char *uri = util_normalise_uri (httpp_getvar (client->parser, HTTPP_VAR_URI));
    int client_limit_reached = 0;

    if (uri == NULL)
    {
        client_send_400 (client, "invalid request URI");
        return 0;
    }
    DEBUG1 ("start with %s", uri);
    config = config_get_config();
    check_for_filtering (config, client, uri);
    port = config->port;
    if (client->server_conn)
    {
        serverhost = client->server_conn->bind_address;
        serverport = client->server_conn->port;
    }
    alias = config->aliases;

    /* there are several types of HTTP GET clients
    ** media clients, which are looking for a source (eg, URI = /stream.ogg)
    ** stats clients, which are looking for /admin/stats.xml
    ** and directory server authorizers, which are looking for /GUID-xxxxxxxx 
    ** (where xxxxxx is the GUID in question) - this isn't implemented yet.
    ** we need to handle the latter two before the former, as the latter two
    ** aren't subject to the limits.
    */
    /* TODO: add GUID-xxxxxx */

    /* Handle aliases */
    while(alias) {
        if(strcmp(uri, alias->source) == 0 && (alias->port == -1 || alias->port == serverport) && (alias->bind_address == NULL || (serverhost != NULL && strcmp(alias->bind_address, serverhost) == 0))) {
            char *newuri = strdup (alias->destination);
            DEBUG2 ("alias has made %s into %s", uri, newuri);
            free (uri);
            uri = newuri;
            break;
        }
        alias = alias->next;
    }
    if (global.clients > config->client_limit)
    {
        client_limit_reached = 1;
        WARN2 ("server client limit reached (%d/%d)", config->client_limit, global.clients);
    }
    config_release_config();

    stats_event_inc(NULL, "client_connections");

    /* Dispatch all admin requests */
    if (admin_handle_request (client, uri) == 0)
    {
        free (uri);
        return 0;
    }
    /* drop non-admin GET requests here if clients limit reached */
    if (client_limit_reached)
        client_send_403 (client, "Too many clients connected");
    else
        auth_add_listener (uri, client);
    free (uri);
    return 0;
}


/* close any open listening sockets and reopen new listener sockets based on the settings
 * in the configuration.
 */
int connection_setup_sockets (ice_config_t *config)
{
    int count = 0;
    listener_t *listener, **prev;

    global_lock();
    /* place sockets away from config, so we don't need to take config lock
     * in the accept loop. */
    if (global.serversock)
    {
        for (; count < global.server_sockets; count++)
        {
            sock_close (global.serversock [count]);
            config_clear_listener (global.server_conn [count]);
        }
        free (global.serversock);
        global.serversock = NULL;
        free (global.server_conn);
        global.server_conn = NULL;
    }
    if (config == NULL)
    {
        global_unlock();
        return 0;
    }

    count = 0;
    global.serversock = calloc (config->listen_sock_count, sizeof (sock_t));
    global.server_conn = calloc (config->listen_sock_count, sizeof (listener_t*));

    listener = config->listen_sock;
    prev = &config->listen_sock;
    while (listener)
    {
        int successful = 0;

        do
        {
            sock_t sock = sock_get_server_socket (listener->port, listener->bind_address);
            if (sock == SOCK_ERROR)
                break;
            if (sock_listen (sock, listener->qlen) == SOCK_ERROR)
            {
                sock_close (sock);
                break;
            }
            /* some win32 setups do not do TCP win scaling well, so allow an override */
            if (listener->so_sndbuf)
                sock_set_send_buffer (sock, listener->so_sndbuf);
            sock_set_blocking (sock, 0);
            successful = 1;
            global.serversock [count] = sock;
            global.server_conn [count] = listener;
            listener->refcount++;
            count++;
        } while(0);
        if (successful == 0)
        {
            if (listener->bind_address)
                ERROR2 ("Could not create listener socket on port %d bind %s",
                        listener->port, listener->bind_address);
            else
                ERROR1 ("Could not create listener socket on port %d", listener->port);
            /* remove failed connection */
            *prev = config_clear_listener (listener);
            listener = *prev;
            continue;
        }
        if (listener->bind_address)
            INFO2 ("listener socket on port %d address %s", listener->port, listener->bind_address);
        else
            INFO1 ("listener socket on port %d", listener->port);
        prev = &listener->next;
        listener = listener->next;
    }
    global.server_sockets = count;
    global_unlock();

    if (count)
        INFO1 ("%d listening sockets setup complete", count);
    else
        ERROR0 ("No listening sockets established");
    return count;
}


void connection_close(connection_t *con)
{
    if (con->con_time)
    {
        sock_close(con->sock);
        free(con->ip);
#ifdef HAVE_OPENSSL
        if (con->ssl) { SSL_shutdown (con->ssl); SSL_free (con->ssl); }
#endif
        memset (con, 0, sizeof (connection_t));
    }
}

