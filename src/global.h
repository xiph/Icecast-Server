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

#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#define ICE_LISTEN_QUEUE 5

#define ICE_RUNNING 1
#define ICE_HALTING 2

#define ICECAST_VERSION_STRING "Icecast " PACKAGE_VERSION

#define MAX_LISTEN_SOCKETS 10

#include "thread/thread.h"
#include "slave.h"

typedef struct ice_global_tag
{
    int serversock[MAX_LISTEN_SOCKETS];
    int server_sockets;

    int running;

    int sources;
    int clients;
    int schedule_config_reread;

    avl_tree *source_tree;
    /* for locally defined relays */
    struct _relay_server *relays;
    /* relays retrieved from master */
    struct _relay_server *master_relays;

    /* slave relay list */
    unsigned int slave_count;
    struct _slave_host *slaves;

    cond_t shutdown_cond;
} ice_global_t;

extern ice_global_t global;

void global_initialize(void);
void global_shutdown(void);
void global_lock(void);
void global_unlock(void);

#endif  /* __GLOBAL_H__ */
