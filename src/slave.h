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

#ifndef __SLAVE_H__
#define __SLAVE_H__

#include "common/thread/thread.h"
#include "icecasttypes.h"

struct _relay_server {
    char *server;
    int port;
    char *mount;
    char *username;
    char *password;
    char *localmount;
    char *bind;
    source_t *source;
    int mp3metadata;
    int on_demand;
    int running;
    int cleanup;
    time_t start;
    thread_type *thread;
    relay_server *next;
};

void slave_initialize(void);
void slave_shutdown(void);
void slave_update_all_mounts (void);
void slave_rebuild_mounts (void);
relay_server *relay_free (relay_server *relay);

#endif  /* __SLAVE_H__ */
