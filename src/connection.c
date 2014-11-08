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
 * Copyright 2011,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 *                      Dave 'justdave' Miller <justdave@mozilla.com>.
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

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
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
#define SHOUTCAST_SOURCE_AUTH 1
#define ICECAST_SOURCE_AUTH 0

typedef struct client_queue_tag {
    client_t *client;
    int offset;
    int stream_offset;
    int shoutcast;
    char *shoutcast_mount;
    struct client_queue_tag *next;
} client_queue_t;

typedef struct _thread_queue_tag {
    thread_type *thread_id;
    struct _thread_queue_tag *next;
} thread_queue_t;

typedef struct
{
    char *filename;
    time_t file_recheck;
    time_t file_mtime;
    avl_tree *contents;
} cache_file_contents;

static spin_t _connection_lock; // protects _current_id, _con_queue, _con_queue_tail
static volatile unsigned long _current_id = 0;
static int _initialized = 0;

static volatile client_queue_t *_req_queue = NULL, **_req_queue_tail = &_req_queue;
static volatile client_queue_t *_con_queue = NULL, **_con_queue_tail = &_con_queue;
static int ssl_ok;
#ifdef HAVE_OPENSSL
static SSL_CTX *ssl_ctx;
#endif

/* filtering client connection based on IP */
static cache_file_contents banned_ip, allowed_ip;

rwlock_t _source_shutdown_rwlock;

static void _handle_connection(void);

static int compare_ip (void *arg, void *a, void *b)
{
    const char *ip = (const char *)a;
    const char *pattern = (const char *)b;

    return strcmp (pattern, ip);
}


static int free_filtered_ip (void*x)
{
    free (x);
    return 1;
}


void connection_initialize(void)
{
    if (_initialized) return;
    
    thread_spin_create (&_connection_lock);
    thread_mutex_create(&move_clients_mutex);
    thread_rwlock_create(&_source_shutdown_rwlock);
    thread_cond_create(&global.shutdown_cond);
    _req_queue = NULL;
    _req_queue_tail = &_req_queue;
    _con_queue = NULL;
    _con_queue_tail = &_con_queue;

    banned_ip.contents = NULL;
    banned_ip.file_mtime = 0;

    allowed_ip.contents = NULL;
    allowed_ip.file_mtime = 0;

    _initialized = 1;
}

void connection_shutdown(void)
{
    if (!_initialized) return;
    
#ifdef HAVE_OPENSSL
    SSL_CTX_free (ssl_ctx);
#endif
    if (banned_ip.contents)  avl_tree_free (banned_ip.contents, free_filtered_ip);
    if (allowed_ip.contents) avl_tree_free (allowed_ip.contents, free_filtered_ip);
 
    thread_cond_destroy(&global.shutdown_cond);
    thread_rwlock_destroy(&_source_shutdown_rwlock);
    thread_spin_destroy (&_connection_lock);
    thread_mutex_destroy(&move_clients_mutex);

    _initialized = 0;
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
    SSL_METHOD *method;
    long ssl_opts;
    ssl_ok = 0;

    SSL_load_error_strings();                /* readable error messages */
    SSL_library_init();                      /* initialize library */

    method = SSLv23_server_method();
    ssl_ctx = SSL_CTX_new (method);
    ssl_opts = SSL_CTX_get_options (ssl_ctx);
#ifdef SSL_OP_NO_COMPRESSION
    SSL_CTX_set_options (ssl_ctx, ssl_opts|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3|SSL_OP_NO_COMPRESSION);
#else
    SSL_CTX_set_options (ssl_ctx, ssl_opts|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
#endif

    do
    {
        if (config->cert_file == NULL)
            break;
        if (SSL_CTX_use_certificate_chain_file (ssl_ctx, config->cert_file) <= 0)
        {
            ICECAST_LOG_WARN("Invalid cert file %s", config->cert_file);
            break;
        }
        if (SSL_CTX_use_PrivateKey_file (ssl_ctx, config->cert_file, SSL_FILETYPE_PEM) <= 0)
        {
            ICECAST_LOG_WARN("Invalid private key file %s", config->cert_file);
            break;
        }
        if (!SSL_CTX_check_private_key (ssl_ctx))
        {
            ICECAST_LOG_ERROR("Invalid %s - Private key does not match cert public key", config->cert_file);
            break;
        }
        if (SSL_CTX_set_cipher_list(ssl_ctx, config->cipher_list) <= 0) 
        { 
            ICECAST_LOG_WARN("Invalid cipher list: %s", config->cipher_list); 
        } 
        ssl_ok = 1;
        ICECAST_LOG_INFO("SSL certificate found at %s", config->cert_file);
        ICECAST_LOG_INFO("SSL using ciphers %s", config->cipher_list); 
        return;
    } while (0);
    ICECAST_LOG_INFO("No SSL capability on any configured ports");
}


/* handlers for reading and writing a connection_t when there is ssl
 * configured on the listening port
 */
static int connection_read_ssl (connection_t *con, void *buf, size_t len)
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

static int connection_send_ssl (connection_t *con, const void *buf, size_t len)
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
    ICECAST_LOG_INFO("No SSL capability");
}
#endif /* HAVE_OPENSSL */


/* handlers (default) for reading and writing a connection_t, no encrpytion
 * used just straight access to the socket
 */
static int connection_read (connection_t *con, void *buf, size_t len)
{
    int bytes = sock_read_bytes (con->sock, buf, len);
    if (bytes == 0)
        con->error = 1;
    if (bytes == -1 && !sock_recoverable (sock_error()))
        con->error = 1;
    return bytes;
}

static int connection_send (connection_t *con, const void *buf, size_t len)
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


/* function to handle the re-populating of the avl tree containing IP addresses
 * for deciding whether a connection of an incoming request is to be dropped.
 */
static void recheck_ip_file (cache_file_contents *cache)
{
    time_t now = time(NULL);
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
                avl_tree_free (cache->contents, free_filtered_ip);
                cache->contents = NULL;
            }
            return;
        }
        if (stat (cache->filename, &file_stat) < 0)
        {
            ICECAST_LOG_WARN("failed to check status of \"%s\": %s", cache->filename, strerror(errno));
            return;
        }
        if (file_stat.st_mtime == cache->file_mtime)
            return; /* common case, no update to file */

        cache->file_mtime = file_stat.st_mtime;

        file = fopen (cache->filename, "r");
        if (file == NULL)
        {
            ICECAST_LOG_WARN("Failed to open file \"%s\": %s", cache->filename, strerror (errno));
            return;
        }

        new_ips = avl_tree_new (compare_ip, NULL);

        while (get_line (file, line, MAX_LINE_LEN))
        {
            char *str;
            if(!line[0] || line[0] == '#')
                continue;
            count++;
            str = strdup (line);
            if (str)
                avl_insert (new_ips, str);
        }
        fclose (file);
        ICECAST_LOG_INFO("%d entries read from file \"%s\"", count, cache->filename);

        if (cache->contents) avl_tree_free (cache->contents, free_filtered_ip);
        cache->contents = new_ips;
    }
}


/* return 0 if the passed ip address is not to be handled by icecast, non-zero otherwise */
static int accept_ip_address (char *ip)
{
    void *result;

    recheck_ip_file (&banned_ip);
    recheck_ip_file (&allowed_ip);

    if (banned_ip.contents)
    {
        if (avl_get_by_key (banned_ip.contents, ip, &result) == 0)
        {
            ICECAST_LOG_DEBUG("%s is banned", ip);
            return 0;
        }
    }
    if (allowed_ip.contents)
    {
        if (avl_get_by_key (allowed_ip.contents, ip, &result) == 0)
        {
            ICECAST_LOG_DEBUG("%s is allowed", ip);
            return 1;
        }
        else
        {
            ICECAST_LOG_DEBUG("%s is not allowed", ip);
            return 0;
        }
    }
    return 1;
}


connection_t *connection_create (sock_t sock, sock_t serversock, char *ip)
{
    connection_t *con;
    con = (connection_t *)calloc(1, sizeof(connection_t));
    if (con)
    {
        con->sock = sock;
        con->serversock = serversock;
        con->con_time = time(NULL);
        con->id = _next_connection_id();
        con->ip = ip;
        con->read = connection_read;
        con->send = connection_send;
    }

    return con;
}

/* prepare connection for interacting over a SSL connection
 */
void connection_uses_ssl (connection_t *con)
{
#ifdef HAVE_OPENSSL
    con->read = connection_read_ssl;
    con->send = connection_send_ssl;
    con->ssl = SSL_new (ssl_ctx);
    SSL_set_accept_state (con->ssl);
    SSL_set_fd (con->ssl, con->sock);
#endif
}

static sock_t wait_for_serversock(int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds [global.server_sockets];
    int i, ret;

    for(i=0; i < global.server_sockets; i++) {
        ufds[i].fd = global.serversock[i];
        ufds[i].events = POLLIN;
        ufds[i].revents = 0;
    }

    ret = poll(ufds, global.server_sockets, timeout);
    if(ret < 0) {
        return SOCK_ERROR;
    }
    else if(ret == 0) {
        return SOCK_ERROR;
    }
    else {
        int dst;
        for(i=0; i < global.server_sockets; i++) {
            if(ufds[i].revents & POLLIN)
                return ufds[i].fd;
            if(ufds[i].revents & (POLLHUP|POLLERR|POLLNVAL))
            {
                if (ufds[i].revents & (POLLHUP|POLLERR))
                {
                    sock_close (global.serversock[i]);
                    ICECAST_LOG_WARN("Had to close a listening socket");
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

    if(timeout >= 0) {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        p = &tv;
    }

    ret = select(max+1, &rfds, NULL, NULL, p);
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

static connection_t *_accept_connection(int duration)
{
    sock_t sock, serversock;
    char *ip;

    serversock = wait_for_serversock (duration);
    if (serversock == SOCK_ERROR)
        return NULL;

    /* malloc enough room for a full IP address (including ipv6) */
    ip = (char *)malloc(MAX_ADDR_LEN);

    sock = sock_accept(serversock, ip, MAX_ADDR_LEN);
    if (sock != SOCK_ERROR)
    {
        connection_t *con = NULL;
        /* Make any IPv4 mapped IPv6 address look like a normal IPv4 address */
        if (strncmp (ip, "::ffff:", 7) == 0)
            memmove (ip, ip+7, strlen (ip+7)+1);

        if (accept_ip_address (ip))
            con = connection_create (sock, serversock, ip);
        if (con)
            return con;
        sock_close (sock);
    }
    else
    {
        if (!sock_recoverable(sock_error()))
        {
            ICECAST_LOG_WARN("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
            thread_sleep (500000);
        }
    }
    free(ip);
    return NULL;
}


/* add client to connection queue. At this point some header information
 * has been collected, so we now pass it onto the connection thread for
 * further processing
 */
static void _add_connection (client_queue_t *node)
{
    thread_spin_lock (&_connection_lock);
    *_con_queue_tail = node;
    _con_queue_tail = (volatile client_queue_t **)&node->next;
    thread_spin_unlock (&_connection_lock);
}


/* this returns queued clients for the connection thread. headers are
 * already provided, but need to be parsed.
 */
static client_queue_t *_get_connection(void)
{
    client_queue_t *node = NULL;

    thread_spin_lock (&_connection_lock);

    if (_con_queue)
    {
        node = (client_queue_t *)_con_queue;
        _con_queue = node->next;
        if (_con_queue == NULL)
            _con_queue_tail = &_con_queue;
        node->next = NULL;
    }

    thread_spin_unlock (&_connection_lock);
    return node;
}


/* run along queue checking for any data that has come in or a timeout */
static void process_request_queue (void)
{
    client_queue_t **node_ref = (client_queue_t **)&_req_queue;
    ice_config_t *config = config_get_config ();
    int timeout = config->header_timeout;
    config_release_config();

    while (*node_ref)
    {
        client_queue_t *node = *node_ref;
        client_t *client = node->client;
        int len = PER_CLIENT_REFBUF_SIZE - 1 - node->offset;
        char *buf = client->refbuf->data + node->offset;

        if (len > 0)
        {
            if (client->con->con_time + timeout <= time(NULL))
                len = 0;
            else
                len = client_read_bytes (client, buf, len);
        }

        if (len > 0)
        {
            int pass_it = 1;
            char *ptr;

            /* handle \n, \r\n and nsvcap which for some strange reason has
             * EOL as \r\r\n */
            node->offset += len;
            client->refbuf->data [node->offset] = '\000';
            do
            {
                if (node->shoutcast == 1)
                {
                    /* password line */
                    if (strstr (client->refbuf->data, "\r\r\n") != NULL)
                        break;
                    if (strstr (client->refbuf->data, "\r\n") != NULL)
                        break;
                    if (strstr (client->refbuf->data, "\n") != NULL)
                        break;
                }
                /* stream_offset refers to the start of any data sent after the
                 * http style headers, we don't want to lose those */
                ptr = strstr (client->refbuf->data, "\r\r\n\r\r\n");
                if (ptr)
                {
                    node->stream_offset = (ptr+6) - client->refbuf->data;
                    break;
                }
                ptr = strstr (client->refbuf->data, "\r\n\r\n");
                if (ptr)
                {
                    node->stream_offset = (ptr+4) - client->refbuf->data;
                    break;
                }
                ptr = strstr (client->refbuf->data, "\n\n");
                if (ptr)
                {
                    node->stream_offset = (ptr+2) - client->refbuf->data;
                    break;
                }
                pass_it = 0;
            } while (0);

            if (pass_it)
            {
                if ((client_queue_t **)_req_queue_tail == &(node->next))
                    _req_queue_tail = (volatile client_queue_t **)node_ref;
                *node_ref = node->next;
                node->next = NULL;
                _add_connection (node);
                continue;
            }
        }
        else
        {
            if (len == 0 || client->con->error)
            {
                if ((client_queue_t **)_req_queue_tail == &node->next)
                    _req_queue_tail = (volatile client_queue_t **)node_ref;
                *node_ref = node->next;
                client_destroy (client);
                free (node);
                continue;
            }
        }
        node_ref = &node->next;
    }
    _handle_connection();
}


/* add node to the queue of requests. This is where the clients are when
 * initial http details are read.
 */
static void _add_request_queue (client_queue_t *node)
{
    *_req_queue_tail = node;
    _req_queue_tail = (volatile client_queue_t **)&node->next;
}


void connection_accept_loop (void)
{
    connection_t *con;
    ice_config_t *config;
    int duration = 300;

    config = config_get_config ();
    get_ssl_certificate (config);
    config_release_config ();

    while (global.running == ICECAST_RUNNING)
    {
        con = _accept_connection (duration);

        if (con)
        {
            client_queue_t *node;
            ice_config_t *config;
            client_t *client = NULL;
            listener_t *listener;

            global_lock();
            if (client_create (&client, con, NULL) < 0)
            {
                global_unlock();
                client_send_403 (client, "Icecast connection limit reached");
                /* don't be too eager as this is an imposed hard limit */
                thread_sleep (400000);
                continue;
            }

            /* setup client for reading incoming http */
            client->refbuf->data [PER_CLIENT_REFBUF_SIZE-1] = '\000';

            if (sock_set_blocking (client->con->sock, 0) || sock_set_nodelay (client->con->sock))
            {
                global_unlock();
                ICECAST_LOG_WARN("failed to set tcp options on client connection, dropping");
                client_destroy (client);
                continue;
            }

            node = calloc (1, sizeof (client_queue_t));
            if (node == NULL)
            {
                global_unlock();
                client_destroy (client);
                continue;
            }
            node->client = client;

            config = config_get_config();
            listener = config_get_listen_sock (config, client->con);

            if (listener)
            {
                if (listener->shoutcast_compat)
                    node->shoutcast = 1;
                if (listener->ssl && ssl_ok)
                    connection_uses_ssl (client->con);
                if (listener->shoutcast_mount)
                    node->shoutcast_mount = strdup (listener->shoutcast_mount);
            }
            global_unlock();
            config_release_config();

            _add_request_queue (node);
            stats_event_inc (NULL, "connections");
            duration = 5;
        }
        else
        {
            if (_req_queue == NULL)
                duration = 300; /* use longer timeouts when nothing waiting */
        }
        process_request_queue ();
    }

    /* Give all the other threads notification to shut down */
    thread_cond_broadcast(&global.shutdown_cond);

    /* wait for all the sources to shutdown */
    thread_rwlock_wlock(&_source_shutdown_rwlock);
    thread_rwlock_unlock(&_source_shutdown_rwlock);
}


/* Called when activating a source. Verifies that the source count is not
 * exceeded and applies any initial parameters.
 */
int connection_complete_source (source_t *source, int response)
{
    ice_config_t *config;

    global_lock ();
    ICECAST_LOG_DEBUG("sources count is %d", global.sources);

    config = config_get_config();
    if (global.sources < config->source_limit)
    {
        const char *contenttype;
        const char *expectcontinue;
        mount_proxy *mountinfo;
        format_type_t format_type;

        /* setup format handler */
        contenttype = httpp_getvar (source->parser, "content-type");
        if (contenttype != NULL)
        {
            format_type = format_get_type (contenttype);

            if (format_type == FORMAT_ERROR)
            {
                config_release_config();
                global_unlock();
                if (response) {
                    client_send_403 (source->client, "Content-type not supported");
                    source->client = NULL;
                }
                ICECAST_LOG_WARN("Content-type \"%s\" not supported, dropping source", contenttype);
                return -1;
            }
        } else if (source->parser->req_type == httpp_req_put) {
            config_release_config();
            global_unlock();
            if (response) {
                client_send_403 (source->client, "No Content-type given");
                source->client = NULL;
            }
            ICECAST_LOG_ERROR("Content-type not given in PUT request, dropping source");
            return -1;
        } else {
            ICECAST_LOG_ERROR("No content-type header, falling back to backwards compatibility mode "
                    "for icecast 1.x relays. Assuming content is mp3. This behaviour is deprecated "
                    "and the source client will NOT work with future Icecast versions!");
            format_type = FORMAT_TYPE_GENERIC;
        }

        if (format_get_plugin (format_type, source) < 0)
        {
            global_unlock();
            config_release_config();
            if (response)
            {
                client_send_403 (source->client, "internal format allocation problem");
                source->client = NULL;
            }
            ICECAST_LOG_WARN("plugin format failed for \"%s\"", source->mount);
            return -1;
        }

	/* For PUT support we check for 100-continue and send back a 100 to stay in spec */
	expectcontinue = httpp_getvar (source->parser, "expect");
	if (expectcontinue != NULL)
	{
#ifdef HAVE_STRCASESTR
	    if (strcasestr (expectcontinue, "100-continue") != NULL)
#else
	    ICECAST_LOG_WARN("OS doesn't support case insenestive substring checks...");
	    if (strstr (expectcontinue, "100-continue") != NULL)
#endif
	    {
		client_send_100 (source->client);
	    }
	}

        global.sources++;
        stats_event_args (NULL, "sources", "%d", global.sources);
        global_unlock();

        source->running = 1;
        mountinfo = config_find_mount (config, source->mount, MOUNT_TYPE_NORMAL);
        source_update_settings (config, source, mountinfo);
        config_release_config();
        slave_rebuild_mounts();

        source->shutdown_rwlock = &_source_shutdown_rwlock;
        ICECAST_LOG_DEBUG("source is ready to start");

        return 0;
    }
    ICECAST_LOG_WARN("Request to add source when maximum source limit "
            "reached %d", global.sources);

    global_unlock();
    config_release_config();

    if (response)
    {
        client_send_403 (source->client, "too many sources connected");
        source->client = NULL;
    }

    return -1;
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
        ICECAST_LOG_WARN("Base64 decode of Authorization header \"%s\" failed",
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
        ICECAST_LOG_WARN("No source password set, rejecting source");
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
                    ICECAST_LOG_WARN("Source is using deprecated icecast login");
            }
        }
    }
    return ret;
}


/* only called for native icecast source clients */
static void _handle_source_request (client_t *client, const char *uri)
{
    ICECAST_LOG_INFO("Source logging in at mountpoint \"%s\" from %s",
        uri, client->con->ip);

    if (uri[0] != '/')
    {
        ICECAST_LOG_WARN("source mountpoint not starting with /");
        client_send_401 (client);
        return;
    }
    switch (client_check_source_auth (client, uri))
    {
        case 0: /* authenticated from config file */
            source_startup (client, uri, ICECAST_SOURCE_AUTH);
            break;

        case 1: /* auth pending */
            break;

        default: /* failed */
            ICECAST_LOG_INFO("Source (%s) attempted to login with invalid or missing password", uri);
            client_send_401(client);
            break;
    }
}


void source_startup (client_t *client, const char *uri, int auth_style)
{
    source_t *source;
    source = source_reserve (uri);

    if (source)
    {
        source->client = client;
        source->parser = client->parser;
        source->con = client->con;
        if (connection_complete_source (source, 1) < 0)
        {
            source_clear_source (source);
            source_free_source (source);
            return;
        }
        client->respcode = 200;
        if (auth_style == SHOUTCAST_SOURCE_AUTH)
        {
            source->shoutcast_compat = 1;
            source_client_callback (client, source);
        }
        else
        {
            refbuf_t *ok = refbuf_new (PER_CLIENT_REFBUF_SIZE);
            client->respcode = 200;
            snprintf (ok->data, PER_CLIENT_REFBUF_SIZE,
                    "HTTP/1.0 200 OK\r\n\r\n");
            ok->len = strlen (ok->data);
            /* we may have unprocessed data read in, so don't overwrite it */
            ok->associated = client->refbuf;
            client->refbuf = ok;
            fserve_add_client_callback (client, source_client_callback, source);
        }
    }
    else
    {
        client_send_403 (client, "Mountpoint in use");
        ICECAST_LOG_WARN("Mountpoint %s in use", uri);
    }
}


static void _handle_stats_request (client_t *client, char *uri)
{
    stats_event_inc(NULL, "stats_connections");

    if (connection_check_admin_pass (client->parser) == 0)
    {
        client_send_401 (client);
        ICECAST_LOG_ERROR("Bad password for stats connection");
        return;
    }

    client->respcode = 200;
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 200 OK\r\n\r\n");
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client_callback (client, stats_callback, NULL);
}

static void _handle_get_request (client_t *client, char *passed_uri)
{
    char *serverhost = NULL;
    int serverport = 0;
    aliases *alias;
    ice_config_t *config;
    char *uri = passed_uri;
    listener_t *listen_sock;

    config = config_get_config();

    listen_sock = config_get_listen_sock (config, client->con);
    if (listen_sock)
    {
        serverhost = listen_sock->bind_address;
        serverport = listen_sock->port;
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
            uri = strdup (alias->destination);
            ICECAST_LOG_DEBUG("alias has made %s into %s", passed_uri, uri);
            break;
        }
        alias = alias->next;
    }
    config_release_config();

    stats_event_inc(NULL, "client_connections");

    /* Dispatch all admin requests */
    if ((strcmp(uri, "/admin.cgi") == 0) ||
        (strncmp(uri, "/admin/", 7) == 0)) {
        admin_handle_request(client, uri);
        if (uri != passed_uri) free (uri);
        return;
    }
    auth_add_listener (uri, client);
    if (uri != passed_uri) free (uri);
}

static void _handle_shoutcast_compatible (client_queue_t *node)
{
    char *http_compliant;
    int http_compliant_len = 0;
    http_parser_t *parser;
    ice_config_t *config = config_get_config ();
    char *shoutcast_mount;
    client_t *client = node->client;

    if (node->shoutcast_mount)
        shoutcast_mount = node->shoutcast_mount;
    else
        shoutcast_mount = config->shoutcast_mount;

    if (node->shoutcast == 1)
    {
        char *source_password, *ptr, *headers;
        mount_proxy *mountinfo = config_find_mount (config, shoutcast_mount, MOUNT_TYPE_NORMAL);

        if (mountinfo && mountinfo->password)
            source_password = strdup (mountinfo->password);
        else
        {
            if (config->source_password) 
                source_password = strdup (config->source_password);
            else
                source_password = NULL;
        }
        config_release_config();

        /* Get rid of trailing \r\n or \n after password */
        ptr = strstr (client->refbuf->data, "\r\r\n");
        if (ptr)
            headers = ptr+3;
        else
        {
            ptr = strstr (client->refbuf->data, "\r\n");
            if (ptr)
                headers = ptr+2;
            else
            {
                ptr = strstr (client->refbuf->data, "\n");
                if (ptr)
                    headers = ptr+1;
            }
        }

        if (ptr == NULL)
        {
            client_destroy (client);
            free (source_password);
            free (node->shoutcast_mount);
            free (node);
            return;
        }
        *ptr = '\0';

        if (source_password && strcmp (client->refbuf->data, source_password) == 0)
        {
            client->respcode = 200;
            /* send this non-blocking but if there is only a partial write
             * then leave to header timeout */
            sock_write (client->con->sock, "OK2\r\nicy-caps:11\r\n\r\n");
            node->offset -= (headers - client->refbuf->data);
            memmove (client->refbuf->data, headers, node->offset+1);
            node->shoutcast = 2;
            /* we've checked the password, now send it back for reading headers */
            _add_request_queue (node);
            free (source_password);
            return;
        }
        else
            ICECAST_LOG_INFO("password does not match \"%s\"", client->refbuf->data);
        client_destroy (client);
        free (source_password);
        free (node->shoutcast_mount);
        free (node);
        return;
    }
    /* actually make a copy as we are dropping the config lock */
    shoutcast_mount = strdup (shoutcast_mount);
    config_release_config();
    /* Here we create a valid HTTP request based of the information
       that was passed in via the non-HTTP style protocol above. This
       means we can use some of our existing code to handle this case */
    http_compliant_len = 20 + strlen (shoutcast_mount) + node->offset;
    http_compliant = (char *)calloc(1, http_compliant_len);
    snprintf (http_compliant, http_compliant_len,
            "SOURCE %s HTTP/1.0\r\n%s", shoutcast_mount, client->refbuf->data);
    parser = httpp_create_parser();
    httpp_initialize(parser, NULL);
    if (httpp_parse (parser, http_compliant, strlen(http_compliant)))
    {
        /* we may have more than just headers, so prepare for it */
        if (node->stream_offset == node->offset)
            client->refbuf->len = 0;
        else
        {
            char *ptr = client->refbuf->data;
            client->refbuf->len = node->offset - node->stream_offset;
            memmove (ptr, ptr + node->stream_offset, client->refbuf->len);
        }
        client->parser = parser;
        source_startup (client, shoutcast_mount, SHOUTCAST_SOURCE_AUTH);
    }
    else {
        httpp_destroy (parser);
        client_destroy (client);
    }
    free (http_compliant);
    free (shoutcast_mount);
    free (node->shoutcast_mount);
    free (node);
    return;
}


/* Connection thread. Here we take clients off the connection queue and check
 * the contents provided. We set up the parser then hand off to the specific
 * request handler.
 */
static void _handle_connection(void)
{
    http_parser_t *parser;
    const char *rawuri;
    client_queue_t *node;

    while (1)
    {
        node = _get_connection();
        if (node)
        {
            client_t *client = node->client;

            /* Check for special shoutcast compatability processing */
            if (node->shoutcast)
            {
                _handle_shoutcast_compatible (node);
                continue;
            }

            /* process normal HTTP headers */
            parser = httpp_create_parser();
            httpp_initialize(parser, NULL);
            client->parser = parser;
            if (httpp_parse (parser, client->refbuf->data, node->offset))
            {
                char *uri;

                /* we may have more than just headers, so prepare for it */
                if (node->stream_offset == node->offset)
                    client->refbuf->len = 0;
                else
                {
                    char *ptr = client->refbuf->data;
                    client->refbuf->len = node->offset - node->stream_offset;
                    memmove (ptr, ptr + node->stream_offset, client->refbuf->len);
                }

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);

                /* assign a port-based shoutcast mountpoint if required */
                if (node->shoutcast_mount && strcmp (rawuri, "/admin.cgi") == 0)
                    httpp_set_query_param (client->parser, "mount", node->shoutcast_mount);

                free (node->shoutcast_mount);
                free (node);

                if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
                    strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
                    ICECAST_LOG_ERROR("Bad HTTP protocol detected");
                    client_destroy (client);
                    continue;
                }

                uri = util_normalise_uri(rawuri);

                if (uri == NULL)
                {
                    client_destroy (client);
                    continue;
                }

                if (parser->req_type == httpp_req_source || parser->req_type == httpp_req_put) {
                    _handle_source_request (client, uri);
                }
                else if (parser->req_type == httpp_req_stats) {
                    _handle_stats_request (client, uri);
                }
                else if (parser->req_type == httpp_req_get) {
                    _handle_get_request (client, uri);
                }
                else {
                    ICECAST_LOG_ERROR("Wrong request type from client");
                    client_send_400 (client, "unknown request");
                }

                free(uri);
            } 
            else
            {
                free (node);
                ICECAST_LOG_ERROR("HTTP request parsing failed");
                client_destroy (client);
            }
            continue;
        }
        break;
    }
}


/* called when listening thread is not checking for incoming connections */
int connection_setup_sockets (ice_config_t *config)
{
    int count = 0;
    listener_t *listener, **prev;

    free (banned_ip.filename);
    banned_ip.filename = NULL;
    free (allowed_ip.filename);
    allowed_ip.filename = NULL;

    global_lock();
    if (global.serversock)
    {
        for (; count < global.server_sockets; count++)
            sock_close (global.serversock [count]);
        free (global.serversock);
        global.serversock = NULL;
    }
    if (config == NULL)
    {
        global_unlock();
        return 0;
    }

    /* setup the banned/allowed IP filenames from the xml */
    if (config->banfile)
        banned_ip.filename = strdup (config->banfile);

    if (config->allowfile)
        allowed_ip.filename = strdup (config->allowfile);

    count = 0;
    global.serversock = calloc (config->listen_sock_count, sizeof (sock_t));

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
            if (sock_listen (sock, ICECAST_LISTEN_QUEUE) == SOCK_ERROR)
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
            count++;
        } while(0);
        if (successful == 0)
        {
            if (listener->bind_address)
                ICECAST_LOG_ERROR("Could not create listener socket on port %d bind %s",
                        listener->port, listener->bind_address);
            else
                ICECAST_LOG_ERROR("Could not create listener socket on port %d", listener->port);
            /* remove failed connection */
            *prev = config_clear_listener (listener);
            listener = *prev;
            continue;
        }
        if (listener->bind_address)
            ICECAST_LOG_INFO("listener socket on port %d address %s", listener->port, listener->bind_address);
        else
            ICECAST_LOG_INFO("listener socket on port %d", listener->port);
        prev = &listener->next;
        listener = listener->next;
    }
    global.server_sockets = count;
    global_unlock();

    if (count == 0)
        ICECAST_LOG_ERROR("No listening sockets established");

    return count;
}


void connection_close(connection_t *con)
{
    sock_close(con->sock);
    if (con->ip) free(con->ip);
    if (con->host) free(con->host);
#ifdef HAVE_OPENSSL
    if (con->ssl) { SSL_shutdown (con->ssl); SSL_free (con->ssl); }
#endif
    free(con);
}
