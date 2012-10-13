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
 * Copyright 2012,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "thread/thread.h"
#include "roarapi.h"
#include "plugins.h"

#define CATMODULE "plugins"

#include "logging.h"

#ifdef HAVE_ROARAUDIO
// all vars are protected by roarapi_*lock();
static struct roar_plugincontainer * container = NULL;
static int plugins_running = 0;
static struct roar_scheduler * sched = NULL;
static struct roar_scheduler_source source_container = {.type = ROAR_SCHEDULER_PLUGINCONTAINER};
static struct roar_scheduler_source source_timeout = {.type = ROAR_SCHEDULER_TIMEOUT, .handle.timeout = {0, 500000000L}};

// not protected by roarapi_*lock()
static thread_type * plugin_thread;
static void *plugin_runner(void *arg);
#endif

void plugins_initialize(void)
{
#ifdef HAVE_ROARAUDIO
    roarapi_lock();
    container = roar_plugincontainer_new_simple(ICECAST_HOST_STRING, PACKAGE_VERSION);
    roar_plugincontainer_set_autoappsched(container, 1);
    sched = roar_scheduler_new(ROAR_SCHEDULER_FLAG_DEFAULT, ROAR_SCHEDULER_STRATEGY_DEFAULT);
    source_container.handle.container = container;
    roar_scheduler_source_add(sched, &source_container);
    roar_scheduler_source_add(sched, &source_timeout);
    roarapi_unlock();
    plugin_thread = thread_create("Plugin Thread", plugin_runner, NULL, 0);
#endif
}

#ifdef HAVE_ROARAUDIO
static inline void plugins_shutdown_plugin_thread(void)
{
    if (!plugins_running)
        return;
    plugins_running = 0;
    roarapi_unlock();
    thread_join(plugin_thread);
    roarapi_lock();
}
#endif

void plugins_shutdown(void)
{
#ifdef HAVE_ROARAUDIO
    roarapi_lock();
    plugins_shutdown_plugin_thread();
    roar_plugincontainer_unref(container);
    roar_scheduler_unref(sched);
    container = NULL;
    sched = NULL;
    roarapi_unlock();
#endif
}

static void plugins_load_one(plugin_t *plugin)
{
#ifdef HAVE_ROARAUDIO
    struct roar_dl_librarypara * para = NULL;

    if (plugin->args)
    {
        para = roar_dl_para_new(plugin->args, NULL, ICECAST_HOST_STRING, PACKAGE_VERSION);
	if (!para)
	    return;
    }

    roar_plugincontainer_load(container, plugin->name, para);
#else
    ERROR1("Can not load plugin \"%s\" as RoarAudio support is not compiled in.". plugin->name);
#endif
}

void plugins_load(ice_config_t * config)
{
    static int done = 0;
    int need_release = 0;
    plugin_t *next;

    // ensure this is not done twice as the current code does not support this.
    if (done)
        return;
    done = 1;

    if (!config)
    {
        config = config_get_config();
	need_release = 1;
    }

    roarapi_lock();
    next = config->plugins;
    while (next)
    {
	plugins_load_one(next);
        next = next->next;
    }
    roarapi_unlock();

    if (need_release)
        config_release_config();
}

#ifdef HAVE_ROARAUDIO
static void *plugin_runner(void *arg)
{
    int ret;
    (void)arg;

    roarapi_lock();
    plugins_running = 1;
    while (plugins_running)
    {
        ret = roar_scheduler_iterate(sched);

        roarapi_unlock();
	if ( ret < 1 )
	    thread_sleep(500000);
	else
	    thread_sleep(5000);
        roarapi_lock();
    }
    roarapi_unlock();

    return NULL;
}
#endif
