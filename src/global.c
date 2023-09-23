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
 * Copyright 2015-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "icecasttypes.h"
#include <igloo/ro.h>
#include <igloo/uuid.h>
#include <igloo/sp.h>
#include <igloo/error.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"

#include "global.h"
#include "module.h"
#include "source.h"

ice_global_t global;
igloo_ro_t   igloo_instance = igloo_RO_NULL;

static mutex_t _global_mutex;

static const char * _instance_uuid = NULL;

void global_initialize(void)
{
    memset(&global, 0, sizeof(global));
    global.sources_update = time(NULL);
    global.source_tree = avl_tree_new(source_compare_sources, NULL);
    igloo_ro_new(&global.modulecontainer, module_container_t, igloo_instance);
    thread_mutex_create(&_global_mutex);
}

void global_shutdown(void)
{
    thread_mutex_destroy(&_global_mutex);
    igloo_ro_unref(&global.modulecontainer);
    igloo_ro_unref(&global.geoip_db);
    avl_tree_free(global.source_tree, NULL);
    igloo_sp_unref(&_instance_uuid, igloo_instance);
}

void global_lock(void)
{
    thread_mutex_lock(&_global_mutex);
}

void global_unlock(void)
{
    thread_mutex_unlock(&_global_mutex);
}

const char * global_instance_uuid(void)
{
    if (_instance_uuid)
        return _instance_uuid;

    if (igloo_uuid_new_random_sp(&_instance_uuid, igloo_instance) != igloo_ERROR_NONE) {
        return NULL;
    }

    return _instance_uuid;
}
