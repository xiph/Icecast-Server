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

#include "os.h"

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

typedef struct con_queue_tag {
    connection_t *con;
    struct con_queue_tag *next;
} con_queue_t;

typedef struct _thread_queue_tag {
    thread_type *thread_id;
    struct _thread_queue_tag *next;
} thread_queue_t;

static mutex_t _connection_mutex;
static volatile unsigned long _current_id = 0;
static int _initialized = 0;

static volatile con_queue_t *_queue = NULL, **_queue_tail = &_queue;
static mutex_t _queue_mutex;

static thread_queue_t *_conhands = NULL;

rwlock_t _source_shutdown_rwlock;

static void *_handle_connection(void *arg);

void connection_initialize(void)
{
    if (_initialized) return;
    
    thread_mutex_create("connection", &_connection_mutex);
    thread_mutex_create("connection q", &_queue_mutex);
    thread_mutex_create("move_clients", &move_clients_mutex);
    thread_rwlock_create(&_source_shutdown_rwlock);
    thread_cond_create(&global.shutdown_cond);

    _initialized = 1;
}

void connection_shutdown(void)
{
    if (!_initialized) return;
    
    thread_cond_destroy(&global.shutdown_cond);
    thread_rwlock_destroy(&_source_shutdown_rwlock);
    thread_mutex_destroy(&_queue_mutex);
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

connection_t *create_connection(sock_t sock, sock_t serversock, char *ip) {
    connection_t *con;
    con = (connection_t *)malloc(sizeof(connection_t));
    memset(con, 0, sizeof(connection_t));
    con->sock = sock;
    con->serversock = serversock;
    con->con_time = time(NULL);
    con->id = _next_connection_id();
    con->ip = ip;

    con->event_number = EVENT_NO_EVENT;
    con->event = NULL;

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
    if (sock >= 0) {
        con = create_connection(sock, serversock, ip);

        return con;
    }

    if (!sock_recoverable(sock_error()))
        WARN2("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
    
    free(ip);

    return NULL;
}

static void _add_connection(connection_t *con)
{
    con_queue_t *node;

    node = (con_queue_t *)malloc(sizeof(con_queue_t));
    
    node->con = con;
    node->next = NULL;
    thread_mutex_lock(&_queue_mutex);
    *_queue_tail = node;
    _queue_tail = (volatile con_queue_t **)&node->next;
    thread_mutex_unlock(&_queue_mutex);
}

static void _push_thread(thread_queue_t **queue, thread_type *thread_id)
{
    /* create item */
    thread_queue_t *item = (thread_queue_t *)malloc(sizeof(thread_queue_t));
    item->thread_id = thread_id;
    item->next = NULL;


    thread_mutex_lock(&_queue_mutex);
    if (*queue == NULL) {
        *queue = item;
    } else {
        item->next = *queue;
        *queue = item;
    }
    thread_mutex_unlock(&_queue_mutex);
}

static thread_type *_pop_thread(thread_queue_t **queue)
{
    thread_type *id;
    thread_queue_t *item;

    thread_mutex_lock(&_queue_mutex);

    item = *queue;
    if (item == NULL) {
        thread_mutex_unlock(&_queue_mutex);
        return NULL;
    }

    *queue = item->next;
    item->next = NULL;
    id = item->thread_id;
    free(item);

    thread_mutex_unlock(&_queue_mutex);

    return id;
}

static void _build_pool(void)
{
    ice_config_t *config;
    int i;
    thread_type *tid;
    char buff[64];
    int threadpool_size;

    config = config_get_config();
    threadpool_size = config->threadpool_size;
    config_release_config();

    for (i = 0; i < threadpool_size; i++) {
        snprintf(buff, 64, "Connection Thread #%d", i);
        tid = thread_create(buff, _handle_connection, NULL, THREAD_ATTACHED);
        _push_thread(&_conhands, tid);
    }
}

static void _destroy_pool(void)
{
    thread_type *id;
    int i;

    i = 0;

    id = _pop_thread(&_conhands);
    while (id != NULL) {
        thread_join(id);
        id = _pop_thread(&_conhands);
    }
    INFO0("All connection threads down");
}

void connection_accept_loop(void)
{
    connection_t *con;

    _build_pool();

    while (global.running == ICE_RUNNING)
    {
        if (global . schedule_config_reread)
        {
            /* reread config file */
            INFO0("Scheduling config reread ...");

            connection_inject_event(EVENT_CONFIG_READ, NULL);
            global . schedule_config_reread = 0;
        }

        con = _accept_connection();

        if (con) {
            _add_connection(con);
        }
    }

    /* Give all the other threads notification to shut down */
    thread_cond_broadcast(&global.shutdown_cond);

    _destroy_pool();

    /* wait for all the sources to shutdown */
    thread_rwlock_wlock(&_source_shutdown_rwlock);
    thread_rwlock_unlock(&_source_shutdown_rwlock);
}

static connection_t *_get_connection(void)
{
    con_queue_t *node = NULL;
    connection_t *con = NULL;

    /* common case, no new connections so don't bother taking locks */
    if (_queue == NULL)
        return NULL;

    thread_mutex_lock(&_queue_mutex);
    if (_queue) {
        node = (con_queue_t *)_queue;
        _queue = node->next;
        if ((con_queue_t**)_queue_tail == &node->next)
            _queue_tail = &_queue;
    }
    thread_mutex_unlock(&_queue_mutex);

    if (node) {
        con = node->con;
        free(node);
    }

    return con;
}

void connection_inject_event(int eventnum, void *event_data) {
    connection_t *con = calloc(1, sizeof(connection_t));

    con->event_number = eventnum;
    con->event = event_data;

    _add_connection(con);
}


/* Called when activating a source. Verifies that the source count is not
 * exceeded and applies any initial parameters.
 */
int connection_complete_source (source_t *source, connection_t *con, http_parser_t *in_parser)
{
    ice_config_t *config = config_get_config();

    global_lock ();
    DEBUG1 ("sources count is %d", global.sources);

    if (global.sources < config->source_limit)
    {
        char *contenttype;
        mount_proxy *mountinfo;
        format_type_t format_type;
        http_parser_t *parser = in_parser;

        /* setup format handler */
        if (source->client)
            parser = source->client->parser;

        contenttype = httpp_getvar (parser, "content-type");
        if (contenttype != NULL)
        {
            format_type = format_get_type (contenttype);

            if (format_type == FORMAT_ERROR)
            {
                global_unlock();
                config_release_config();
                if (source->client)
                    client_send_404 (source->client, "Content-type not supported");
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

        if (format_get_plugin (format_type, source, parser) < 0)
        {
            global_unlock();
            config_release_config();
            if (source->client)
                client_send_404 (source->client, "internal format allocation problem");
            WARN1 ("plugin format failed for \"%s\"", source->mount);
            source->client = NULL;
            return -1;
        }

        global.sources++;
        global_unlock();

        /* for relays, we don't yet have a client, however we do require one
         * to retrieve the stream from.  This is created here, quite late,
         * because we can't use this client to return an error code/message,
         * so we only do this once we know we're going to accept the source.
         */
        if (source->client == NULL)
        {
            source->client = client_create (con, parser);
            if (source->client == NULL)
            {
                config_release_config();
                global_lock();
                global.sources--;
                global_unlock();
                connection_close (con);
                httpp_destroy (parser);
                return -1;
            }
        }

        source->running = 1;
        mountinfo = config_find_mount (config, source->mount);
        if (mountinfo)
            source_recheck_mounts ();
        else
            source_update_settings (config, source, mountinfo);
        config_release_config();

        source->shutdown_rwlock = &_source_shutdown_rwlock;
        DEBUG0 ("source is ready to start");

        return 0;
    }
    WARN1("Request to add source when maximum source limit "
            "reached %d", global.sources);

    global_unlock();
    config_release_config();

    if (source->client)
        client_send_404 (source->client, "too many sources connected");

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
        if (connection_complete_source (source, NULL, NULL) < 0)
        {
            source->client = NULL;
            source_free_source (source);
        }
        else
            thread_create ("Source Thread", source_client_thread,
                    source, THREAD_DETACHED);
    }
    else
    {
        client_send_404 (client, "Mountpoint in use");
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
    if (sock_write (client->con->sock, "HTTP/1.0 200 OK\r\n\r\n") < 0)
    {
        client_destroy (client);
        ERROR0 ("failed to write header");
        return;
    }

    thread_create("Stats Connection", stats_connection, (void *)client, THREAD_DETACHED);
}

static void _handle_get_request (client_t *client, char *passed_uri)
{
    int fileserve;
    char *host = NULL;
    int port;
    int i;
    char *serverhost = NULL;
    int serverport = 0;
    aliases *alias;
    ice_config_t *config;
    char *uri = passed_uri;

    DEBUG1("start with %s", passed_uri);
    config = config_get_config();
    fileserve = config->fileserve;
    if (config->hostname)
        host = strdup (config->hostname);
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
        free (host);
        return;
    }
    free (host);

    add_client (uri, client);

    if (uri != passed_uri) free (uri);
}

void _handle_shoutcast_compatible(connection_t *con, char *mount, char *source_password) {
    char shoutcast_password[256];
    char *http_compliant;
    int http_compliant_len = 0;
    char header[4096];
    http_parser_t *parser;

    memset(shoutcast_password, 0, sizeof (shoutcast_password));
    /* Step one of shoutcast auth protocol, read encoder password (1 line) */
    if (util_read_header(con->sock, shoutcast_password, 
            sizeof (shoutcast_password), 
            READ_LINE) == 0) {
        /* either we didn't get a complete line, or we timed out */
        connection_close(con);
        return;
    }
    /* Get rid of trailing \n */
    shoutcast_password[strlen(shoutcast_password)-1] = '\000';
    if (strcmp(shoutcast_password, source_password)) {
        ERROR0("Invalid source password");
        connection_close(con);
        return;
    }
    /* Step two of shoutcast auth protocol, send OK2.  For those
       interested, OK2 means it supports metadata updates via admin.cgi,
       and the string "OK" can also be sent, but will indicate to the
       shoutcast source client to not send metadata updates.
       I believe icecast 1.x used to send OK. */
    sock_write(con->sock, "%s\r\n", "OK2");

    memset(header, 0, sizeof (header));
    /* Step three of shoutcast auth protocol, read HTTP-style
       request headers and process them.*/
    if (util_read_header(con->sock, header, sizeof (header), 
                         READ_ENTIRE_HEADER) == 0) {
        /* either we didn't get a complete header, or we timed out */
        connection_close(con);
        return;
    }
    /* Here we create a valid HTTP request based of the information
       that was passed in via the non-HTTP style protocol above. This
       means we can use some of our existing code to handle this case */
    http_compliant_len = strlen(header) + strlen(mount) + 20;
    http_compliant = (char *)calloc(1, http_compliant_len);
    snprintf (http_compliant, http_compliant_len,
            "SOURCE %s HTTP/1.0\r\n%s", mount, header);
    parser = httpp_create_parser();
    httpp_initialize(parser, NULL);
    if (httpp_parse (parser, http_compliant, strlen(http_compliant)))
    {
        client_t *client = client_create (con, parser);
        if (client)
        {
            _handle_source_request (client, mount, SHOUTCAST_SOURCE_AUTH);
            free (http_compliant);
            return;
        }
    }
    connection_close (con);
    httpp_destroy (parser);
    free (http_compliant);
}

static void *_handle_connection(void *arg)
{
    char header[4096];
    connection_t *con;
    http_parser_t *parser;
    char *rawuri, *uri;
    client_t *client;
    int i = 0;
    int continue_flag = 0;
    ice_config_t *config;
    char *source_password;

    while (global.running == ICE_RUNNING) {

        /* grab a connection and set the socket to blocking */
        while ((con = _get_connection())) {

            /* Handle meta-connections */
            if(con->event_number > 0) {
                switch(con->event_number) {
                    case EVENT_CONFIG_READ:
                        event_config_read(con->event);
                        break;
                    default:
                        ERROR1("Unknown event number: %d", con->event_number);
                        break;
                }
                free(con);
                continue;
            }

            stats_event_inc(NULL, "connections");

            sock_set_blocking(con->sock, SOCK_BLOCK);

            continue_flag = 0;
            /* Check for special shoutcast compatability processing */
            for(i = 0; i < MAX_LISTEN_SOCKETS; i++) {
                if(global.serversock[i] == con->serversock) {
                    config = config_get_config();
                    if (config->listeners[i].shoutcast_compat) {
                        char *shoutcast_mount = strdup (config->shoutcast_mount);
                        mount_proxy *mountinfo = config_find_mount (config, config->shoutcast_mount);
                        if (mountinfo && mountinfo->password)
                            source_password = strdup (mountinfo->password);
                        else
                            source_password = strdup (config->source_password);
                        config_release_config();
                        _handle_shoutcast_compatible(con, shoutcast_mount, source_password);
                        free(source_password);
                        free (shoutcast_mount);
                        continue_flag = 1;
                        break;
                    }
                    config_release_config();
                }
            }
            if(continue_flag) {
                continue;
            }

            /* fill header with the http header */
            memset(header, 0, sizeof (header));
            if (util_read_header(con->sock, header, sizeof (header), 
                        READ_ENTIRE_HEADER) == 0) {
                /* either we didn't get a complete header, or we timed out */
                connection_close(con);
                continue;
            }

            parser = httpp_create_parser();
            httpp_initialize(parser, NULL);
            if (httpp_parse(parser, header, strlen(header))) {
                /* handle the connection or something */
                
                if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
                    strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
                    ERROR0("Bad HTTP protocol detected");
                    connection_close(con);
                    httpp_destroy(parser);
                    continue;
                }

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);
                uri = util_normalise_uri(rawuri);

                if (uri == NULL)
                {
                    sock_write(con->sock, "The path you requested was invalid\r\n");
                    connection_close(con);
                    httpp_destroy(parser);
                    continue;
                }
                client = client_create (con, parser);
                if (client == NULL)
                {
                    sock_write (con->sock, "HTTP/1.0 404 File Not Found\r\n"
                            "Content-Type: text/html\r\n\r\n"
                            "<b>Connection limit reached</b>");
                    connection_close(con);
                    httpp_destroy(parser);
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
                continue;
            } 
            else {
                ERROR0("HTTP request parsing failed");
                connection_close(con);
                httpp_destroy(parser);
                continue;
            }
        }
        thread_sleep (100000);
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
