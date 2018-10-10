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

#include <string.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"

#include "global.h"
#include "refobject.h"
#include "module.h"
#include "source.h"

ice_global_t global;

static mutex_t _global_mutex;

void global_initialize(void)
{
    global.listensockets = NULL;
    global.relays = NULL;
    global.master_relays = NULL;
    global.running = 0;
    global.clients = 0;
    global.sources = 0;
    global.source_tree = avl_tree_new(source_compare_sources, NULL);
    global.modulecontainer = refobject_new(module_container_t);
    thread_mutex_create(&_global_mutex);
}

void global_shutdown(void)
{
    thread_mutex_destroy(&_global_mutex);
    refobject_unref(global.modulecontainer);
    avl_tree_free(global.source_tree, NULL);
}

void global_lock(void)
{
    thread_mutex_lock(&_global_mutex);
}

void global_unlock(void)
{
    thread_mutex_unlock(&_global_mutex);
}
