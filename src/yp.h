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

#include <stdio.h>

#define  YP_SERVER_NAME 1
#define  YP_SERVER_DESC 2
#define  YP_SERVER_GENRE 3
#define  YP_SERVER_URL 4
#define  YP_BITRATE 5
#define  YP_AUDIO_INFO 6
#define  YP_SERVER_TYPE 7
#define  YP_CURRENT_SONG 8

struct source_tag;

#define YP_ADD_ALL -1
typedef struct ypdata_tag
{
    char *sid;
    char *server_name;
    char *server_desc;
    char *server_genre;
    char *cluster_password;
    char *server_url;
    char *listen_url;
    char *bitrate;
    char *audio_info;
    char *server_type;
    char *current_song;
    char *yp_url;
    int    yp_url_timeout;
    long yp_last_touch;
    int    yp_touch_interval;
} ypdata_t;

void *yp_touch_thread(void *arg);
int yp_add(struct source_tag *source, int which);
int yp_touch();
int yp_remove(struct source_tag *psource);
ypdata_t *yp_create_ypdata();
void yp_destroy_ypdata(ypdata_t *ypdata);
void add_yp_info(struct source_tag *source, char *stat_name, void *info, 
     int type);
#ifdef USE_YP
void yp_recheck_config (ice_config_t *config);
#else
#define yp_recheck_config(x)  do{}while(0)
#endif
void yp_initialize();

#endif


