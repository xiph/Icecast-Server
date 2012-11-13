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
static struct roar_scheduler_source source_cpi_service = {.type = ROAR_SCHEDULER_CPI_SERVICE};
static struct roar_scheduler_source source_container = {.type = ROAR_SCHEDULER_PLUGINCONTAINER};
static struct roar_scheduler_source source_timeout = {.type = ROAR_SCHEDULER_TIMEOUT, .handle.timeout = {0, 500000000L}};

// not protected by roarapi_*lock()
static thread_type * plugin_thread;
static void *plugin_runner(void *arg);
#endif

void plugins_initialize(void)
{
#ifdef HAVE_ROARAUDIO
    DEBUG0("Plugin Interface is being initialized");
    roarapi_lock();
    container = roar_plugincontainer_new_simple(ICECASTPH_APPNAME, ICECASTPH_ABIVERSION);
    roar_plugincontainer_set_autoappsched(container, 1);
    sched = roar_scheduler_new(ROAR_SCHEDULER_FLAG_DEFAULT, ROAR_SCHEDULER_STRATEGY_DEFAULT);
    source_container.handle.container = container;
    roar_scheduler_source_add(sched, &source_cpi_service);
    roar_scheduler_source_add(sched, &source_container);
    roar_scheduler_source_add(sched, &source_timeout);
    roarapi_unlock();
    plugin_thread = thread_create("Plugin Thread", plugin_runner, NULL, 0);
    DEBUG0("Plugin Interface is now initialized");
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
    DEBUG0("Plugin Interface is being shut down");
    roarapi_lock();
    plugins_shutdown_plugin_thread();
    roar_plugincontainer_unref(container);
    roar_scheduler_unref(sched);
    container = NULL;
    sched = NULL;
    roarapi_unlock();
    DEBUG0("Plugin Interface is now shut down");
#endif
}

static void plugins_load_one(plugin_t *plugin)
{
#ifdef HAVE_ROARAUDIO
    struct roar_dl_librarypara * para = NULL;

    para = roar_dl_para_new(plugin->args, NULL, ICECASTPH_APPNAME, ICECASTPH_ABIVERSION);
    if (!para)
        return;

    roar_plugincontainer_load(container, plugin->name, para);
    roar_dl_para_unref(para);
#else
    ERROR1("Can not load plugin \"%s\" as RoarAudio support is not compiled in.", plugin->name);
#endif
}

static void to_lower(char * p)
{
    for (; *p; p++)
        if ( *p >= 'A' && *p <= 'Z' )
	    *p += 'a' - 'A';
}

static void plugins_load_cpi(cpi_t *cpi)
{
#ifdef HAVE_ROARAUDIO
    struct roar_scheduler_source * source = roar_mm_malloc(sizeof(*source));
    char protoname[80];
    plugin_t plugin;

    if (!source)
        return;

    memset(source, 0, sizeof(*source));
    source->flags = ROAR_SCHEDULER_FLAG_FREE;
    source->type  = ROAR_SCHEDULER_CPI_LISTEN;
    source->handle.cpi.proto = roar_str2proto(cpi->protocol);

    source->vio   = roar_mm_malloc(sizeof(struct roar_vio_calls));
    if (!source->vio)
    {
        roar_mm_free(source);
	return;
    }

    if ( roar_vio_open_socket_listen(source->vio, ROAR_SOCKET_TYPE_UNKNOWN, cpi->host, cpi->port) == -1 )
    {
        roar_mm_free(source->vio);
        roar_mm_free(source);
	return;
    }

    source->vio->flags |= ROAR_VIO_FLAGS_FREESELF;

    roar_scheduler_source_add(sched, source);

    if ((source->flags & ROAR_SCHEDULER_FLAG_STUB) && cpi->autoload)
    {
        snprintf (protoname, sizeof(protoname), "protocol-%s", roar_proto2str(source->handle.cpi.proto));
	to_lower(protoname);
	memset(&plugin, 0, sizeof(plugin));
	plugin.name = protoname;
	plugins_load_one(&plugin);
    }
#else
    ERROR0("Can not load CPI as RoarAudio support is not compiled in.");
#endif
}

void plugins_load(ice_config_t * config)
{
    static int done = 0;
    int need_release = 0;
    plugin_t *p_next;
    cpi_t *c_next;

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
    p_next = config->plugins;
    while (p_next)
    {
	plugins_load_one(p_next);
        p_next = p_next->next;
    }
    c_next = config->cpis;
    while (c_next)
    {
        plugins_load_cpi(c_next);
        c_next = c_next->next;
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
