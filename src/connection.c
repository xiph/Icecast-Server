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
#include <string.h>
#include <time.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#ifndef _WIN32
#include <sys/time.h>
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
    struct client_queue_tag *next;
} client_queue_t;

typedef struct _thread_queue_tag {
    thread_type *thread_id;
    struct _thread_queue_tag *next;
} thread_queue_t;

static mutex_t _connection_mutex;
static volatile unsigned long _current_id = 0;
static int _initialized = 0;
static thread_type *tid;

static volatile client_queue_t *_req_queue = NULL, **_req_queue_tail = &_req_queue;
static volatile client_queue_t *_con_queue = NULL, **_con_queue_tail = &_con_queue;
static mutex_t _con_queue_mutex;
static mutex_t _req_queue_mutex;

rwlock_t _source_shutdown_rwlock;

static void *_handle_connection(void *arg);

void connection_initialize(void)
{
    if (_initialized) return;
    
    thread_mutex_create(&_connection_mutex);
    thread_mutex_create(&_con_queue_mutex);
    thread_mutex_create(&_req_queue_mutex);
    thread_mutex_create(&move_clients_mutex);
    thread_rwlock_create(&_source_shutdown_rwlock);
    thread_cond_create(&global.shutdown_cond);
    _req_queue = NULL;
    _req_queue_tail = &_req_queue;
    _con_queue = NULL;
    _con_queue_tail = &_con_queue;

    _initialized = 1;
}

void connection_shutdown(void)
{
    if (!_initialized) return;
    
    thread_cond_destroy(&global.shutdown_cond);
    thread_rwlock_destroy(&_source_shutdown_rwlock);
    thread_mutex_destroy(&_con_queue_mutex);
    thread_mutex_destroy(&_req_queue_mutex);
    thread_mutex_destroy(&_connection_mutex);
    thread_mutex_destroy(&move_clients_mutex);

    _initialized = 0;
}

static unsigned long _next_connection_id(void)
{
    unsigned long id;

    thread_mutex_lock(&_connection_mutex);
    id = _current_id++;
    thread_mutex_unlock(&_connection_mutex);

    return id;
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
    }

    return con;
}

static int wait_for_serversock(int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds[MAX_LISTEN_SOCKETS];
    int i, ret;

    for(i=0; i < global.server_sockets; i++) {
        ufds[i].fd = global.serversock[i];
        ufds[i].events = POLLIN;
        ufds[i].revents = 0;
    }

    ret = poll(ufds, global.server_sockets, timeout);
    if(ret < 0) {
        return -2;
    }
    else if(ret == 0) {
        return -1;
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
                    close (global.serversock[i]);
                    WARN0("Had to close a listening socket");
                }
                global.serversock[i] = -1;
            }
        }
        /* remove any closed sockets */
        for(i=0, dst=0; i < global.server_sockets; i++)
        {
            if (global.serversock[i] == -1)
                continue;
            if (i!=dst)
                global.serversock[dst] = global.serversock[i];
            dst++;
        }
        global.server_sockets = dst;
        return -1;
    }
#else
    fd_set rfds;
    struct timeval tv, *p=NULL;
    int i, ret;
    int max = -1;

    FD_ZERO(&rfds);

    for(i=0; i < global.server_sockets; i++) {
        FD_SET(global.serversock[i], &rfds);
        if(global.serversock[i] > max)
            max = global.serversock[i];
    }

    if(timeout >= 0) {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        p = &tv;
    }

    ret = select(max+1, &rfds, NULL, NULL, p);
    if(ret < 0) {
        return -2;
    }
    else if(ret == 0) {
        return -1;
    }
    else {
        for(i=0; i < global.server_sockets; i++) {
            if(FD_ISSET(global.serversock[i], &rfds))
                return global.serversock[i];
        }
        return -1; /* Should be impossible, stop compiler warnings */
    }
#endif
}

static connection_t *_accept_connection(void)
{
    int sock;
    connection_t *con;
    char *ip;
    int serversock; 

    serversock = wait_for_serversock(100);
    if(serversock < 0)
        return NULL;

    /* malloc enough room for a full IP address (including ipv6) */
    ip = (char *)malloc(MAX_ADDR_LEN);

    sock = sock_accept(serversock, ip, MAX_ADDR_LEN);
    if (sock >= 0)
    {
        con = connection_create (sock, serversock, ip);
        if (con == NULL)
            free (ip);

        return con;
    }

    if (!sock_recoverable(sock_error()))
        WARN2("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
    
    free(ip);

    return NULL;
}


/* add client to connection queue. At this point some header information
 * has been collected, so we now pass it onto the connection thread for
 * further processing
 */
static void _add_connection (client_queue_t *node)
{
    thread_mutex_lock (&_con_queue_mutex);
    *_con_queue_tail = node;
    _con_queue_tail = (volatile client_queue_t **)&node->next;
    thread_mutex_unlock (&_con_queue_mutex);
}


/* this returns queued clients for the connection thread. headers are
 * already provided, but need to be parsed.
 */
static client_queue_t *_get_connection(void)
{
    client_queue_t *node = NULL;

    /* common case, no new connections so don't bother taking locks */
    if (_con_queue)
    {
        thread_mutex_lock (&_con_queue_mutex);
        node = (client_queue_t *)_con_queue;
        _con_queue = node->next;
        if (_con_queue == NULL)
            _con_queue_tail = &_con_queue;
        thread_mutex_unlock (&_con_queue_mutex);
    }
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
}


/* add node to the queue of requests. This is where the clients are when
 * initial http details are read.
 */
static void _add_request_queue (client_queue_t *node)
{
    thread_mutex_lock (&_req_queue_mutex);
    *_req_queue_tail = node;
    _req_queue_tail = (volatile client_queue_t **)&node->next;
    thread_mutex_unlock (&_req_queue_mutex);
}


void connection_accept_loop(void)
{
    connection_t *con;

    tid = thread_create ("connection thread", _handle_connection, NULL, THREAD_ATTACHED);

    while (global.running == ICE_RUNNING)
    {
        con = _accept_connection();

        if (con)
        {
            client_queue_t *node;
            ice_config_t *config;
            int i;
            client_t *client = NULL;

            global_lock();
            if (client_create (&client, con, NULL) < 0)
            {
                global_unlock();
                client_send_403 (client, "Icecast connection limit reached");
                continue;
            }
            global_unlock();

            /* setup client for reading incoming http */
            client->refbuf->data [PER_CLIENT_REFBUF_SIZE-1] = '\000';

            node = calloc (1, sizeof (client_queue_t));
            if (node == NULL)
            {
                client_destroy (client);
                continue;
            }
            node->client = client;

            /* Check for special shoutcast compatability processing */
            config = config_get_config();
            for (i = 0; i < global.server_sockets; i++)
            {
                if (global.serversock[i] == con->serversock)
                {
                    if (config->listeners[i].shoutcast_compat)
                        node->shoutcast = 1;
                }
            }
            config_release_config(); 

            sock_set_blocking (client->con->sock, SOCK_NONBLOCK);
            sock_set_nodelay (client->con->sock);

            _add_request_queue (node);
            stats_event_inc (NULL, "connections");
        }
        process_request_queue ();
    }

    /* Give all the other threads notification to shut down */
    thread_cond_broadcast(&global.shutdown_cond);

    if (tid)
        thread_join (tid);

    /* wait for all the sources to shutdown */
    thread_rwlock_wlock(&_source_shutdown_rwlock);
    thread_rwlock_unlock(&_source_shutdown_rwlock);
}


/* Called when activating a source. Verifies that the source count is not
 * exceeded and applies any initial parameters.
 */
int connection_complete_source (source_t *source, int response)
{
    ice_config_t *config = config_get_config();

    global_lock ();
    DEBUG1 ("sources count is %d", global.sources);

    if (global.sources < config->source_limit)
    {
        char *contenttype;
        mount_proxy *mountinfo;
        format_type_t format_type;

        /* setup format handler */
        contenttype = httpp_getvar (source->parser, "content-type");
        if (contenttype != NULL)
        {
            format_type = format_get_type (contenttype);

            if (format_type == FORMAT_ERROR)
            {
                global_unlock();
                config_release_config();
                if (response)
                {
                    client_send_403 (source->client, "Content-type not supported");
                    source->client = NULL;
                }
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

        if (format_get_plugin (format_type, source) < 0)
        {
            global_unlock();
            config_release_config();
            if (response)
            {
                client_send_403 (source->client, "internal format allocation problem");
                source->client = NULL;
            }
            WARN1 ("plugin format failed for \"%s\"", source->mount);
            return -1;
        }

        global.sources++;
        stats_event_args (NULL, "sources", "%d", global.sources);
        global_unlock();

        source->running = 1;
        mountinfo = config_find_mount (config, source->mount);
        if (mountinfo == NULL)
            source_update_settings (config, source, mountinfo);
        source_recheck_mounts ();
        config_release_config();

        source->shutdown_rwlock = &_source_shutdown_rwlock;
        DEBUG0 ("source is ready to start");

        return 0;
    }
    WARN1("Request to add source when maximum source limit "
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
        char *correctuser, char *correctpass)
{
    /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
    char *header = httpp_getvar(parser, "authorization");
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

static int _check_pass_icy(http_parser_t *parser, char *correctpass)
{
    char *password;

    password = httpp_getvar(parser, HTTPP_VAR_ICYPASSWORD);
    if(!password)
        return 0;

    if (strcmp(password, correctpass))
        return 0;
    else
        return 1;
}

static int _check_pass_ice(http_parser_t *parser, char *correctpass)
{
    char *password;

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
    char *protocol;

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

int connection_check_source_pass(http_parser_t *parser, const char *mount)
{
    ice_config_t *config = config_get_config();
    char *pass = config->source_password;
    char *user = "source";
    int ret;
    int ice_login = config->ice_login;
    char *protocol;

    mount_proxy *mountinfo = config_find_mount (config, mount);

    if (mountinfo)
    {
        if (mountinfo->password)
            pass = mountinfo->password;
        if (mountinfo->username)
            user = mountinfo->username;
    }

    if(!pass) {
        WARN0("No source password set, rejecting source");
        config_release_config();
        return 0;
    }

    protocol = httpp_getvar(parser, HTTPP_VAR_PROTOCOL);
    if(protocol != NULL && !strcmp(protocol, "ICY")) {
        ret = _check_pass_icy(parser, pass);
    }
    else {
        ret = _check_pass_http(parser, user, pass);
        if(!ret && ice_login)
        {
            ret = _check_pass_ice(parser, pass);
            if(ret)
                WARN0("Source is using deprecated icecast login");
        }
    }
    config_release_config();
    return ret;
}


static void _handle_source_request (client_t *client, char *uri, int auth_style)
{
    source_t *source;

    INFO1("Source logging in at mountpoint \"%s\"", uri);

    if (uri[0] != '/')
    {
        WARN0 ("source mountpoint not starting with /");
        client_send_401 (client);
        return;
    }
    if (auth_style == ICECAST_SOURCE_AUTH) {
        if (connection_check_source_pass (client->parser, uri) == 0)
        {
            /* We commonly get this if the source client is using the wrong
             * protocol: attempt to diagnose this and return an error
             */
            /* TODO: Do what the above comment says */
            INFO1("Source (%s) attempted to login with invalid or missing password", uri);
            client_send_401(client);
            return;
        }
    }
    source = source_reserve (uri);
    if (source)
    {
        if (auth_style == SHOUTCAST_SOURCE_AUTH) {
            source->shoutcast_compat = 1;
        }
        source->client = client;
        source->parser = client->parser;
        source->con = client->con;
        if (connection_complete_source (source, 1) < 0)
        {
            source_clear_source (source);
            source_free_source (source);
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
        WARN1 ("Mountpoint %s in use", uri);
    }
}


static void _handle_stats_request (client_t *client, char *uri)
{
    stats_event_inc(NULL, "stats_connections");

    if (connection_check_admin_pass (client->parser) == 0)
    {
        client_send_401 (client);
        ERROR0("Bad password for stats connection");
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
    int fileserve;
    int port;
    int i;
    char *serverhost = NULL;
    int serverport = 0;
    aliases *alias;
    ice_config_t *config;
    char *uri = passed_uri;

    config = config_get_config();
    fileserve = config->fileserve;
    port = config->port;
    for(i = 0; i < global.server_sockets; i++) {
        if(global.serversock[i] == client->con->serversock) {
            serverhost = config->listeners[i].bind_address;
            serverport = config->listeners[i].port;
            break;
        }
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
            DEBUG2 ("alias has made %s into %s", passed_uri, uri);
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

    /* Here we are parsing the URI request to see
    ** if the extension is .xsl, if so, then process
    ** this request as an XSLT request
    */
    if (util_check_valid_extension (uri) == XSLT_CONTENT)
    {
        /* If the file exists, then transform it, otherwise, write a 404 */
        DEBUG0("Stats request, sending XSL transformed stats");
        stats_transform_xslt (client, uri);
        if (uri != passed_uri) free (uri);
        return;
    }

    add_client (uri, client);
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

    if (node->shoutcast == 1)
    {
        char *source_password, *ptr, *headers;
        mount_proxy *mountinfo = config_find_mount (config, config->shoutcast_mount);

        if (mountinfo && mountinfo->password)
            source_password = strdup (mountinfo->password);
        else
            source_password = strdup (config->source_password);
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
            free (node);
            return;
        }
        *ptr = '\0';

        if (strcmp (client->refbuf->data, source_password) == 0)
        {
            client->respcode = 200;
            /* send this non-blocking but if there is only a partial write
             * then leave to header timeout */
            sock_write (client->con->sock, "OK2\r\n");
            node->offset -= (headers - client->refbuf->data);
            memmove (client->refbuf->data, headers, node->offset+1);
            node->shoutcast = 2;
            /* we've checked the password, now send it back for reading headers */
            _add_request_queue (node);
            free (source_password);
            return;
        }
        else
            INFO1 ("password does not match \"%s\"", client->refbuf->data);
        client_destroy (client);
        free (node);
        return;
    }
    shoutcast_mount = strdup (config->shoutcast_mount);
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
        _handle_source_request (client, shoutcast_mount, SHOUTCAST_SOURCE_AUTH);
    }
    else {
        httpp_destroy (parser);
        client_destroy (client);
    }
    free (http_compliant);
    free (shoutcast_mount);
    free (node);
    return;
}


/* Connection thread. Here we take clients off the connection queue and check
 * the contents provided. We set up the parser then hand off to the specific
 * request handler.
 */
static void *_handle_connection(void *arg)
{
    http_parser_t *parser;
    char *rawuri, *uri;

    while (global.running == ICE_RUNNING) {

        client_queue_t *node = _get_connection();

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
                /* we may have more than just headers, so prepare for it */
                if (node->stream_offset == node->offset)
                    client->refbuf->len = 0;
                else
                {
                    char *ptr = client->refbuf->data;
                    client->refbuf->len = node->offset - node->stream_offset;
                    memmove (ptr, ptr + node->stream_offset, client->refbuf->len);
                }
                free (node);
                
                if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
                    strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
                    ERROR0("Bad HTTP protocol detected");
                    client_destroy (client);
                    continue;
                }

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);
                uri = util_normalise_uri(rawuri);

                if (uri == NULL)
                {
                    client_destroy (client);
                    continue;
                }

                if (parser->req_type == httpp_req_source) {
                    _handle_source_request (client, uri, ICECAST_SOURCE_AUTH);
                }
                else if (parser->req_type == httpp_req_stats) {
                    _handle_stats_request (client, uri);
                }
                else if (parser->req_type == httpp_req_get) {
                    _handle_get_request (client, uri);
                }
                else {
                    ERROR0("Wrong request type from client");
                    client_send_400 (client, "unknown request");
                }

                free(uri);
            } 
            else
            {
                free (node);
                ERROR0("HTTP request parsing failed");
                client_destroy (client);
            }
            continue;
        }
        thread_sleep (50000);
    }
    DEBUG0 ("Connection thread done");

    return NULL;
}

void connection_close(connection_t *con)
{
    sock_close(con->sock);
    if (con->ip) free(con->ip);
    if (con->host) free(con->host);
    free(con);
}
