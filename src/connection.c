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
#include "connection_handle.h"
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
    bool ready;
    struct client_queue_tag *next;
} client_queue_entry_t;

typedef struct {
    client_queue_entry_t *head;
    client_queue_entry_t **tail;
    mutex_t mutex;
    cond_t cond;
    thread_type *thread;
    bool running;
#ifdef HAVE_POLL
    struct pollfd *pollfds;
    size_t pollfds_len;
#endif
} client_queue_t;

#define QUEUE_READY_TIMEOUT 50

static spin_t _connection_lock; // protects _current_id
static volatile connection_id_t _current_id = 0;
static int _initialized = 0;

static client_queue_t _request_queue;
static client_queue_t _connection_queue;
static client_queue_t _body_queue;
static client_queue_t _handle_queue;
static bool tls_ok = false;
static tls_ctx_t *tls_ctx;

/* filtering client connection based on IP */
static matchfile_t *banned_ip, *allowed_ip;

rwlock_t _source_shutdown_rwlock;

static void get_tls_certificate(ice_config_t *config);
static void free_client_node(client_queue_entry_t *node);
static void * _handle_connection(client_queue_t *queue);
static void * process_request_queue (client_queue_t *queue);
static void * process_request_body_queue (client_queue_t *queue);
static void * handle_client_worker(client_queue_t *queue);

static void client_queue_init(client_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    queue->tail = &(queue->head);
    thread_mutex_create(&(queue->mutex));
    thread_cond_create(&(queue->cond));
}

static void client_queue_destroy(client_queue_t *queue)
{
    if (queue->thread) {
        queue->running = false;
        thread_cond_broadcast(&(queue->cond));
        thread_join(queue->thread);
    }
    thread_cond_destroy(&(queue->cond));
    thread_mutex_destroy(&(queue->mutex));
#ifdef HAVE_POLL
    free(queue->pollfds);
#endif
}

static void client_queue_start_thread(client_queue_t *queue, const char *name, void *(*func)(client_queue_t *))
{
    if (queue->thread)
        return;
    queue->running = true;
    queue->thread = thread_create(name, (void*(*)(void*))func, queue, THREAD_ATTACHED);
}

static inline bool client_queue_running(client_queue_t *queue)
{
    return queue->running;
}

static void client_queue_add(client_queue_t *queue, client_queue_entry_t *entry)
{
    thread_mutex_lock(&(queue->mutex));
    *(queue->tail) = entry;
    queue->tail = &(entry->next);
    thread_mutex_unlock(&(queue->mutex));
    thread_cond_broadcast(&(queue->cond));
}

static void client_queue_wait(client_queue_t *queue)
{
    thread_cond_wait(&(queue->cond));
}

static client_queue_entry_t * client_queue_shift(client_queue_t *queue, client_queue_entry_t *stop)
{
    client_queue_entry_t *ret;

    thread_mutex_lock(&(queue->mutex));
    ret = queue->head;
    if (ret) {
        if (ret == stop) {
            ret = NULL;
        } else {
            queue->head = ret->next;
            if (!queue->head) {
                queue->tail = &(queue->head);
            }
            ret->next = NULL;
        }
    }
    thread_mutex_unlock(&(queue->mutex));

    return ret;
}

static bool client_queue_check_ready(client_queue_t *queue, int timeout, time_t connection_timeout)
{
    if (!queue->head)
        return false;

#ifdef HAVE_POLL
    if (true) {
        bool had_timeout = false;
        size_t count = 0;
        size_t i;
        client_queue_entry_t *cur;

        thread_mutex_lock(&(queue->mutex));
        for (cur = queue->head; cur; cur = cur->next) {
            count++;
            if (cur->client->con->con_time <= connection_timeout) {
                cur->ready = true;
                had_timeout = true;
            } else {
                cur->ready = false;
            }
        }

        if (queue->pollfds_len < count) {
            free(queue->pollfds);
            queue->pollfds = calloc(count, sizeof(*queue->pollfds));
            if (queue->pollfds) {
                queue->pollfds_len = count;
            } else {
                ICECAST_LOG_ERROR("Allocation of queue->pollfds failed. BAD.");
                queue->pollfds_len = 0;
                thread_mutex_unlock(&(queue->mutex));
                return false;
            }
        } else {
            memset(queue->pollfds, 0, sizeof(*queue->pollfds)*count);
        }

        for (cur = queue->head, i = 0; cur && i < count; cur = cur->next, i++) {
            queue->pollfds[i].fd = cur->client->con->sock;
            queue->pollfds[i].events = POLLIN;
        }
        thread_mutex_unlock(&(queue->mutex));

        if (had_timeout)
            return true;

        if (poll(queue->pollfds, count, timeout) < 1)
            return false;

        thread_mutex_lock(&(queue->mutex));
        for (cur = queue->head; cur; cur = cur->next) {
            for (i = 0; i < count; i++) {
                if (queue->pollfds[i].fd == cur->client->con->sock) {
                    if (queue->pollfds[i].revents) {
                        cur->ready = true;
                    }
                }
            }
        }
        thread_mutex_unlock(&(queue->mutex));
    }
#endif

    return true;
}

static bool client_queue_check_ready_wait(client_queue_t *queue, int timeout, int connection_timeout)
{
    while (queue->running) {
        if (client_queue_check_ready(queue, timeout, time(NULL) - connection_timeout))
            return true;

        if (!queue->head)
            thread_cond_wait(&(queue->cond));
    }

    return false;
}

static client_queue_entry_t * client_queue_shift_ready(client_queue_t *queue, client_queue_entry_t *stop)
{
#ifdef HAVE_POLL
    client_queue_entry_t *cur;
    client_queue_entry_t *last = NULL;

    if (!queue->head)
        return NULL;

    thread_mutex_lock(&(queue->mutex));
    for (cur = queue->head; cur && cur != stop; cur = cur->next) {
        if (cur->ready) {
            // use this one.
            if (last == NULL) {
                /* we are the head */
                queue->head = cur->next;
                if (!queue->head) {
                    queue->tail = &(queue->head);
                }
            } else {
                last->next = cur->next;
                if (queue->tail == &(cur->next)) {
                    queue->tail = &(last->next);
                }
            }

            cur->next = NULL;
            thread_mutex_unlock(&(queue->mutex));
            return cur;
        }
        last = cur;
    }
    thread_mutex_unlock(&(queue->mutex));
    return NULL;
#else
    /* just return any */
    return client_queue_shift(queue, stop);
#endif
}

void connection_initialize(void)
{
    if (_initialized)
        return;

    thread_spin_create (&_connection_lock);
    thread_mutex_create(&move_clients_mutex);
    thread_rwlock_create(&_source_shutdown_rwlock);
    client_queue_init(&_request_queue);
    client_queue_init(&_connection_queue);
    client_queue_init(&_body_queue);
    client_queue_init(&_handle_queue);

    _initialized = 1;
}

void connection_shutdown(void)
{
    if (!_initialized)
        return;

    tls_ctx_unref(tls_ctx);
    matchfile_release(banned_ip);
    matchfile_release(allowed_ip);

    thread_rwlock_destroy(&_source_shutdown_rwlock);
    thread_spin_destroy (&_connection_lock);
    thread_mutex_destroy(&move_clients_mutex);
    client_queue_destroy(&_request_queue);
    client_queue_destroy(&_connection_queue);
    client_queue_destroy(&_body_queue);
    client_queue_destroy(&_handle_queue);

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
        if (tls_error(con->tls)) {
            ICECAST_LOG_DEBUG("Client hit TLS error (con=%p, tls=%p)", con, con->tls);
            con->error = 1;
        }
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

#if !defined(HAVE_POLL) && !defined(_WIN32)
    if (sock >= FD_SETSIZE) {
        ICECAST_LOG_ERROR("Can not create connection: System filehandle set overflow");
        return NULL;
    }
#endif

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

/* run along queue checking for any data that has come in or a timeout */
static bool process_request_queue_one (client_queue_entry_t *node, time_t timeout)
{
    client_t *client = node->client;
    int len = PER_CLIENT_REFBUF_SIZE - 1 - node->offset;
    char *buf = client->refbuf->data + node->offset;

    ICECAST_LOG_DDEBUG("Checking on client %p", client);

    if (client->con->tlsmode == ICECAST_TLSMODE_AUTO || client->con->tlsmode == ICECAST_TLSMODE_AUTO_NO_PLAIN) {
        char peak;
        if (recv(client->con->sock, &peak, 1, MSG_PEEK) == 1) {
            if (peak == 0x16) { /* TLS Record Protocol Content type 0x16 == Handshake */
                connection_uses_tls(client->con);
            }
        }
    }

    if (len > 0) {
        if (client->con->con_time <= timeout) {
            ICECAST_LOG_DEBUG("Timeout on client %p (connection ID: %llu, sock=%R)", client, (long long unsigned int)client->con->id, client->con->sock);
            client_destroy(client);
            free_client_node(node);
            return true;
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
            client_queue_add(&_connection_queue, node);
            return true;
        }
    } else if (len == 0 || client->con->error) {
        client_destroy(client);
        free_client_node(node);
        return true;
    }

    return false;
}

static void * process_request_queue (client_queue_t *queue)
{
    while (client_queue_running(queue)) {
        client_queue_entry_t *stop = NULL;
        client_queue_entry_t *node;
        ice_config_t *config;
        int timeout;
        time_t now;

        config = config_get_config();
        timeout = config->header_timeout;
        config_release_config();

        client_queue_check_ready_wait(queue, QUEUE_READY_TIMEOUT, timeout);

        now = time(NULL);
        while ((node = client_queue_shift_ready(queue, stop))) {
            if (process_request_queue_one(node, now - timeout))
                continue;

            client_queue_add(queue, node);
            if (!stop)
                stop = node;
        }
    }

    return NULL;
}

static client_slurp_result_t process_request_body_queue_one(client_queue_entry_t *node, time_t timeout, size_t body_size_limit)
{
        client_t *client = node->client;
        client_slurp_result_t res;

        node->ready = false;

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
            if (client->con->con_time <= (time(NULL) - timeout) || client->request_body_read >= body_size_limit || client->con->error) {
                return CLIENT_SLURP_ERROR;
            }
        }

        return res;
}

/* This queue reads data from the body of clients. */
static void * process_request_body_queue (client_queue_t *queue)
{
    while (client_queue_running(queue)) {
        client_queue_entry_t *stop = NULL;
        client_queue_entry_t *node;
        ice_config_t *config;
        int timeout;
        size_t body_size_limit;

        ICECAST_LOG_DDEBUG("Processing body queue.");

        config = config_get_config();
        timeout = config->body_timeout;
        body_size_limit = config->body_size_limit;
        config_release_config();

        client_queue_check_ready_wait(queue, QUEUE_READY_TIMEOUT, timeout);

        while ((node = client_queue_shift(queue, stop))) {
            client_t *client = node->client;
            client_slurp_result_t res;

            node->tried_body = 1;

            ICECAST_LOG_DEBUG("Got client %p in body queue.", client);

            res = process_request_body_queue_one(node, timeout, body_size_limit);

            if (res == CLIENT_SLURP_NEEDS_MORE_DATA) {
                client_queue_add(queue, node);
                if (!stop)
                    stop = node;
            } else {
                ICECAST_LOG_DEBUG("Putting client %p back in connection queue.", client);

                client_queue_add(&_connection_queue, node);
                continue;
            }
        }
    }

    return NULL;
}

static client_queue_entry_t *create_client_node(client_t *client)
{
    client_queue_entry_t *node = calloc (1, sizeof (client_queue_entry_t));
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

static void free_client_node(client_queue_entry_t *node)
{
    free(node->shoutcast_mount);
    free(node->bodybuffer);
    free(node);
}

void connection_queue(connection_t *con)
{
    client_queue_entry_t *node;
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

    client_queue_add(&_request_queue, node);
    stats_event_inc(NULL, "connections");
}

void connection_accept_loop(void)
{
    ice_config_t *config;

    config = config_get_config();
    get_tls_certificate(config);
    config_release_config();

    client_queue_start_thread(&_request_queue, "Request Queue", process_request_queue);
    client_queue_start_thread(&_connection_queue, "Con Queue", _handle_connection);
    client_queue_start_thread(&_body_queue, "Body Queue", process_request_body_queue);
    client_queue_start_thread(&_handle_queue, "Client Handler", handle_client_worker);

    while (global.running == ICECAST_RUNNING) {
        connection_t *con = listensocket_container_accept(global.listensockets, 800);

        if (con) {
            connection_queue(con);
        }
    }
    ICECAST_LOG_INFO("No longer running. Shutting down...");

    /* Give all the other threads notification to shut down */

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

        if (format_type == FORMAT_TYPE_GENERIC)
           source_set_flags(source, SOURCE_FLAG_FORMAT_GENERIC);

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
        global.sources_update = time(NULL);
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

static void _handle_shoutcast_compatible(client_queue_entry_t *node)
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
            free_client_node(node);
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
        client_queue_add(&_request_queue, node);
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
    free_client_node(node);
    return;
}

/* Check if we need body of client */
static int _need_body(client_queue_entry_t *node)
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

/* Connection thread. Here we take clients off the connection queue and check
 * the contents provided. We set up the parser then hand off to the specific
 * request handler.
 */
static void * _handle_connection(client_queue_t *queue)
{
    while (client_queue_running(queue)) {
        client_queue_entry_t *node;

        node = client_queue_shift(&_connection_queue, NULL);
        if (node) {
            client_t *client = node->client;
            http_parser_t *parser;
            const char *rawuri;
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
            if ((already_parsed || httpp_parse (parser, client->refbuf->data, node->offset)) && !client->con->error) {
                client->refbuf->len = 0;

                /* early check if we need more data */
                client_complete(client);
                if (_need_body(node)) {
                    /* Just calling client_queue_add(&_body_queue, node) would do the job.
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
                        ICECAST_LOG_DEBUG("Putting client %p in body queue.", client);
                        client_queue_add(&_body_queue, node);
                        continue;
                    } else {
                        ICECAST_LOG_DEBUG("Success on fast lane (client=%p)", client);
                    }
                }

                if (client->request_body_length != -1 && (size_t)client->request_body_length != client->request_body_read) {
                    ICECAST_LOG_DEBUG("Incomplete request, dropping client (client=%p)", client);
                    free_client_node(node);
                    client_destroy(client);
                    continue;
                }

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);

                /* assign a port-based shoutcast mountpoint if required */
                if (node->shoutcast_mount && strcmp (rawuri, "/admin.cgi") == 0)
                    httpp_set_query_param (client->parser, "mount", node->shoutcast_mount);


                client_queue_add(&_handle_queue, node);
            } else {
                free_client_node(node);
                ICECAST_LOG_ERROR("HTTP request parsing failed (client=%p)", client);
                client_destroy (client);
            }
        } else {
            client_queue_wait(queue);
        }
    }

    return NULL;
}

static void * handle_client_worker(client_queue_t *queue)
{
    while (client_queue_running(queue)) {
        client_queue_entry_t *node = client_queue_shift(queue, NULL);
        if (node) {
            client_t *client = node->client;
            free_client_node(node);

            connection_handle_client(client);
        } else {
            client_queue_wait(queue);
        }
    }

    return NULL;
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
    client_queue_entry_t *node = create_client_node(client);
    client_queue_add(&_connection_queue, node);
}
