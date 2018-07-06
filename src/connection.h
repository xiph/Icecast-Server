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
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include <sys/types.h>
#include <time.h>

#include "tls.h"

#include "icecasttypes.h"
#include "compat.h"
#include "common/thread/thread.h"
#include "common/net/sock.h"

typedef unsigned long connection_id_t;

struct connection_tag {
    connection_id_t id;

    time_t con_time;
    time_t discon_time;
    uint64_t sent_bytes;

    sock_t sock;
    listensocket_t *listensocket_real;
    listensocket_t *listensocket_effective;
    int error;

    tlsmode_t tlsmode;
    tls_t *tls;
    int (*send)(connection_t *handle, const void *buf, size_t len);
    int (*read)(connection_t *handle, void *buf, size_t len);

    void *readbuffer;
    size_t readbufferlen;

    char *ip;
};

void connection_initialize(void);
void connection_shutdown(void);
void connection_reread_config(ice_config_t *config);
void connection_accept_loop(void);
void connection_setup_sockets(ice_config_t *config);
void connection_close(connection_t *con);
connection_t *connection_create(sock_t sock, listensocket_t *listensocket_real, listensocket_t* listensocket_effective, char *ip);
int connection_complete_source(source_t *source, int response);
void connection_queue(connection_t *con);
void connection_uses_tls(connection_t *con);

ssize_t connection_read_bytes(connection_t *con, void *buf, size_t len);
int connection_read_put_back(connection_t *con, const void *buf, size_t len);

extern rwlock_t _source_shutdown_rwlock;

#endif  /* __CONNECTION_H__ */
