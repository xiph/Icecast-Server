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

#include <signal.h>

#include <igloo/igloo.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "icecasttypes.h"

typedef struct ice_global_tag
{
    listensocket_container_t *listensockets;

    int running;

    int sources;
    time_t sources_update;
    int sources_legacy;
    int clients;

#ifdef HAVE_SIG_ATOMIC_T
    volatile sig_atomic_t schedule_config_reread;
#else
    volatile int schedule_config_reread;
#endif

    avl_tree *source_tree;
    /* for locally defined relays */
    relay_t *relays;
    /* relays retrieved from master */
    relay_t *master_relays;

    module_container_t *modulecontainer;
    geoip_db_t *geoip_db;


    /* state */
    bool chroot_succeeded;
    bool chuid_succeeded;
} ice_global_t;

extern ice_global_t global;
extern igloo_ro_t   igloo_instance;

void global_initialize(void);
void global_shutdown(void);
void global_lock(void);
void global_unlock(void);

const char * global_instance_uuid(void);

#endif  /* __GLOBAL_H__ */
