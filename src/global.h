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
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#define ICECAST_LISTEN_QUEUE 5

#define ICECAST_RUNNING 1
#define ICECAST_HALTING 2

#define ICECAST_VERSION_STRING "Icecast " PACKAGE_VERSION

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "icecasttypes.h"

typedef struct ice_global_tag
{
    listensocket_container_t *listensockets;

    int running;

    int sources;
    int sources_legacy;
    int clients;
    int schedule_config_reread;

    avl_tree *source_tree;
    /* for locally defined relays */
    relay_t *relays;
    /* relays retrieved from master */
    relay_t *master_relays;

    module_container_t *modulecontainer;

    cond_t shutdown_cond;
} ice_global_t;

extern ice_global_t global;

void global_initialize(void);
void global_shutdown(void);
void global_lock(void);
void global_unlock(void);

#endif  /* __GLOBAL_H__ */
