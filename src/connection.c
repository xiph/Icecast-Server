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
 * Copyright 2011-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_POLL
#include <poll.h>
#endif
#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/net/sock.h"
#include "common/httpp/httpp.h"

#include "compat.h"
#include "connection.h"
#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "refobject.h"
#include "refbuf.h"
#include "client.h"
#include "errors.h"
#include "stats.h"
#include "logging.h"
#include "fserve.h"
#include "slave.h"

#include "source.h"
#include "admin.h"
#include "auth.h"
#include "matchfile.h"
#include "tls.h"
#include "acl.h"
#include "refobject.h"
#include "listensocket.h"
#include "fastevent.h"
#include "navigation.h"

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
    int shoutcast;
    char *shoutcast_mount;
    char *bodybuffer;
    size_t bodybufferlen;
    int tried_body;
    struct client_queue_tag *next;
} client_queue_t;

static spin_t _connection_lock; // protects _current_id, _con_queue, _con_queue_tail
static volatile connection_id_t _current_id = 0;
static int _initialized = 0;

static volatile client_queue_t *_req_queue = NULL, **_req_queue_tail = &_req_queue;
static volatile client_queue_t *_con_queue = NULL, **_con_queue_tail = &_con_queue;
static volatile client_queue_t *_body_queue = NULL, **_body_queue_tail = &_body_queue;
static bool tls_ok = false;
static tls_ctx_t *tls_ctx;

/* filtering client connection based on IP */
static matchfile_t *banned_ip, *allowed_ip;

rwlock_t _source_shutdown_rwlock;

static int  _update_admin_command(client_t *client);
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
    _body_queue = NULL;
    _body_queue_tail = &_body_queue;

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

void connection_reread_config(ice_config_t *config)
{
    get_tls_certificate(config);
    listensocket_container_configure_and_setup(global.listensockets, config);
}

static connection_id_t _next_connection_id(void)
{
    connection_id_t id;

    thread_spin_lock(&_connection_lock);
    id = _current_id++;
    thread_spin_unlock(&_connection_lock);

    return id;
}


#ifdef ICECAST_CAP_TLS
static void get_tls_certificate(ice_config_t *config)
{
    const char *keyfile;

    tls_ok = false;

    keyfile = config->tls_context.key_file;
    if (!keyfile)
        keyfile = config->tls_context.cert_file;

    tls_ctx_unref(tls_ctx);
    tls_ctx = tls_ctx_new(config->tls_context.cert_file, keyfile, config->tls_context.cipher_list);
    if (!tls_ctx) {
        ICECAST_LOG_INFO("No TLS capability on any configured ports");
        return;
    }

    tls_ok = true;
}


/* handlers for reading and writing a connection_t when there is TLS
 * configured on the listening port
 */
static int connection_read_tls(connection_t *con, void *buf, size_t len)
{
    ssize_t bytes = tls_read(con->tls, buf, len);

    if (bytes <= 0) {
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
    tls_ok = false;
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

connection_t *connection_create(sock_t sock, listensocket_t *listensocket_real, listensocket_t* listensocket_effective, char *ip)
{
    connection_t *con;

    if (!matchfile_match_allow_deny(allowed_ip, banned_ip, ip))
        return NULL;

    con = (connection_t *)calloc(1, sizeof(connection_t));
    if (con) {
        refobject_ref(listensocket_real);
        refobject_ref(listensocket_effective);

        con->sock       = sock;
        con->listensocket_real = listensocket_real;
        con->listensocket_effective = listensocket_effective;
        con->con_time   = time(NULL);
        con->id         = _next_connection_id();
        con->ip         = ip;
        con->tlsmode    = ICECAST_TLSMODE_AUTO;
        con->read       = connection_read;
        con->send       = connection_send;
    }

    fastevent_emit(FASTEVENT_TYPE_CONNECTION_CREATE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CONNECTION, con);

    return con;
}

/* prepare connection for interacting over a TLS connection
 */
void connection_uses_tls(connection_t *con)
{
#ifdef ICECAST_CAP_TLS
    if (con->tls)
        return;

    if (con->readbufferlen) {
        ICECAST_LOG_ERROR("Connection is now using TLS but has data put back. BAD. Discarding putback data.");
        free(con->readbuffer);
        con->readbufferlen = 0;
    }

    con->tlsmode = ICECAST_TLSMODE_RFC2818;
    con->read = connection_read_tls;
    con->send = connection_send_tls;
    con->tls = tls_new(tls_ctx);
    tls_set_incoming(con->tls);
    tls_set_socket(con->tls, con->sock);
#endif
}

ssize_t connection_send_bytes(connection_t *con, const void *buf, size_t len)
{
    ssize_t ret = con->send(con, buf, len);

    fastevent_emit(FASTEVENT_TYPE_CONNECTION_WRITE, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_OBRD, con, buf, len, ret);

    return ret;
}

static inline ssize_t connection_read_bytes_real(connection_t *con, void *buf, size_t len)
{
    ssize_t done = 0;
    ssize_t ret;

    if (con->readbufferlen) {
        ICECAST_LOG_DEBUG("On connection %p we read from putback buffer, filled with %zu bytes, requested are %zu bytes", con, con->readbufferlen, len);
        if (len >= con->readbufferlen) {
            memcpy(buf, con->readbuffer, con->readbufferlen);
            free(con->readbuffer);
            con->readbuffer = NULL;
            ICECAST_LOG_DEBUG("New fill in buffer=<empty>");
            if (len == con->readbufferlen) {
                con->readbufferlen = 0;
                return len;
            } else {
                len -= con->readbufferlen;
                buf += con->readbufferlen;
                done = con->readbufferlen;
                con->readbufferlen = 0;
            }
        } else {
            memcpy(buf, con->readbuffer, len);
            memmove(con->readbuffer, con->readbuffer+len, con->readbufferlen-len);
            con->readbufferlen -= len;
            return len;
        }
    }

    ret = con->read(con, buf, len);

    if (ret < 0) {
        if (done == 0) {
            return ret;
        } else {
            return done;
        }
    }

    return done + ret;
}

ssize_t connection_read_bytes(connection_t *con, void *buf, size_t len)
{
    ssize_t ret = connection_read_bytes_real(con, buf, len);

    fastevent_emit(FASTEVENT_TYPE_CONNECTION_READ, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_OBRD, con, buf, len, ret);

    return ret;
}

int connection_read_put_back(connection_t *con, const void *buf, size_t len)
{
    void *n;

    fastevent_emit(FASTEVENT_TYPE_CONNECTION_PUTBACK, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_OBR, con, buf, len);

    if (con->readbufferlen) {
        n = realloc(con->readbuffer, con->readbufferlen + len);
        if (!n)
            return -1;

        memcpy(n + con->readbufferlen, buf, len);
        con->readbuffer = n;
        con->readbufferlen += len;

        ICECAST_LOG_DEBUG("On connection %p %zu bytes have been put back.", con, len);
        return 0;
    } else {
        n = malloc(len);
        if (!n)
            return -1;

        memcpy(n, buf, len);
        con->readbuffer = n;
        con->readbufferlen = len;
        ICECAST_LOG_DEBUG("On connection %p %zu bytes have been put back.", con, len);
        return 0;
    }
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

        ICECAST_LOG_DDEBUG("Checking on client %p", client);

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

        if (len > 0 || node->shoutcast > 1) {
            ssize_t stream_offset = -1;
            int pass_it = 1;
            char *ptr;

            if (len < 0 && node->shoutcast > 1)
                len = 0;

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
                    stream_offset = (ptr+6) - client->refbuf->data;
                    break;
                }
                ptr = strstr(client->refbuf->data, "\r\n\r\n");
                if (ptr) {
                    stream_offset = (ptr+4) - client->refbuf->data;
                    break;
                }
                ptr = strstr(client->refbuf->data, "\n\n");
                if (ptr) {
                    stream_offset = (ptr+2) - client->refbuf->data;
                    break;
                }
                pass_it = 0;
            } while (0);

            ICECAST_LOG_DDEBUG("pass_it=%i, len=%i", pass_it, (int)len);
            ICECAST_LOG_DDEBUG("Client %p has buffer: %H", client, client->refbuf->data);

            if (pass_it) {
                if (stream_offset != -1) {
                    connection_read_put_back(client->con, client->refbuf->data + stream_offset, node->offset - stream_offset);
                    node->offset = stream_offset;
                }
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

/* add client to body queue.
 */
static void _add_body_client(client_queue_t *node)
{
    ICECAST_LOG_DEBUG("Putting client %p in body queue.", node->client);

    thread_spin_lock(&_connection_lock);
    *_body_queue_tail = node;
    _body_queue_tail = (volatile client_queue_t **) &node->next;
    thread_spin_unlock(&_connection_lock);
}

static client_slurp_result_t process_request_body_queue_one(client_queue_t *node, time_t timeout, size_t body_size_limit)
{
        client_t *client = node->client;
        client_slurp_result_t res;

        if (client->parser->req_type == httpp_req_post) {
            if (node->bodybuffer == NULL && client->request_body_read == 0) {
                if (client->request_body_length < 0) {
                    node->bodybufferlen = body_size_limit;
                    node->bodybuffer = malloc(node->bodybufferlen);
                } else if (client->request_body_length <= (ssize_t)body_size_limit) {
                    node->bodybufferlen = client->request_body_length;
                    node->bodybuffer = malloc(node->bodybufferlen);
                }
            }
        }

        if (node->bodybuffer) {
            res = client_body_slurp(client, node->bodybuffer, &(node->bodybufferlen));
            if (res == CLIENT_SLURP_SUCCESS) {
                httpp_parse_postdata(client->parser, node->bodybuffer, node->bodybufferlen);
                free(node->bodybuffer);
                node->bodybuffer = NULL;
            }
        } else {
            res = client_body_skip(client);
        }

        if (res != CLIENT_SLURP_SUCCESS) {
            if (client->con->con_time <= timeout || client->request_body_read >= body_size_limit) {
                return CLIENT_SLURP_ERROR;
            }
        }

        return res;
}

/* This queue reads data from the body of clients. */
static void process_request_body_queue (void)
{
    client_queue_t **node_ref = (client_queue_t **)&_body_queue;
    ice_config_t *config;
    time_t timeout;
    size_t body_size_limit;

    ICECAST_LOG_DDEBUG("Processing body queue.");

    ICECAST_LOG_DDEBUG("_body_queue=%p, &_body_queue=%p, _body_queue_tail=%p", _body_queue, &_body_queue, _body_queue_tail);

    config = config_get_config();
    timeout = time(NULL) - config->body_timeout;
    body_size_limit = config->body_size_limit;
    config_release_config();

    while (*node_ref) {
        client_queue_t *node = *node_ref;
        client_t *client = node->client;
        client_slurp_result_t res;

        node->tried_body = 1;

        ICECAST_LOG_DEBUG("Got client %p in body queue.", client);

        res = process_request_body_queue_one(node, timeout, body_size_limit);

        if (res != CLIENT_SLURP_NEEDS_MORE_DATA) {
            ICECAST_LOG_DEBUG("Putting client %p back in connection queue.", client);

            if ((client_queue_t **)_body_queue_tail == &(node->next))
                _body_queue_tail = (volatile client_queue_t **)node_ref;
            *node_ref = node->next;
            node->next = NULL;
            _add_connection(node);
            continue;
        }
        node_ref = &node->next;
    }
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
    const listener_t *listener;

    if (!node)
        return NULL;

    node->client = client;

    listener = listensocket_get_listener(client->con->listensocket_effective);

    if (listener) {
        if (listener->shoutcast_compat)
            node->shoutcast = 1;
        client->con->tlsmode = listener->tls;
        if (listener->tls == ICECAST_TLSMODE_RFC2818 && tls_ok)
            connection_uses_tls(client->con);
        if (listener->shoutcast_mount)
            node->shoutcast_mount = strdup(listener->shoutcast_mount);
    }

    listensocket_release_listener(client->con->listensocket_effective);

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
        con = listensocket_container_accept(global.listensockets, duration);

        if (con) {
            connection_queue(con);
            duration = 5;
        } else {
            if (_req_queue == NULL)
                duration = 300; /* use longer timeouts when nothing waiting */
        }
        process_request_queue();
        process_request_body_queue();
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

static inline void source_startup(client_t *client)
{
    source_t *source;
    source = source_reserve(client->uri);

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
            int status_to_send = 0;
            ssize_t ret;

            transfer_encoding = httpp_getvar(source->parser, "transfer-encoding");
            if (transfer_encoding && strcasecmp(transfer_encoding, HTTPP_ENCODING_IDENTITY) != 0) {
                client->encoding = httpp_encoding_new(transfer_encoding);
                if (!client->encoding) {
                    client_send_error_by_id(client, ICECAST_ERROR_CON_UNIMPLEMENTED);
                    return;
                }
            }

            if (source->parser && source->parser->req_type == httpp_req_source) {
                status_to_send = 200;
            } else {
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
            }

            client->respcode = 200;
            if (status_to_send) {
                ret = util_http_build_header(ok->data, PER_CLIENT_REFBUF_SIZE, 0, 0, status_to_send, NULL, NULL, NULL, NULL, NULL, client);
                snprintf(ok->data + ret, PER_CLIENT_REFBUF_SIZE - ret, "Content-Length: 0\r\n\r\n");
                ok->len = strlen(ok->data);
            } else {
                ok->len = 0;
            }
            refbuf_release(client->refbuf);
            client->refbuf = ok;
            fserve_add_client_callback(client, source_client_callback, source);
        }
    } else {
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNT_IN_USE);
        ICECAST_LOG_WARN("Mountpoint %s in use", client->uri);
    }
}

/* only called for native icecast source clients */
static void _handle_source_request(client_t *client)
{
    const char *method = httpp_getvar(client->parser, HTTPP_VAR_REQ_TYPE);

    ICECAST_LOG_INFO("Source logging in at mountpoint \"%s\" using %s%H%s from %s as role %s with acl %s",
        client->uri,
        ((method) ? "\"" : "<"), ((method) ? method : "unknown"), ((method) ? "\"" : ">"),
        client->con->ip, client->role, acl_get_name(client->acl));

    if (client->parser && client->parser->req_type == httpp_req_source) {
        ICECAST_LOG_DEBUG("Source at mountpoint \"%s\" connected using deprecated SOURCE method.", client->uri);
    }

    if (client->uri[0] != '/') {
        ICECAST_LOG_WARN("source mountpoint not starting with /");
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH);
        return;
    }

    source_startup(client);
}


static void _handle_stats_request(client_t *client)
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
static void __add_listener_to_source(source_t *source, client_t *client)
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
                client_send_error_by_id(client, ICECAST_ERROR_SOURCE_MAX_LISTENERS);
                return;
            }
            ICECAST_LOG_INFO("stream full, trying %s", next->mount);
            source = next;
            navigation_history_navigate_to(&(client->history), source->identifier, NAVIGATION_DIRECTION_DOWN);
            loop--;
            continue;
        }
        /* now we fail the client */
        client_send_error_by_id(client, ICECAST_ERROR_SOURCE_MAX_LISTENERS);
        return;
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

static void _handle_get_request(client_t *client) {
    source_t *source = NULL;

    ICECAST_LOG_DEBUG("Got client %p with URI %H", client, client->uri);

    /* there are several types of HTTP GET clients
     * media clients, which are looking for a source (eg, URI = /stream.ogg),
     * stats clients, which are looking for /admin/stats.xml and
     * fserve clients, which are looking for static files.
     */

    stats_event_inc(NULL, "client_connections");

    /* this is a web/ request. let's check if we are allowed to do that. */
    if (acl_test_web(client->acl) != ACL_POLICY_ALLOW) {
        /* doesn't seem so, sad client :( */
        auth_reject_client_on_deny(client);
        return;
    }

    if (client->parser->req_type == httpp_req_options) {
        client_send_204(client);
        return;
    }

    if (util_check_valid_extension(client->uri) == XSLT_CONTENT) {
        /* If the file exists, then transform it, otherwise, write a 404 */
        ICECAST_LOG_DEBUG("Stats request, sending XSL transformed stats");
        stats_transform_xslt(client);
        return;
    }

    avl_tree_rlock(global.source_tree);
    /* let's see if this is a source or just a random fserve file */
    source = source_find_mount_with_history(client->uri, &(client->history));
    if (source) {
        /* true mount */
        do {
            ssize_t max_connections_per_user = acl_get_max_connections_per_user(client->acl);
            /* check for duplicate_logins */
            if (max_connections_per_user > 0) { /* -1 = not set (-> default=unlimited), 0 = unlimited */
                if (max_connections_per_user <= __count_user_role_on_mount(source, client)) {
                    client_send_error_by_id(client, ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT);
                    break;
                }
            }

            if (!source->allow_direct_access) {
                client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNT_NO_FOR_DIRECT_ACCESS);
                break;
            }

            /* Set max listening duration in case not already set. */
            if (client->con->discon_time == 0) {
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

            __add_listener_to_source(source, client);
        } while (0);
        avl_tree_unlock(global.source_tree);
    } else {
        /* file */
        avl_tree_unlock(global.source_tree);
        fserve_client_create(client);
    }
}

static void _handle_delete_request(client_t *client) {
    source_t *source;

    avl_tree_wlock(global.source_tree);
    source = source_find_mount_raw(client->uri);
    if (source) {
        source->running = 0;
        avl_tree_unlock(global.source_tree);
        client_send_204(client);
    } else {
        avl_tree_unlock(global.source_tree);
        client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
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

    ICECAST_LOG_DDEBUG("Client %p is a shoutcast client of stage %i", client, (int)node->shoutcast);

    if (node->shoutcast == 1)
    {
        char *ptr, *headers;

        ICECAST_LOG_DDEBUG("Client %p has buffer: %H", client, client->refbuf->data);

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
        ICECAST_LOG_DDEBUG("Client %p re-added to request queue", client);
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
        client->refbuf->len = 0;
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
    const listener_t *listen_sock;
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
    listen_sock = listensocket_get_listener(client->con->listensocket_effective);
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

        if (resource->listen_socket != NULL && (listen_sock->id == NULL || strcmp(resource->listen_socket, listen_sock->id) != 0))
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

        if (resource->module) {
            module_t *module = module_container_get_module(global.modulecontainer, resource->module);

            if (module != NULL) {
                refobject_unref(client->handler_module);
                client->handler_module = module;
            } else {
                ICECAST_LOG_ERROR("Module used in alias not found: %s", resource->module);
            }
        }

        if (resource->handler) {
            char *func = strdup(resource->handler);
            if (func) {
                free(client->handler_function);
                client->handler_function = func;
            } else {
                ICECAST_LOG_ERROR("Can not allocate memory.");
            }
        }

        ICECAST_LOG_DEBUG("resource has made %s into %s", *uri, new_uri);
        break;
    }

    listensocket_release_listener(client->con->listensocket_effective);
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

    admin_handle_request(client, adminuri);
}

/* Handle any client that passed the authing process.
 */
static void _handle_authed_client(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    /* Update admin parameters just in case auth changed our URI */
    if (_update_admin_command(client) == -1)
        return;

    fastevent_emit(FASTEVENT_TYPE_CLIENT_AUTHED, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    if (result != AUTH_OK) {
        auth_reject_client_on_fail(client);
        return;
    }

    if (acl_test_method(client->acl, client->parser->req_type) != ACL_POLICY_ALLOW) {
        ICECAST_LOG_ERROR("Client (role=%s, acl=%s, username=%s) not allowed to use this request method on %H", client->role, acl_get_name(client->acl), client->username, client->uri);
        auth_reject_client_on_deny(client);
        return;
    }

    /* Dispatch legacy admin.cgi requests */
    if (strcmp(client->uri, "/admin.cgi") == 0) {
        _handle_admin_request(client, client->uri + 1);
        return;
    } /* Dispatch all admin requests */
    else if (strncmp(client->uri, "/admin/", 7) == 0) {
        _handle_admin_request(client, client->uri + 7);
        return;
    }

    if (client->handler_module && client->handler_function) {
        const module_client_handler_t *handler = module_get_client_handler(client->handler_module, client->handler_function);
        if (handler) {
            handler->cb(client->handler_module, client);
            return;
        } else {
            ICECAST_LOG_ERROR("No such handler function in module: %s", client->handler_function);
        }
    }

    switch (client->parser->req_type) {
        case httpp_req_source:
        case httpp_req_put:
            _handle_source_request(client);
        break;
        case httpp_req_stats:
            _handle_stats_request(client);
        break;
        case httpp_req_get:
        case httpp_req_post:
        case httpp_req_options:
            _handle_get_request(client);
        break;
        case httpp_req_delete:
            _handle_delete_request(client);
        break;
        default:
            ICECAST_LOG_ERROR("Wrong request type from client");
            client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
        break;
    }
}

/* Handle clients that still need to authenticate.
 */

static void _handle_authentication_global(client_t *client, void *userdata, auth_result result)
{
    ice_config_t *config;
    auth_stack_t *authstack;

    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        /* Allow global admins access to all mount points */
        !(result == AUTH_OK && client->admin_command != ADMIN_COMMAND_ERROR && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying global authenticators for client %p.", client);
    config = config_get_config();
    authstack = config->authstack;
    auth_stack_addref(authstack);
    config_release_config();
    auth_stack_add_client(authstack, client, _handle_authed_client, userdata);
    auth_stack_release(authstack);
}

static inline mount_proxy * __find_non_admin_mount(ice_config_t *config, const char *name, mount_type type)
{
    if (strcmp(name, "/admin.cgi") == 0 || strncmp(name, "/admin/", 7) == 0)
        return NULL;

    return config_find_mount(config, name, type);
}

static void _handle_authentication_mount_generic(client_t *client, void *userdata, mount_type type, void (*callback)(client_t*, void*, auth_result))
{
    ice_config_t *config;
    mount_proxy *mountproxy;
    auth_stack_t *stack = NULL;

    config = config_get_config();
    mountproxy = __find_non_admin_mount(config, client->uri, type);
    if (!mountproxy) {
        int command_type = admin_get_command_type(client->admin_command);
        if (command_type == ADMINTYPE_MOUNT || command_type == ADMINTYPE_HYBRID) {
            const char *mount = httpp_get_param(client->parser, "mount");
            if (mount)
                mountproxy = __find_non_admin_mount(config, mount, type);
        }
    }
    if (mountproxy && mountproxy->mounttype == type)
        stack = mountproxy->authstack;
    auth_stack_addref(stack);
    config_release_config();

    if (stack) {
        auth_stack_add_client(stack, client, callback, userdata);
        auth_stack_release(stack);
    } else {
        callback(client, userdata, AUTH_NOMATCH);
    }
}

static void _handle_authentication_mount_default(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        /* Allow global admins access to all mount points */
        !(result == AUTH_OK && client->admin_command != ADMIN_COMMAND_ERROR && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying <mount type=\"default\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, userdata, MOUNT_TYPE_DEFAULT, _handle_authentication_global);
}

static void _handle_authentication_mount_normal(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying <mount type=\"normal\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, userdata, MOUNT_TYPE_NORMAL, _handle_authentication_mount_default);
}

static void _handle_authentication_listen_socket(client_t *client)
{
    auth_stack_t *stack = NULL;
    const listener_t *listener;

    listener = listensocket_get_listener(client->con->listensocket_effective);
    if (listener) {
        if (listener->authstack) {
            auth_stack_addref(stack = listener->authstack);
        }
        listensocket_release_listener(client->con->listensocket_effective);
    }

    if (stack) {
        auth_stack_add_client(stack, client, _handle_authentication_mount_normal, NULL);
        auth_stack_release(stack);
    } else {
        _handle_authentication_mount_normal(client, NULL, AUTH_NOMATCH);
    }
}

static void _handle_authentication(client_t *client)
{
    fastevent_emit(FASTEVENT_TYPE_CLIENT_READY_FOR_AUTH, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);
    _handle_authentication_listen_socket(client);
}

static void __prepare_shoutcast_admin_cgi_request(client_t *client)
{
    ice_config_t *config;
    const char *sc_mount;
    const char *pass = httpp_get_query_param(client->parser, "pass");
    const listener_t *listener;

    if (pass == NULL) {
        ICECAST_LOG_ERROR("missing pass parameter");
        return;
    }

    if (client->password) {
        ICECAST_LOG_INFO("Client already has password set");
        return;
    }

    /* Why do we acquire a global lock here? -- ph3-der-loewe, 2018-05-11 */
    global_lock();
    config = config_get_config();
    sc_mount = config->shoutcast_mount;

    listener = listensocket_get_listener(client->con->listensocket_effective);
    if (listener && listener->shoutcast_mount)
        sc_mount = listener->shoutcast_mount;

    httpp_set_query_param(client->parser, "mount", sc_mount);
    listensocket_release_listener(client->con->listensocket_effective);

    httpp_setvar(client->parser, HTTPP_VAR_PROTOCOL, "ICY");
    client->password = strdup(pass);
    config_release_config();
    global_unlock();
}

/* Check if we need body of client */
static int _need_body(client_queue_t *node)
{
    client_t *client = node->client;

    if (node->tried_body)
        return 0;

    if (client->parser->req_type == httpp_req_source) {
        /* SOURCE connection. */
        return 0;
    } else if (client->parser->req_type == httpp_req_put) {
        /* PUT connection.
         * TODO: We may need body for /admin/ but we do not know if it's an admin request yet.
         */
        return 0;
    } else if (client->request_body_length != -1 && (size_t)client->request_body_length != client->request_body_read) {
        return 1;
    } else if (client->request_body_length == -1 && client_body_eof(client) == 0) {
        return 1;
    }

    return 0;
}

/* Updates client's admin_command */
static int _update_admin_command(client_t *client)
{
    if (strcmp(client->uri, "/admin.cgi") == 0) {
        client->admin_command = admin_get_command(client->uri + 1);
        __prepare_shoutcast_admin_cgi_request(client);
        if (!client->password) {
            client_send_error_by_id(client, ICECAST_ERROR_CON_MISSING_PASS_PARAMETER);
            return -1;
        }
    } else if (strncmp(client->uri, "/admin/", 7) == 0) {
        client->admin_command = admin_get_command(client->uri + 7);
    }

    return 0;
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

                client->refbuf->len = 0;

                /* early check if we need more data */
                client_complete(client);
                if (_need_body(node)) {
                    /* Just calling _add_body_client() would do the job.
                     * However, if the client only has a small body this might work without moving it between queues.
                     * -> much faster.
                     */
                    client_slurp_result_t res;
                    ice_config_t *config;
                    time_t timeout;
                    size_t body_size_limit;

                    config = config_get_config();
                    timeout = time(NULL) - config->body_timeout;
                    body_size_limit = config->body_size_limit;
                    config_release_config();

                    res = process_request_body_queue_one(node, timeout, body_size_limit);
                    if (res != CLIENT_SLURP_SUCCESS) {
                        _add_body_client(node);
                        continue;
                    } else {
                        ICECAST_LOG_DEBUG("Success on fast lane");
                    }
                }

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);

                /* assign a port-based shoutcast mountpoint if required */
                if (node->shoutcast_mount && strcmp (rawuri, "/admin.cgi") == 0)
                    httpp_set_query_param (client->parser, "mount", node->shoutcast_mount);

                free (node->bodybuffer);
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
                    if (client->con->tlsmode == ICECAST_TLSMODE_DISABLED || client->con->tls || strstr(upgrade, "TLS/1.0") == NULL) {
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

                if (parser->req_type == httpp_req_options && strcmp(rawuri, "*") == 0) {
                    client->uri = strdup("*");
                    client_send_204(client);
                    continue;
                }

                uri = util_normalise_uri(rawuri);

                if (!uri) {
                    client_destroy (client);
                    continue;
                }

                client->mode = config_str_to_omode(NULL, NULL, httpp_get_param(client->parser, "omode"));

                if (_handle_resources(client, &uri) != 0) {
                    client_destroy (client);
                    continue;
                }

                client->uri = uri;

                if (_update_admin_command(client) == -1)
                    continue;

                _handle_authentication(client);
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


static void __on_sock_count(size_t count, void *userdata)
{
    (void)userdata;

    ICECAST_LOG_DEBUG("Listen socket count is now %zu.", count);

    if (count == 0 && global.running == ICECAST_RUNNING) {
        ICECAST_LOG_INFO("No more listen sockets. Exiting.");
        global.running = ICECAST_HALTING;
    }
}

/* called when listening thread is not checking for incoming connections */
void connection_setup_sockets (ice_config_t *config)
{
    global_lock();
    refobject_unref(global.listensockets);

    if (config == NULL) {
        global_unlock();
        return;
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

    global.listensockets = refobject_new(listensocket_container_t);
    listensocket_container_configure(global.listensockets, config);

    global_unlock();

    listensocket_container_set_sockcount_cb(global.listensockets, __on_sock_count, NULL);
    listensocket_container_setup(global.listensockets);;
}


void connection_close(connection_t *con)
{
    if (!con)
        return;

    ICECAST_LOG_DEBUG("Closing connection %p (connection ID: %llu, sock=%R)", con, (long long unsigned int)con->id, con->sock);

    fastevent_emit(FASTEVENT_TYPE_CONNECTION_DESTROY, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CONNECTION, con);

    tls_unref(con->tls);
    if (con->sock != SOCK_ERROR)
        sock_close(con->sock);
    if (con->ip)
        free(con->ip);
    if (con->readbuffer)
        free(con->readbuffer);
    refobject_unref(con->listensocket_real);
    refobject_unref(con->listensocket_effective);
    free(con);
}

void connection_queue_client(client_t *client)
{
    client_queue_t *node = create_client_node(client);
    _add_connection(node);
}
