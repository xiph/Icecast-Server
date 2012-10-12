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
static struct roar_plugincontainer * container = NULL;
#endif

void plugins_initialize(void)
{
#ifdef HAVE_ROARAUDIO
    roarapi_lock();
    container = roar_plugincontainer_new_simple(ICECAST_HOST_VERSION_STRING, PACKAGE_VERSION);
    roar_plugincontainer_set_autoappsched(container, 1);
    roarapi_unlock();
#endif
}

void plugins_shutdown(void)
{
#ifdef HAVE_ROARAUDIO
    roarapi_lock();
    roar_plugincontainer_unref(container);
    container = NULL;
    roarapi_unlock();
#endif
}

static void plugins_load_one(plugin_t *plugin)
{
#ifdef HAVE_ROARAUDIO
    struct roar_dl_librarypara * para = NULL;

    if (plugin->args)
    {
        para = roar_dl_para_new(plugin->args, NULL, ICECAST_HOST_VERSION_STRING, PACKAGE_VERSION);
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
