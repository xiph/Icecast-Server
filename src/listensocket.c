/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Listen socket operations.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_POLL
#include <poll.h>
#else
#include <sys/select.h>
#endif

#include <string.h>

#include "common/net/sock.h"

#include "listensocket.h"
#include "global.h"
#include "connection.h"
#include "refobject.h"

#include "logging.h"
#define CATMODULE "listensocket"


struct listensocket_container_tag {
    refobject_base_t __base;
    listensocket_t **sock;
    int *sockref;
    size_t sock_len;
    void (*sockcount_cb)(size_t count, void *userdata);
    void *sockcount_userdata;
};
struct listensocket_tag {
    refobject_base_t __base;
    size_t sockrefc;
    listener_t *listener;
    sock_t sock;
};

static listensocket_t * listensocket_new(const listener_t *listener);
#ifdef HAVE_POLL
static int listensocket__poll_fill(listensocket_t *self, struct pollfd *p);
#else
static int listensocket__select_set(listensocket_t *self, fd_set *set, int *max);
static int listensocket__select_isset(listensocket_t *self, fd_set *set);
#endif

static inline void __call_sockcount_cb(listensocket_container_t *self)
{
    if (self->sockcount_cb == NULL)
        return;

    self->sockcount_cb(listensocket_container_sockcount(self), self->sockcount_userdata);
}

static void listensocket_container_clear_sockets(listensocket_container_t *self)
{
    size_t i;

    if (self->sock == NULL)
        return;

    for (i = 0; i < self->sock_len; i++) {
        if (self->sock[i] != NULL) {
            if (self->sockref[i]) {
                listensocket_unrefsock(self->sock[i]);
            }
            refobject_unref(self->sock[i]);
            self->sock[i] = NULL;
        }
    }

    self->sock_len = 0;
    free(self->sock);
    free(self->sockref);
    self->sock = NULL;
    self->sockref = NULL;

    __call_sockcount_cb(self);
}


static void __listensocket_container_free(refobject_t self, void **userdata)
{
    listensocket_container_t *container = REFOBJECT_TO_TYPE(self, listensocket_container_t *);
    listensocket_container_clear_sockets(container);
}

listensocket_container_t *  listensocket_container_new(void)
{
    listensocket_container_t *self = REFOBJECT_TO_TYPE(refobject_new(sizeof(listensocket_container_t), __listensocket_container_free, NULL, NULL, NULL), listensocket_container_t *);
    if (!self)
        return NULL;


    self->sock = NULL;
    self->sock_len = 0;
    self->sockcount_cb = NULL;
    self->sockcount_userdata = NULL;

    return self;
}

int                         listensocket_container_configure(listensocket_container_t *self, const ice_config_t *config)
{
    listensocket_t **n;
    listener_t *cur;
    int *r;
    size_t i;

    if (!self || !config)
        return -1;

    if (!config->listen_sock_count) {
        listensocket_container_clear_sockets(self);
        return 0;
    }

    n = calloc(config->listen_sock_count, sizeof(listensocket_t *));
    r = calloc(config->listen_sock_count, sizeof(int));
    if (!n || !r) {
        free(n);
        free(r);
        return -1;
    }

    cur = config->listen_sock;
    for (i = 0; i < config->listen_sock_count; i++) {
        if (cur) {
            n[i] = listensocket_new(cur);
        } else {
            n[i] = NULL;
        }
        if (n[i] == NULL) {
            for (; i; i--) {
                refobject_unref(n[i - 1]);
            }
            return -1;
        }

        cur = cur->next;
    }

    listensocket_container_clear_sockets(self);

    self->sock = n;
    self->sockref = r;
    self->sock_len = config->listen_sock_count;

    return 0;
}

int                         listensocket_container_setup(listensocket_container_t *self) {
    size_t i;
    int ret = 0;

    if (!self)
        return -1;

    for (i = 0; i < self->sock_len; i++) {
        if (self->sockref[i])
            continue;

        if (listensocket_refsock(self->sock[i]) == 0) {
            self->sockref[i] = 1;
        } else {
            ICECAST_LOG_DEBUG("Can not ref socket.");
            ret = 1;
        }
    }

    __call_sockcount_cb(self);

    return ret;
}

static connection_t *       listensocket_container_accept__inner(listensocket_container_t *self, int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds[self->sock_len];
    listensocket_t *socks[self->sock_len];
    size_t i, found, p;
    int ok;
    int ret;

    for (i = 0, found = 0; i < self->sock_len; i++) {
        ok = self->sockref[i];

        if (ok && listensocket__poll_fill(self->sock[i], &(ufds[found])) == -1) {
            ICECAST_LOG_WARN("Can not poll on closed socket.");
            ok = 0;
        }

        if (ok) {
            socks[found] = self->sock[i];
            found++;
        }
    }

    if (!found) {
        ICECAST_LOG_ERROR("No sockets found to poll on.");
        return NULL;
    }

    ret = poll(ufds, found, timeout);
    if (ret <= 0)
        return NULL;

    for (i = 0; i < found; i++) {
        if (ufds[i].revents & POLLIN) {
            return listensocket_accept(socks[i]);
        }

        if (!(ufds[i].revents & (POLLHUP|POLLERR|POLLNVAL)))
            continue;

        for (p = 0; p < self->sock_len; p++) {
            if (self->sock[p] == socks[i]) {
                if (self->sockref[p]) {
                    ICECAST_LOG_ERROR("Closing listen socket in error state.");
                    listensocket_unrefsock(socks[i]);
                    self->sockref[p] = 0;
                    __call_sockcount_cb(self);
                }
            }
        }
    }

    return NULL;
#else
    fd_set rfds;
    size_t i;
    struct timeval tv, *p=NULL;
    int ret;
    int max = -1;

    FD_ZERO(&rfds);

    if (timeout >= 0) {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        p = &tv;
    }

    for (i = 0; i < self->sock_len; i++) {
        if (self->sockref[i]) {
            listensocket__select_set(self->sock[i], &rfds, &max);
        }
    }

    ret = select(max+1, &rfds, NULL, NULL, p);
    if (ret <= 0)
        return NULL;

    for (i = 0; i < self->sock_len; i++) {
        if (self->sockref[i]) {
            if (listensocket__select_isset(self->sock[i], &rfds)) {
                return listensocket_accept(self->sock[i]);
            }
        }
    }

    return NULL;
#endif
}
connection_t *              listensocket_container_accept(listensocket_container_t *self, int timeout)
{
    if (!self)
        return NULL;

    return listensocket_container_accept__inner(self, timeout);
}

int                         listensocket_container_set_sockcount_cb(listensocket_container_t *self, void (*cb)(size_t count, void *userdata), void *userdata)
{
    if (!self)
        return -1;

    self->sockcount_cb = cb;
    self->sockcount_userdata = userdata;

    return 0;
}

ssize_t                     listensocket_container_sockcount(listensocket_container_t *self)
{
    ssize_t count = 0;
    size_t i;

    if (!self)
        return -1;

    for (i = 0; i < self->sock_len; i++) {
        if (self->sockref[i]) {
            count++;
        }
    }

    return count;
}

static void __listensocket_free(refobject_t self, void **userdata)
{
    listensocket_t *listensocket = REFOBJECT_TO_TYPE(self, listensocket_t *);

    if (listensocket->sockrefc) {
        ICECAST_LOG_ERROR("BUG: listensocket->sockrefc == 0 && listensocket->sockrefc == %zu", listensocket->sockrefc);
        listensocket->sockrefc = 1;
        listensocket_unrefsock(listensocket);
    }

    while ((listensocket->listener = config_clear_listener(listensocket->listener)));
}

static listensocket_t * listensocket_new(const listener_t *listener) {
    listensocket_t *self;

    if (listener == NULL)
        return NULL;

    self = REFOBJECT_TO_TYPE(refobject_new(sizeof(listensocket_t), __listensocket_free, NULL, NULL, NULL), listensocket_t *);
    if (!self)
        return NULL;

    self->sock = SOCK_ERROR;

    self->listener = config_copy_listener_one(listener);
    if (self->listener == NULL) {
        refobject_unref(self);
        return NULL;
    }

    return self;
}

int                         listensocket_refsock(listensocket_t *self)
{
    if (!self)
        return -1;

    if (self->sockrefc) {
        self->sockrefc++;
        return 0;
    }

    self->sock = sock_get_server_socket(self->listener->port, self->listener->bind_address);
    if (self->sock == SOCK_ERROR)
        return -1;

    if (sock_listen(self->sock, ICECAST_LISTEN_QUEUE) == SOCK_ERROR) {
        sock_close(self->sock);
        self->sock = SOCK_ERROR;
        return -1;
    }

    if (self->listener->so_sndbuf)
        sock_set_send_buffer(self->sock, self->listener->so_sndbuf);

    sock_set_blocking(self->sock, 0);

    self->sockrefc++;

    return 0;
}

int                         listensocket_unrefsock(listensocket_t *self)
{
    if (!self)
        return -1;

    self->sockrefc--;
    if (self->sockrefc)
        return 0;

    if (self->sock == SOCK_ERROR)
        return 0;

    sock_close(self->sock);
    self->sock = SOCK_ERROR;

    return 0;
}

connection_t *              listensocket_accept(listensocket_t *self)
{
    connection_t *con;
    sock_t sock;
    char *ip;

    if (!self)
        return NULL;

    ip = calloc(MAX_ADDR_LEN, 1);
    if (!ip)
        return NULL;

    sock = sock_accept(self->sock, ip, MAX_ADDR_LEN);
    if (sock == SOCK_ERROR) {
        free(ip);
        return NULL;
    }

    if (strncmp(ip, "::ffff:", 7) == 0) {
        memmove(ip, ip+7, strlen(ip+7)+1);
    }

    con = connection_create(sock, self, self, ip);
    if (con == NULL) {
        sock_close(sock);
        free(ip);
        return NULL;
    }

    return con;
}

const listener_t *          listensocket_get_listener(listensocket_t *self)
{
    if (!self)
        return NULL;

    return self->listener;
}

#ifdef HAVE_POLL
static int listensocket__poll_fill(listensocket_t *self, struct pollfd *p)
{
    if (!self || self->sock == SOCK_ERROR)
        return -1;

    memset(p, 0, sizeof(*p));
    p->fd = self->sock;
    p->events = POLLIN;
    p->revents = 0;

    return 0;
}
#else
static int listensocket__select_set(listensocket_t *self, fd_set *set, int *max)
{
    if (!self || self->sock == SOCK_ERROR)
        return -1;

    if (*max < self->sock)
        *max = self->sock;

    FD_SET(self->sock, set);
    return 0;
}
static int listensocket__select_isset(listensocket_t *self, fd_set *set)
{
    if (!self || self->sock == SOCK_ERROR)
        return -1;
    return FD_ISSET(self->sock, set);
}
#endif
