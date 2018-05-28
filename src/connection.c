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
 * Copyright 2011,      Dave 'justdave' Miller <justdave@mozilla.com>,
 * Copyright 2011-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include "compat.h"

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/net/sock.h"
#include "common/httpp/httpp.h"

#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "errors.h"
#include "stats.h"
#include "logging.h"
#include "xslt.h"
#include "fserve.h"
#include "sighandler.h"

#include "yp.h"
#include "source.h"
#include "format.h"
#include "format_mp3.h"
#include "admin.h"
#include "auth.h"
#include "matchfile.h"
#include "tls.h"

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

static spin_t _connection_lock; // protects _current_id, _con_queue, _con_queue_tail
static volatile unsigned long _current_id = 0;
static int _initialized = 0;

static volatile client_queue_t *_req_queue = NULL, **_req_queue_tail = &_req_queue;
static volatile client_queue_t *_con_queue = NULL, **_con_queue_tail = &_con_queue;
static int tls_ok;
static tls_ctx_t *tls_ctx;

/* filtering client connection based on IP */
static matchfile_t *banned_ip, *allowed_ip;

rwlock_t _source_shutdown_rwlock;

static void _handle_connection(void);
static void get_tls_certificate(ice_config_t *config);

void connection_initialize(void)
{
    if (_initialized)
        return;

    thread_spin_create (&_connection_lock);
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
    if (!_initialized)
        return;

    tls_ctx_unref(tls_ctx);
    matchfile_release(banned_ip);
    matchfile_release(allowed_ip);
 
    thread_cond_destroy(&global.shutdown_cond);
    thread_rwlock_destroy(&_source_shutdown_rwlock);
    thread_spin_destroy (&_connection_lock);
    thread_mutex_destroy(&move_clients_mutex);

    _initialized = 0;
}

void connection_reread_config(struct ice_config_tag *config)
{
    get_tls_certificate(config);
}

static unsigned long _next_connection_id(void)
{
    unsigned long id;

    thread_spin_lock(&_connection_lock);
    id = _current_id++;
    thread_spin_unlock(&_connection_lock);

    return id;
}


#ifdef ICECAST_CAP_TLS
static void get_tls_certificate(ice_config_t *config)
{
    const char *keyfile;

    config->tls_ok = tls_ok = 0;

    keyfile = config->tls_context.key_file;
    if (!keyfile)
        keyfile = config->tls_context.cert_file;

    tls_ctx_unref(tls_ctx);
    tls_ctx = tls_ctx_new(config->tls_context.cert_file, keyfile, config->tls_context.cipher_list);
    if (!tls_ctx) {
        ICECAST_LOG_INFO("No TLS capability on any configured ports");
        return;
    }

    config->tls_ok = tls_ok = 1;
}


/* handlers for reading and writing a connection_t when there is TLS
 * configured on the listening port
 */
static int connection_read_tls(connection_t *con, void *buf, size_t len)
{
    ssize_t bytes = tls_read(con->tls, buf, len);

    if (bytes < 0) {
        if (tls_want_io(con->tls) > 0)
            return -1;
        con->error = 1;
    }
    return bytes;
}

static int connection_send_tls(connection_t *con, const void *buf, size_t len)
{
    ssize_t bytes = tls_write(con->tls, buf, len);

    if (bytes < 0) {
        if (tls_want_io(con->tls) > 0)
            return -1;
        con->error = 1;
    } else {
        con->sent_bytes += bytes;
    }
    return bytes;
}
#else

/* TLS not compiled in, so at least log it */
static void get_tls_certificate(ice_config_t *config)
{
    tls_ok = 0;
    ICECAST_LOG_INFO("No TLS capability. "
                     "Rebuild Icecast with OpenSSL support to enable this.");
}
#endif /* ICECAST_CAP_TLS */


/* handlers (default) for reading and writing a connection_t, no encrpytion
 * used just straight access to the socket
 */
static int connection_read(connection_t *con, void *buf, size_t len)
{
    int bytes = sock_read_bytes(con->sock, buf, len);
    if (bytes == 0)
        con->error = 1;
    if (bytes == -1 && !sock_recoverable(sock_error()))
        con->error = 1;
    return bytes;
}

static int connection_send(connection_t *con, const void *buf, size_t len)
{
    int bytes = sock_write_bytes(con->sock, buf, len);
    if (bytes < 0) {
        if (!sock_recoverable(sock_error()))
            con->error = 1;
    } else {
        con->sent_bytes += bytes;
    }
    return bytes;
}

connection_t *connection_create (sock_t sock, sock_t serversock, char *ip)
{
    connection_t *con;
    con = (connection_t *)calloc(1, sizeof(connection_t));
    if (con) {
        con->sock       = sock;
        con->serversock = serversock;
        con->con_time   = time(NULL);
        con->id         = _next_connection_id();
        con->ip         = ip;
        con->tlsmode    = ICECAST_TLSMODE_AUTO;
        con->read       = connection_read;
        con->send       = connection_send;
    }

    return con;
}

/* prepare connection for interacting over a TLS connection
 */
void connection_uses_tls(connection_t *con)
{
#ifdef ICECAST_CAP_TLS
    if (con->tls)
        return;

    con->tlsmode = ICECAST_TLSMODE_RFC2818;
    con->read = connection_read_tls;
    con->send = connection_send_tls;
    con->tls = tls_new(tls_ctx);
    tls_set_incoming(con->tls);
    tls_set_socket(con->tls, con->sock);
#endif
}

ssize_t connection_read_bytes(connection_t *con, void *buf, size_t len)
{
    return con->read(con, buf, len);
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
    } else if(ret == 0) {
        return SOCK_ERROR;
    } else {
        int dst;
        for(i=0; i < global.server_sockets; i++) {
            if(ufds[i].revents & POLLIN)
                return ufds[i].fd;
            if(ufds[i].revents & (POLLHUP|POLLERR|POLLNVAL)) {
                if (ufds[i].revents & (POLLHUP|POLLERR)) {
                    sock_close (global.serversock[i]);
                    ICECAST_LOG_WARN("Had to close a listening socket");
                }
                global.serversock[i] = SOCK_ERROR;
            }
        }
        /* remove any closed sockets */
        for(i=0, dst=0; i < global.server_sockets; i++) {
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
    } else if(ret == 0) {
        return SOCK_ERROR;
    } else {
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
    if (sock != SOCK_ERROR) {
        connection_t *con = NULL;
        /* Make any IPv4 mapped IPv6 address look like a normal IPv4 address */
        if (strncmp(ip, "::ffff:", 7) == 0)
            memmove(ip, ip+7, strlen (ip+7)+1);

        if (matchfile_match_allow_deny(allowed_ip, banned_ip, ip))
            con = connection_create (sock, serversock, ip);
        if (con)
            return con;
        sock_close(sock);
    } else {
        if (!sock_recoverable(sock_error())) {
            ICECAST_LOG_WARN("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
            thread_sleep(500000);
        }
    }
    free(ip);
    return NULL;
}


/* add client to connection queue. At this point some header information
 * has been collected, so we now pass it onto the connection thread for
 * further processing
 */
static void _add_connection(client_queue_t *node)
{
    thread_spin_lock(&_connection_lock);
    *_con_queue_tail = node;
    _con_queue_tail = (volatile client_queue_t **) &node->next;
    thread_spin_unlock(&_connection_lock);
}


/* this returns queued clients for the connection thread. headers are
 * already provided, but need to be parsed.
 */
static client_queue_t *_get_connection(void)
{
    client_queue_t *node = NULL;

    thread_spin_lock(&_connection_lock);

    if (_con_queue){
        node = (client_queue_t *)_con_queue;
        _con_queue = node->next;
        if (_con_queue == NULL)
            _con_queue_tail = &_con_queue;
        node->next = NULL;
    }

    thread_spin_unlock(&_connection_lock);
    return node;
}


/* run along queue checking for any data that has come in or a timeout */
static void process_request_queue (void)
{
    client_queue_t **node_ref = (client_queue_t **)&_req_queue;
    ice_config_t *config;
    int timeout;
    char peak;

    config = config_get_config();
    timeout = config->header_timeout;
    config_release_config();

    while (*node_ref) {
        client_queue_t *node = *node_ref;
        client_t *client = node->client;
        int len = PER_CLIENT_REFBUF_SIZE - 1 - node->offset;
        char *buf = client->refbuf->data + node->offset;

        if (client->con->tlsmode == ICECAST_TLSMODE_AUTO || client->con->tlsmode == ICECAST_TLSMODE_AUTO_NO_PLAIN) {
            if (recv(client->con->sock, &peak, 1, MSG_PEEK) == 1) {
                if (peak == 0x16) { /* TLS Record Protocol Content type 0x16 == Handshake */
                    connection_uses_tls(client->con);
                }
            }
        }

        if (len > 0) {
            if (client->con->con_time + timeout <= time(NULL)) {
                len = 0;
            } else {
                len = client_read_bytes(client, buf, len);
            }
        }

        if (len > 0) {
            int pass_it = 1;
            char *ptr;

            /* handle \n, \r\n and nsvcap which for some strange reason has
             * EOL as \r\r\n */
            node->offset += len;
            client->refbuf->data[node->offset] = '\000';
            do {
                if (node->shoutcast == 1) {
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
                ptr = strstr(client->refbuf->data, "\r\r\n\r\r\n");
                if (ptr) {
                    node->stream_offset = (ptr+6) - client->refbuf->data;
                    break;
                }
                ptr = strstr(client->refbuf->data, "\r\n\r\n");
                if (ptr) {
                    node->stream_offset = (ptr+4) - client->refbuf->data;
                    break;
                }
                ptr = strstr(client->refbuf->data, "\n\n");
                if (ptr) {
                    node->stream_offset = (ptr+2) - client->refbuf->data;
                    break;
                }
                pass_it = 0;
            } while (0);

            if (pass_it) {
                if ((client_queue_t **)_req_queue_tail == &(node->next))
                    _req_queue_tail = (volatile client_queue_t **)node_ref;
                *node_ref = node->next;
                node->next = NULL;
                _add_connection(node);
                continue;
            }
        } else {
            if (len == 0 || client->con->error) {
                if ((client_queue_t **)_req_queue_tail == &node->next)
                    _req_queue_tail = (volatile client_queue_t **)node_ref;
                *node_ref = node->next;
                client_destroy(client);
                free(node);
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
static void _add_request_queue(client_queue_t *node)
{
    *_req_queue_tail = node;
    _req_queue_tail = (volatile client_queue_t **)&node->next;
}

static client_queue_t *create_client_node(client_t *client)
{
    client_queue_t *node = calloc (1, sizeof (client_queue_t));
    ice_config_t *config;
    listener_t *listener;

    if (!node)
        return NULL;

    node->client = client;

    config = config_get_config();
    listener = config_get_listen_sock(config, client->con);

    if (listener) {
        if (listener->shoutcast_compat)
            node->shoutcast = 1;
        client->con->tlsmode = listener->tls;
        if (listener->tls == ICECAST_TLSMODE_RFC2818 && tls_ok)
            connection_uses_tls(client->con);
        if (listener->shoutcast_mount)
            node->shoutcast_mount = strdup(listener->shoutcast_mount);
    }

    config_release_config();

    return node;
}

void connection_queue(connection_t *con)
{
    client_queue_t *node;
    client_t *client = NULL;

    global_lock();
    if (client_create(&client, con, NULL) < 0) {
        global_unlock();
        client_send_error_by_id(client, ICECAST_ERROR_GEN_CLIENT_LIMIT);
        /* don't be too eager as this is an imposed hard limit */
        thread_sleep(400000);
        return;
    }

    /* setup client for reading incoming http */
    client->refbuf->data[PER_CLIENT_REFBUF_SIZE-1] = '\000';

    if (sock_set_blocking(client->con->sock, 0) || sock_set_nodelay(client->con->sock)) {
        global_unlock();
        ICECAST_LOG_WARN("Failed to set tcp options on client connection, dropping");
        client_destroy(client);
        return;
    }
    node = create_client_node(client);
    global_unlock();

    if (node == NULL) {
        client_destroy(client);
        return;
    }

    _add_request_queue(node);
    stats_event_inc(NULL, "connections");
}

void connection_accept_loop(void)
{
    connection_t *con;
    ice_config_t *config;
    int duration = 300;

    config = config_get_config();
    get_tls_certificate(config);
    config_release_config();

    while (global.running == ICECAST_RUNNING) {
        con = _accept_connection (duration);

        if (con) {
            connection_queue(con);
            duration = 5;
        } else {
            if (_req_queue == NULL)
                duration = 300; /* use longer timeouts when nothing waiting */
        }
        process_request_queue();
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
int connection_complete_source(source_t *source, int response)
{
    ice_config_t *config;

    global_lock();
    ICECAST_LOG_DEBUG("sources count is %d", global.sources);

    config = config_get_config();
    if (global.sources < config->source_limit) {
        const char *contenttype;
        mount_proxy *mountinfo;
        format_type_t format_type;

        /* setup format handler */
        contenttype = httpp_getvar (source->parser, "content-type");
        if (contenttype != NULL) {
            format_type = format_get_type(contenttype);

            if (format_type == FORMAT_ERROR) {
                config_release_config();
                global_unlock();
                if (response) {
                    client_send_error_by_id(source->client, ICECAST_ERROR_CON_CONTENT_TYPE_NOSYS);
                    source->client = NULL;
                }
                ICECAST_LOG_WARN("Content-type \"%s\" not supported, dropping source", contenttype);
                return -1;
            }
        } else if (source->parser->req_type == httpp_req_put) {
            config_release_config();
            global_unlock();
            if (response) {
                client_send_error_by_id(source->client, ICECAST_ERROR_CON_NO_CONTENT_TYPE_GIVEN);
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

        if (format_get_plugin (format_type, source) < 0) {
            global_unlock();
            config_release_config();
            if (response) {
                client_send_error_by_id(source->client, ICECAST_ERROR_CON_INTERNAL_FORMAT_ALLOC_ERROR);
                source->client = NULL;
            }
            ICECAST_LOG_WARN("plugin format failed for \"%s\"", source->mount);
            return -1;
        }

        global.sources++;
        stats_event_args(NULL, "sources", "%d", global.sources);
        global_unlock();

        source->running = 1;
        mountinfo = config_find_mount(config, source->mount, MOUNT_TYPE_NORMAL);
        source_update_settings(config, source, mountinfo);
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

    if (response) {
        client_send_error_by_id(source->client, ICECAST_ERROR_CON_SOURCE_CLIENT_LIMIT);
        source->client = NULL;
    }

    return -1;
}

static inline void source_startup(client_t *client, const char *uri)
{
    source_t *source;
    source = source_reserve(uri);

    if (source) {
        source->client = client;
        source->parser = client->parser;
        source->con = client->con;
        if (connection_complete_source(source, 1) < 0) {
            source_clear_source(source);
            source_free_source(source);
            return;
        }
        client->respcode = 200;
        if (client->protocol == ICECAST_PROTOCOL_SHOUTCAST) {
            client->respcode = 200;
            /* send this non-blocking but if there is only a partial write
             * then leave to header timeout */
            client_send_bytes(client, "OK2\r\nicy-caps:11\r\n\r\n", 20); /* TODO: Replace Magic Number! */
            source->shoutcast_compat = 1;
            source_client_callback(client, source);
        } else {
            refbuf_t *ok = refbuf_new(PER_CLIENT_REFBUF_SIZE);
            const char *expectcontinue;
            const char *transfer_encoding;
            int status_to_send = 200;
            ssize_t ret;

            transfer_encoding = httpp_getvar(source->parser, "transfer-encoding");
            if (transfer_encoding && strcasecmp(transfer_encoding, HTTPP_ENCODING_IDENTITY) != 0) {
                client->encoding = httpp_encoding_new(transfer_encoding);
                if (!client->encoding) {
                    client_send_error_by_id(client, ICECAST_ERROR_CON_UNIMPLEMENTED);
                    return;
                }
            }

            /* For PUT support we check for 100-continue and send back a 100 to stay in spec */
            expectcontinue = httpp_getvar (source->parser, "expect");

            if (expectcontinue != NULL) {
#ifdef HAVE_STRCASESTR
                if (strcasestr (expectcontinue, "100-continue") != NULL)
#else
                ICECAST_LOG_WARN("OS doesn't support case insensitive substring checks...");
                if (strstr (expectcontinue, "100-continue") != NULL)
#endif
                {
                    status_to_send = 100;
                }
            }

            client->respcode = 200;
            ret = util_http_build_header(ok->data, PER_CLIENT_REFBUF_SIZE, 0, 0, status_to_send, NULL, NULL, NULL, NULL, NULL, client);
            snprintf(ok->data + ret, PER_CLIENT_REFBUF_SIZE - ret, "Content-Length: 0\r\n\r\n");
            ok->len = strlen(ok->data);
            /* we may have unprocessed data read in, so don't overwrite it */
            ok->associated = client->refbuf;
            client->refbuf = ok;
            fserve_add_client_callback(client, source_client_callback, source);
        }
    } else {
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNT_IN_USE);
        ICECAST_LOG_WARN("Mountpoint %s in use", uri);
    }
}

/* only called for native icecast source clients */
static void _handle_source_request(client_t *client, const char *uri)
{
    ICECAST_LOG_INFO("Source logging in at mountpoint \"%s\" from %s as role %s",
        uri, client->con->ip, client->role);

    if (uri[0] != '/') {
        ICECAST_LOG_WARN("source mountpoint not starting with /");
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH);
        return;
    }

    source_startup(client, uri);
}


static void _handle_stats_request(client_t *client, char *uri)
{
    stats_event_inc(NULL, "stats_connections");

    client->respcode = 200;
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
        "HTTP/1.0 200 OK\r\n\r\n");
    client->refbuf->len = strlen(client->refbuf->data);
    fserve_add_client_callback(client, stats_callback, NULL);
}

/* if 0 is returned then the client should not be touched, however if -1
 * is returned then the caller is responsible for handling the client
 */
static int __add_listener_to_source(source_t *source, client_t *client)
{
    size_t loop = 10;

    do {
        ICECAST_LOG_DEBUG("max on %s is %ld (cur %lu)", source->mount,
            source->max_listeners, source->listeners);
        if (source->max_listeners == -1)
            break;
        if (source->listeners < (unsigned long)source->max_listeners)
            break;

        if (loop && source->fallback_when_full && source->fallback_mount) {
            source_t *next = source_find_mount (source->fallback_mount);
            if (!next) {
                ICECAST_LOG_ERROR("Fallback '%s' for full source '%s' not found",
                    source->mount, source->fallback_mount);
                return -1;
            }
            ICECAST_LOG_INFO("stream full, trying %s", next->mount);
            source = next;
            loop--;
            continue;
        }
        /* now we fail the client */
        return -1;
    } while (1);

    client->write_to_client = format_generic_write_to_client;
    client->check_buffer = format_check_http_buffer;
    client->refbuf->len = PER_CLIENT_REFBUF_SIZE;
    memset(client->refbuf->data, 0, PER_CLIENT_REFBUF_SIZE);

    /* lets add the client to the active list */
    avl_tree_wlock(source->pending_tree);
    avl_insert(source->pending_tree, client);
    avl_tree_unlock(source->pending_tree);

    if (source->running == 0 && source->on_demand) {
        /* enable on-demand relay to start, wake up the slave thread */
        ICECAST_LOG_DEBUG("kicking off on-demand relay");
        source->on_demand_req = 1;
    }
    ICECAST_LOG_DEBUG("Added client to %s", source->mount);
    return 0;
}

/* count the number of clients on a mount with same username and same role as the given one */
static inline ssize_t __count_user_role_on_mount (source_t *source, client_t *client) {
    ssize_t ret = 0;
    avl_node *node;

    avl_tree_rlock(source->client_tree);
    node = avl_get_first(source->client_tree);
    while (node) {
        client_t *existing_client = (client_t *)node->key;
        if (existing_client->username && client->username &&
            strcmp(existing_client->username, client->username) == 0 &&
            existing_client->role && client->role &&
            strcmp(existing_client->role, client->role) == 0) {
            ret++;
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->client_tree);

    avl_tree_rlock(source->pending_tree);
    node = avl_get_first(source->pending_tree);
    while (node) {
        client_t *existing_client = (client_t *)node->key;
        if (existing_client->username && client->username &&
            strcmp(existing_client->username, client->username) == 0 &&
            existing_client->role && client->role &&
            strcmp(existing_client->role, client->role) == 0){
            ret++;
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->pending_tree);
    return ret;
}

static void _handle_get_request(client_t *client, char *uri) {
    source_t *source = NULL;

    ICECAST_LOG_DEBUG("Got client %p with URI %H", client, uri);

    /* there are several types of HTTP GET clients
     * media clients, which are looking for a source (eg, URI = /stream.ogg),
     * stats clients, which are looking for /admin/stats.xml and
     * fserve clients, which are looking for static files.
     */

    stats_event_inc(NULL, "client_connections");

    /* this is a web/ request. let's check if we are allowed to do that. */
    if (acl_test_web(client->acl) != ACL_POLICY_ALLOW) {
        /* doesn't seem so, sad client :( */
        if (client->protocol == ICECAST_PROTOCOL_SHOUTCAST) {
            client_destroy(client);
        } else {
            client_send_error_by_id(client, ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE);
        }
        return;
    }

    if (util_check_valid_extension(uri) == XSLT_CONTENT) {
        /* If the file exists, then transform it, otherwise, write a 404 */
        ICECAST_LOG_DEBUG("Stats request, sending XSL transformed stats");
        stats_transform_xslt(client, uri);
        return;
    }

    avl_tree_rlock(global.source_tree);
    /* let's see if this is a source or just a random fserve file */
    source = source_find_mount(uri);
    if (source) {
        /* true mount */
        int in_error = 0;
        ssize_t max_connections_per_user = acl_get_max_connections_per_user(client->acl);
        /* check for duplicate_logins */
        if (max_connections_per_user > 0) { /* -1 = not set (-> default=unlimited), 0 = unlimited */
            if (max_connections_per_user <= __count_user_role_on_mount(source, client)) {
                client_send_error_by_id(client, ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT);
                in_error = 1;
            }
        }


        /* Set max listening duration in case not already set. */
        if (!in_error && client->con->discon_time == 0) {
            time_t connection_duration = acl_get_max_connection_duration(client->acl);
            if (connection_duration == -1) {
                ice_config_t *config = config_get_config();
                mount_proxy *mount = config_find_mount(config, source->mount, MOUNT_TYPE_NORMAL);
                if (mount && mount->max_listener_duration)
                    connection_duration = mount->max_listener_duration;
                config_release_config();
            }

            if (connection_duration > 0) /* -1 = not set (-> default=unlimited), 0 = unlimited */
                client->con->discon_time = connection_duration + time(NULL);
        }
        if (!in_error && __add_listener_to_source(source, client) == -1) {
            client_send_error_by_id(client, ICECAST_ERROR_CON_rejecting_client_for_whatever_reason);
        }
        avl_tree_unlock(global.source_tree);
    } else {
        /* file */
        avl_tree_unlock(global.source_tree);
        fserve_client_create(client, uri);
    }
}

static void _handle_shoutcast_compatible(client_queue_t *node)
{
    char *http_compliant;
    int http_compliant_len = 0;
    http_parser_t *parser;
    const char *shoutcast_mount;
    client_t *client = node->client;
    ice_config_t *config;

    if (node->shoutcast == 1)
    {
        char *ptr, *headers;

        /* Get rid of trailing \r\n or \n after password */
        ptr = strstr(client->refbuf->data, "\r\r\n");
        if (ptr) {
            headers = ptr+3;
        } else {
            ptr = strstr(client->refbuf->data, "\r\n");
            if (ptr) {
                headers = ptr+2;
            } else {
                ptr = strstr(client->refbuf->data, "\n");
                if (ptr)
                    headers = ptr+1;
            }
        }

        if (ptr == NULL){
            client_destroy(client);
            free(node->shoutcast_mount);
            free(node);
            return;
        }
        *ptr = '\0';

        client->password = strdup(client->refbuf->data);
        config = config_get_config();
        client->username = strdup(config->shoutcast_user);
        config_release_config();
        node->offset -= (headers - client->refbuf->data);
        memmove(client->refbuf->data, headers, node->offset+1);
        node->shoutcast = 2;
        /* we've checked the password, now send it back for reading headers */
        _add_request_queue(node);
        return;
    }
    /* actually make a copy as we are dropping the config lock */
    /* Here we create a valid HTTP request based of the information
       that was passed in via the non-HTTP style protocol above. This
       means we can use some of our existing code to handle this case */
    config = config_get_config();
    if (node->shoutcast_mount) {
        shoutcast_mount = node->shoutcast_mount;
    } else {
        shoutcast_mount = config->shoutcast_mount;
    }
    http_compliant_len = 20 + strlen(shoutcast_mount) + node->offset;
    http_compliant = (char *)calloc(1, http_compliant_len);
    snprintf(http_compliant, http_compliant_len,
            "SOURCE %s HTTP/1.0\r\n%s", shoutcast_mount, client->refbuf->data);
    config_release_config();

    parser = httpp_create_parser();
    httpp_initialize(parser, NULL);
    if (httpp_parse(parser, http_compliant, strlen(http_compliant))) {
        /* we may have more than just headers, so prepare for it */
        if (node->stream_offset == node->offset) {
            client->refbuf->len = 0;
        } else {
            char *ptr = client->refbuf->data;
            client->refbuf->len = node->offset - node->stream_offset;
            memmove(ptr, ptr + node->stream_offset, client->refbuf->len);
        }
        client->parser = parser;
        client->protocol = ICECAST_PROTOCOL_SHOUTCAST;
        node->shoutcast = 0;
        return;
    } else {
        httpp_destroy(parser);
        client_destroy(client);
    }
    free(http_compliant);
    free(node->shoutcast_mount);
    free(node);
    return;
}

/* Handle <resource> lookups here.
 */

static int _handle_resources(client_t *client, char **uri)
{
    const char *http_host = httpp_getvar(client->parser, "host");
    char *serverhost = NULL;
    int   serverport = 0;
    char *vhost = NULL;
    char *vhost_colon;
    char *new_uri = NULL;
    ice_config_t *config;
    listener_t *listen_sock;
    resource_t *resource;

    if (http_host) {
        vhost = strdup(http_host);
        if (vhost) {
            vhost_colon = strstr(vhost, ":");
            if (vhost_colon)
                *vhost_colon = 0;
        }
    }

    config = config_get_config();
    listen_sock = config_get_listen_sock (config, client->con);
    if (listen_sock) {
        serverhost = listen_sock->bind_address;
        serverport = listen_sock->port;
    }

    resource = config->resources;

    /* We now go thru all resources and see if any matches. */
    for (; resource; resource = resource->next) {
        /* We check for several aspects, if they DO NOT match, we continue with our search. */

        /* Check for the URI to match. */
        if (resource->flags & ALIAS_FLAG_PREFIXMATCH) {
            size_t len = strlen(resource->source);
            if (strncmp(*uri, resource->source, len) != 0)
                continue;
            ICECAST_LOG_DEBUG("Match: *uri='%s', resource->source='%s', len=%zu", *uri, resource->source, len);
        } else {
            if (strcmp(*uri, resource->source) != 0)
                continue;
        }

        /* Check for the server's port to match. */
        if (resource->port != -1 && resource->port != serverport)
            continue;

        /* Check for the server's bind address to match. */
        if (resource->bind_address != NULL && serverhost != NULL && strcmp(resource->bind_address, serverhost) != 0)
            continue;

        /* Check for the vhost to match. */
        if (resource->vhost != NULL && vhost != NULL && strcmp(resource->vhost, vhost) != 0)
            continue;

        /* Ok, we found a matching entry. */

        if (resource->destination) {
            if (resource->flags & ALIAS_FLAG_PREFIXMATCH) {
                size_t len = strlen(resource->source);
                asprintf(&new_uri, "%s%s", resource->destination, (*uri) + len);
            } else {
                new_uri = strdup(resource->destination);
            }
        }
        if (resource->omode != OMODE_DEFAULT)
            client->mode = resource->omode;
        ICECAST_LOG_DEBUG("resource has made %s into %s", *uri, new_uri);
        break;
    }

    config_release_config();

    if (new_uri) {
        free(*uri);
        *uri = new_uri;
    }

    if (vhost)
        free(vhost);

    return 0;
}

static void _handle_admin_request(client_t *client, char *adminuri)
{
    ICECAST_LOG_DEBUG("Client %p requesting admin interface.", client);

    stats_event_inc(NULL, "client_connections");

    switch (client->parser->req_type) {
        case httpp_req_get:
            admin_handle_request(client, adminuri);
        break;
        default:
            ICECAST_LOG_ERROR("Wrong request type from client");
            client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
        break;
    }
}

/* Handle any client that passed the authing process.
 */
static void _handle_authed_client(client_t *client, void *uri, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_OK) {
        client_send_error_by_id(client, ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE);
        free(uri);
        return;
    }

    if (acl_test_method(client->acl, client->parser->req_type) != ACL_POLICY_ALLOW) {
        ICECAST_LOG_ERROR("Client (role=%s, username=%s) not allowed to use this request method on %H", client->role, client->username, uri);
        client_send_error_by_id(client, ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE);
        free(uri);
        return;
    }

    /* Dispatch legacy admin.cgi requests */
    if (strcmp(uri, "/admin.cgi") == 0) {
        _handle_admin_request(client, uri + 1);
        free(uri);
        return;
    } /* Dispatch all admin requests */
    else if (strncmp(uri, "/admin/", 7) == 0) {
        _handle_admin_request(client, uri + 7);
        free(uri);
        return;
    }

    switch (client->parser->req_type) {
        case httpp_req_source:
        case httpp_req_put:
            _handle_source_request(client, uri);
        break;
        case httpp_req_stats:
            _handle_stats_request(client, uri);
        break;
        case httpp_req_get:
            _handle_get_request(client, uri);
        break;
        default:
            ICECAST_LOG_ERROR("Wrong request type from client");
            client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
        break;
    }

    free(uri);
}

/* Handle clients that still need to authenticate.
 */

static void _handle_authentication_global(client_t *client, void *uri, auth_result result)
{
    ice_config_t *config;

    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        !(result == AUTH_OK && client->admin_command != -1 && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, uri, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying global authenticators for client %p.", client);
    config = config_get_config();
    auth_stack_add_client(config->authstack, client, _handle_authed_client, uri);
    config_release_config();
}

static inline mount_proxy * __find_non_admin_mount(ice_config_t *config, const char *name, mount_type type)
{
    if (strcmp(name, "/admin.cgi") == 0 || strncmp(name, "/admin/", 7) == 0)
        return NULL;

    return config_find_mount(config, name, type);
}

static void _handle_authentication_mount_generic(client_t *client, void *uri, mount_type type, void (*callback)(client_t*, void*, auth_result))
{
    ice_config_t *config;
    mount_proxy *mountproxy;
    auth_stack_t *stack = NULL;

    config = config_get_config();
    mountproxy = __find_non_admin_mount(config, uri, type);
    if (!mountproxy) {
        int command_type = admin_get_command_type(client->admin_command);
        if (command_type == ADMINTYPE_MOUNT || command_type == ADMINTYPE_HYBRID) {
            const char *mount = httpp_get_query_param(client->parser, "mount");
            if (mount)
                mountproxy = __find_non_admin_mount(config, mount, type);
        }
    }
    if (mountproxy && mountproxy->mounttype == type)
        stack = mountproxy->authstack;
    auth_stack_addref(stack);
    config_release_config();

    if (stack) {
        auth_stack_add_client(stack, client, callback, uri);
        auth_stack_release(stack);
    } else {
        callback(client, uri, AUTH_NOMATCH);
    }
}

static void _handle_authentication_mount_default(client_t *client, void *uri, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        !(result == AUTH_OK && client->admin_command != -1 && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, uri, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying <mount type=\"default\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, uri, MOUNT_TYPE_DEFAULT, _handle_authentication_global);
}

static void _handle_authentication_mount_normal(client_t *client, char *uri)
{
    ICECAST_LOG_DEBUG("Trying <mount type=\"normal\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, uri, MOUNT_TYPE_NORMAL, _handle_authentication_mount_default);
}

static void _handle_authentication(client_t *client, char *uri)
{
    _handle_authentication_mount_normal(client, uri);
}

static void __prepare_shoutcast_admin_cgi_request(client_t *client)
{
    ice_config_t *config;
    const char *sc_mount;
    const char *pass = httpp_get_query_param(client->parser, "pass");
    listener_t *listener;

    if (pass == NULL) {
        ICECAST_LOG_ERROR("missing pass parameter");
        return;
    }

    if (client->password) {
        ICECAST_LOG_INFO("Client already has password set");
        return;
    }

    global_lock();
    config = config_get_config();
    sc_mount = config->shoutcast_mount;
    listener = config_get_listen_sock(config, client->con);

    if (listener && listener->shoutcast_mount)
        sc_mount = listener->shoutcast_mount;

    httpp_set_query_param(client->parser, "mount", sc_mount);
    httpp_setvar(client->parser, HTTPP_VAR_PROTOCOL, "ICY");
    client->password = strdup(pass);
    config_release_config();
    global_unlock();
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

    while (1) {
        node = _get_connection();
        if (node) {
            client_t *client = node->client;
            int already_parsed = 0;

            /* Check for special shoutcast compatability processing */
            if (node->shoutcast) {
                _handle_shoutcast_compatible (node);
                if (node->shoutcast)
                    continue;
            }

            /* process normal HTTP headers */
            if (client->parser) {
                already_parsed = 1;
                parser = client->parser;
            } else {
                parser = httpp_create_parser();
                httpp_initialize(parser, NULL);
                client->parser = parser;
            }
            if (already_parsed || httpp_parse (parser, client->refbuf->data, node->offset)) {
                char *uri;
                const char *upgrade, *connection;

                /* we may have more than just headers, so prepare for it */
                if (node->stream_offset == node->offset) {
                    client->refbuf->len = 0;
                } else {
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

                upgrade = httpp_getvar(parser, "upgrade");
                connection = httpp_getvar(parser, "connection");
                if (upgrade && connection && strcasecmp(connection, "upgrade") == 0) {
                    if (client->con->tlsmode == ICECAST_TLSMODE_DISABLED || strstr(upgrade, "TLS/1.0") == NULL) {
                        client_send_error_by_id(client, ICECAST_ERROR_CON_UPGRADE_ERROR);
                        continue;
                    } else {
                        client_send_101(client, ICECAST_REUSE_UPGRADETLS);
                        continue;
                    }
                } else if (client->con->tlsmode != ICECAST_TLSMODE_DISABLED && client->con->tlsmode != ICECAST_TLSMODE_AUTO && !client->con->tls) {
                    client_send_426(client, ICECAST_REUSE_UPGRADETLS);
                    continue;
                }

                uri = util_normalise_uri(rawuri);

                if (!uri) {
                    client_destroy (client);
                    continue;
                }

                client->mode = config_str_to_omode(httpp_get_query_param(client->parser, "omode"));

                if (_handle_resources(client, &uri) != 0) {
                    client_destroy (client);
                    continue;
                }

                if (strcmp(uri, "/admin.cgi") == 0) {
                    client->admin_command = admin_get_command(uri + 1);
                    __prepare_shoutcast_admin_cgi_request(client);
                    if (!client->password) {
                        client_send_error_by_id(client, ICECAST_ERROR_CON_MISSING_PASS_PARAMETER);
                        continue;
                    }
                } else if (strncmp("/admin/", uri, 7) == 0) {
                    client->admin_command = admin_get_command(uri + 7);
                }

                _handle_authentication(client, uri);
            } else {
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

    global_lock();
    if (global.serversock) {
        for (; count < global.server_sockets; count++)
            sock_close (global.serversock [count]);
        free (global.serversock);
        global.serversock = NULL;
    }
    if (config == NULL) {
        global_unlock();
        return 0;
    }

    /* setup the banned/allowed IP filenames from the xml */
    if (config->banfile) {
        matchfile_release(banned_ip);
        banned_ip = matchfile_new(config->banfile);
        if (!banned_ip)
            ICECAST_LOG_ERROR("Can not create ban object, bad!");
    }

    if (config->allowfile) {
        matchfile_release(allowed_ip);
        allowed_ip = matchfile_new(config->allowfile);
    }

    count = 0;
    global.serversock = calloc(config->listen_sock_count, sizeof(sock_t));

    listener = config->listen_sock;
    prev = &config->listen_sock;
    while (listener) {
        int successful = 0;

        do {
            sock_t sock = sock_get_server_socket (listener->port, listener->bind_address);
            if (sock == SOCK_ERROR)
                break;
            if (sock_listen (sock, ICECAST_LISTEN_QUEUE) == SOCK_ERROR) {
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
        if (successful == 0) {
            if (listener->bind_address) {
                ICECAST_LOG_ERROR("Could not create listener socket on port %d bind %s",
                        listener->port, listener->bind_address);
            } else {
                ICECAST_LOG_ERROR("Could not create listener socket on port %d", listener->port);
            }
            /* remove failed connection */
            *prev = config_clear_listener (listener);
            listener = *prev;
            continue;
        }
        if (listener->bind_address) {
            ICECAST_LOG_INFO("listener socket on port %d address %s", listener->port, listener->bind_address);
        } else {
            ICECAST_LOG_INFO("listener socket on port %d", listener->port);
        }
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
    if (!con)
        return;

    tls_unref(con->tls);
    if (con->sock != -1) /* TODO: do not use magic */
        sock_close(con->sock);
    if (con->ip)
        free(con->ip);
    free(con);
}
