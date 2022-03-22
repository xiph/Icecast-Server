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

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifndef __YP_H__
#define __YP_H__

#include "icecasttypes.h"

#ifdef USE_YP
void yp_add (const char *mount);
void yp_remove (const char *mount);
void yp_touch (const char *mount);
void yp_recheck_config (ice_config_t *config);
void yp_initialize(void);
void yp_shutdown(void);

#else

#define yp_add(x)               do{}while(0)
#define yp_remove(x)            do{}while(0)
#define yp_touch(x)             do{}while(0)
#define yp_recheck_config(x)    do{}while(0)
#define yp_initialize()         ICECAST_LOG_WARN("YP server handling has been disabled")
#define yp_shutdown()           do{}while(0)

#endif /* USE_YP */

#endif


