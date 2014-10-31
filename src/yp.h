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
#ifndef __YP_H__
#define __YP_H__

#define  YP_SERVER_NAME 1
#define  YP_SERVER_DESC 2
#define  YP_SERVER_GENRE 3
#define  YP_SERVER_URL 4
#define  YP_BITRATE 5
#define  YP_AUDIO_INFO 6
#define  YP_SERVER_TYPE 7
#define  YP_CURRENT_SONG 8
#define  YP_CLUSTER_PASSWORD 9
#define  YP_SUBTYPE 10 

#define YP_ADD_ALL -1

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


