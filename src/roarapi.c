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
#include "logging.h"

#define CATMODULE "roarapi" 

static mutex_t _roarapi_mutex;

void roarapi_initialize(void)
{
    LOG_DEBUG("RoarAPI is being initialized");
    thread_mutex_create(&_roarapi_mutex);
    LOG_DEBUG("RoarAPI is now initialized");
}

void roarapi_shutdown(void)
{
    LOG_DEBUG("RoarAPI is being shut down");
    thread_mutex_destroy(&_roarapi_mutex);
    LOG_DEBUG("RoarAPI is now shut down");
}

void roarapi_lock(void)
{
    thread_mutex_lock(&_roarapi_mutex);
}

void roarapi_unlock(void)
{
    thread_mutex_unlock(&_roarapi_mutex);
}
