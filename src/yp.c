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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <thread/thread.h>

#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "format.h"
#include "geturl.h"
#include "source.h"
#include "cfgfile.h"
#include "stats.h"

#define CATMODULE "yp" 

static int    yp_url_timeout [MAX_YP_DIRECTORIES];

void yp_recheck_config (ice_config_t *config)
{
    memcpy (&config->yp_url_timeout[0], yp_url_timeout, (sizeof yp_url_timeout));
}

void yp_initialize()
{
    ice_config_t *config = config_get_config();
    yp_recheck_config (config);
    config_release_config ();
    thread_create("YP Touch Thread", yp_touch_thread,
                            (void *)NULL, THREAD_DETACHED);
}
static int yp_submit_url(int curl_con, char *yp_url, char *url, char *type, 
    int timeout)
{
    int ret = 0;
    /* If not specified, use a reasonable timeout
    of 30 seconds */
    if (timeout == 0) {
        timeout = 30;
    }
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_URL, yp_url);
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_POSTFIELDS, url);
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_TIMEOUT, timeout);
    /* This is to force libcurl to not use signals for timeouts */
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_NOSIGNAL, 1);
    /* get it! */
    memset(curl_get_result(curl_con), 0, sizeof(struct curl_memory_struct));
    memset(curl_get_header_result(curl_con), 0, 
            sizeof(struct curl_memory_struct2));

    curl_easy_perform(curl_get_handle(curl_con));

    curl_print_header_result(curl_get_header_result(curl_con));

    if (curl_get_header_result(curl_con)->response == ACK) {
        INFO2("Successfull ACK from %s (%s)", type, yp_url);
        ret = 1;
    }
    else {
        if (strlen(curl_get_header_result(curl_con)->message) > 0) {
            ERROR3("Got a NAK from %s(%s) (%s)", type,
                    curl_get_header_result(curl_con)->message, yp_url);
        }
        else {
            ERROR2("Got a NAK from %s(Unknown) (%s)", type, yp_url);
        }
        ret = 0;
    }
    return ret;
}

void *yp_touch_thread(void *arg)
{
    yp_touch();
    return NULL;
}

int yp_remove(source_t *source)
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    int i = 0;

    time_t current_time = 0;

    current_time = time(NULL);

    for (i=0; i<source->num_yp_directories; i++) {
        source->ypdata[i]->yp_last_touch = current_time;
        if (source->ypdata[i]->sid == 0) {
            return 0;
        }
        else {
            if (strlen(source->ypdata[i]->sid) == 0) {
                return 0;
            }
        }
        if (source->ypdata) {
            url_size = strlen("action=remove&sid=") + 1;
            url_size += strlen(source->ypdata[i]->sid);
            url_size += 1024;
            url = malloc(url_size);
            sprintf(url, "action=remove&sid=%s", 
                    source->ypdata[i]->sid);
            curl_con = curl_get_connection();
            if (curl_con < 0) {
                ERROR0("Unable to get auth connection");
            }
            else {
                /* specify URL to get */
                ret = yp_submit_url(curl_con, source->ypdata[i]->yp_url, 
                        url, "yp_remove", yp_url_timeout[i]);
            }
            if (url) {
                free(url);
            }
            curl_release_connection(curl_con);
        }
    }
    return 1;
}
int yp_touch()
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    int i = 0;
    int regen_sid = 0;
    time_t  current_time = 0;
    avl_node *node;
    source_t *source;
    char current_song[256];
    char tyme[128];
    char *s;

    while (global.running == ICE_RUNNING) {
    avl_tree_rlock(global.source_tree);
    node = avl_get_first(global.source_tree);
    while (node) {
        source = (source_t *)node->key;
        current_time = time(NULL);
        if (!source->yp_public) {
            node = avl_get_next(node);
            continue;
        }
        for (i=0; i<source->num_yp_directories; i++) {
            if (current_time > (source->ypdata[i]->yp_last_touch +
                        source->ypdata[i]->yp_touch_interval)) {
                current_song[0] = 0;
                regen_sid = 0;
                if ((s = (char *)stats_get_value(source->mount, "artist"))) {
                    strncat(current_song, s,
                            sizeof(current_song) - 1);
                    if (strlen(current_song) + 4 <
                            sizeof(current_song))
                    {
                        strncat(current_song, " - ", 3);
                    }
                    if (s) {
                        free(s);
                    }
                }
                if ((s = (char *)stats_get_value(source->mount, "title"))) {
                    if (strlen(current_song) + strlen(s)
                            < sizeof(current_song) -1)
                    {
                        strncat(current_song,
                                s,
                                sizeof(current_song) - 1 -
                                strlen(current_song));
                    }
                    if (s) {
                        free(s);
                    }
                }
                add_yp_info(source, "current_song", current_song, 
                    YP_CURRENT_SONG);

                source->ypdata[i]->yp_last_touch = current_time;
                if (source->ypdata[i]->sid == 0) {
                    regen_sid = 1;
                }
                else {
                    if (strlen(source->ypdata[i]->sid) == 0) {
                        regen_sid = 1;
                    }
                }
                if (regen_sid) {
                    yp_add(source, i);
                }
                if (source->ypdata[i]->sid != 0) {
                    if (strlen(source->ypdata[i]->sid) != 0) {
                        if (source->ypdata) {
                            struct tm tm;
                            url_size = 
                                strlen("action=touch&sid=&st=&listeners=") + 1;
                            if (source->ypdata[i]->current_song) {
                                url_size += 
                                strlen(source->ypdata[i]->current_song);
                            }
                            else {
                                source->ypdata[i]->current_song = 
                                    (char *)malloc(1);
                                source->ypdata[i]->current_song[0] = 0;
                            }
                            if (source->ypdata[i]->sid) {
                                url_size += strlen(source->ypdata[i]->sid);
                            }
                            else {
                                source->ypdata[i]->sid = (char *)malloc(1);
                                source->ypdata[i]->sid[0] = 0;
                            }
                            url_size += 1024;
                            url = malloc(url_size);
                            sprintf(url, 
                                "action=touch&sid=%s&st=%s&listeners=%ld", 
                                source->ypdata[i]->sid,
                                source->ypdata[i]->current_song,
                                source->listeners);
            
                            curl_con = curl_get_connection();
                            if (curl_con < 0) {
                                ERROR0("Unable to get auth connection");
                            }
                            else {
                                /* specify URL to get */
                                ret = yp_submit_url(curl_con, 
                                    source->ypdata[i]->yp_url, 
                                    url, "yp_touch", yp_url_timeout[i]);
                                if (!ret) {
                                    source->ypdata[i]->sid[0] = 0;
                                }
                            }
                            if (url) {
                               free(url);
                            } 
                            curl_release_connection(curl_con);
                            memset(tyme, '\000', sizeof(tyme));
                            localtime_r (&current_time, &tm);
                            strftime(tyme, 128, "%Y-%m-%d  %H:%M:%S", &tm); 
                            stats_event(source->mount, "yp_last_touch", tyme);
                            source->ypdata[i]->yp_last_touch = current_time;
                        }
                    }
                }
            }
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(global.source_tree);
    thread_sleep(200000);
    }


    return 1;
}
int yp_add(source_t *source, int which)
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    int i = 0;
    int ok = 0;

    for (i=0; i<source->num_yp_directories; i++) {
        if (which != -1) {
            if (i == which) {
                ok = 1;
            }
            else {
                ok = 0;
            }
        }
        else {
            ok = 1;
        }

        if (ok) {
            if (source->ypdata[i]) {
                url_size = strlen("action=add&sn=&genre=&cpswd="
                                  "&desc=&url=&listenurl=&type=&b=&")
                                  + 1;
                if (source->ypdata[i]->server_name) {
                    url_size += strlen(source->ypdata[i]->server_name);
                }
                else {
                    source->ypdata[i]->server_name = (char *)malloc(1);
                    source->ypdata[i]->server_name[0] = 0;
                }
                if (source->ypdata[i]->server_desc) {
                    url_size += strlen(source->ypdata[i]->server_desc);
                }
                else {
                    source->ypdata[i]->server_desc = (char *)malloc(1);
                    source->ypdata[i]->server_desc[0] = 0;
                }
                if (source->ypdata[i]->server_genre) {
                    url_size += strlen(source->ypdata[i]->server_genre);
                }
                else {
                    source->ypdata[i]->server_genre = (char *)malloc(1);
                    source->ypdata[i]->server_genre[0] = 0;
                }
                if (source->ypdata[i]->cluster_password) {
                    url_size += strlen(source->ypdata[i]->cluster_password);
                }
                else {
                    source->ypdata[i]->cluster_password = (char *)malloc(1);
                    source->ypdata[i]->cluster_password[0] = 0;
                }
                if (source->ypdata[i]->server_url) {
                    url_size += strlen(source->ypdata[i]->server_url);
                }
                else {
                    source->ypdata[i]->server_url = (char *)malloc(1);
                    source->ypdata[i]->server_url[0] = 0;
                }
                if (source->ypdata[i]->listen_url) {
                    url_size += strlen(source->ypdata[i]->listen_url);
                }
                else {
                    source->ypdata[i]->listen_url = (char *)malloc(1);
                    source->ypdata[i]->listen_url[0] = 0;
                }
                if (source->ypdata[i]->server_type) {
                    url_size += strlen(source->ypdata[i]->server_type);
                }
                else {
                    source->ypdata[i]->server_type = (char *)malloc(1);
                    source->ypdata[i]->server_type[0] = 0;
                }
                if (source->ypdata[i]->bitrate) {
                    url_size += strlen(source->ypdata[i]->bitrate);
                }
                else {
                    source->ypdata[i]->bitrate = (char *)malloc(1);
                    source->ypdata[i]->bitrate[0] = 0;
                }
                if (source->ypdata[i]->current_song) {
                    url_size += strlen(source->ypdata[i]->current_song);
                }
                else {
                    source->ypdata[i]->current_song = (char *)malloc(1);
                    source->ypdata[i]->current_song[0] = 0;
                }
                if (source->ypdata[i]->audio_info) {
                    url_size += strlen(source->ypdata[i]->audio_info);
                }
                else {
                    source->ypdata[i]->audio_info = (char *)malloc(1);
                    source->ypdata[i]->audio_info[0] = 0;
                }

                url_size += 1024;
                url = malloc(url_size);
                sprintf(url, "action=add&sn=%s&genre=%s&cpswd=%s&desc="
                             "%s&url=%s&listenurl=%s&type=%s&b=%s&%s", 
                        source->ypdata[i]->server_name,
                        source->ypdata[i]->server_genre,
                        source->ypdata[i]->cluster_password,
                        source->ypdata[i]->server_desc,
                        source->ypdata[i]->server_url,
                        source->ypdata[i]->listen_url,
                        source->ypdata[i]->server_type,
                        source->ypdata[i]->bitrate,
                        source->ypdata[i]->audio_info);

               curl_con = curl_get_connection();
               if (curl_con < 0) {
                   ERROR0("Unable to get auth connection");
               }
               else {
                   /* specify URL to get */
                   ret = yp_submit_url(curl_con, source->ypdata[i]->yp_url, 
                           url, "yp_add", yp_url_timeout[i]);

                   if (ret) {
                       if (strlen(curl_get_header_result(curl_con)->sid) > 0) {
                           if (source->ypdata) {
                               if (source->ypdata[i]->sid) {
                                   free(source->ypdata[i]->sid);
                                   source->ypdata[i]->sid = NULL;
                               }
                               source->ypdata[i]->sid = (char *)malloc(
                                      strlen(curl_get_header_result(curl_con)->
                                           sid) +1);
                               strcpy(source->ypdata[i]->sid, 
                                       curl_get_header_result(curl_con)->sid);
                               source->ypdata[i]->yp_touch_interval = 
                                   curl_get_header_result(
                                           curl_con)->touch_interval;
                           }
                       }
                   }
               }
               if (url) {
                   free(url);
               }
               curl_release_connection(curl_con);
            }
        }
    }
    return 1;
}

ypdata_t *yp_create_ypdata()
{
    return calloc(1, sizeof(ypdata_t));
}

void yp_destroy_ypdata(ypdata_t *ypdata)
{
    if (ypdata) {
        if (ypdata->sid) {
            free(ypdata->sid);
        }
        if (ypdata->server_name) {
            free(ypdata->server_name);
        }
        if (ypdata->server_desc) {
            free(ypdata->server_desc);
        }
        if (ypdata->server_genre) {
            free(ypdata->server_genre);
        }
        if (ypdata->cluster_password) {
            free(ypdata->cluster_password);
        }
        if (ypdata->server_url) {
            free(ypdata->server_url);
        }
        if (ypdata->listen_url) {
            free(ypdata->listen_url);
        }
        if (ypdata->current_song) {
            free(ypdata->current_song);
        }
        if (ypdata->bitrate) {
            free(ypdata->bitrate);
        }
        if (ypdata->server_type) {
            free(ypdata->server_type);
        }
        if (ypdata->audio_info) {
            free(ypdata->audio_info);
        }
        free(ypdata);
    }
}

void add_yp_info(source_t *source, char *stat_name, 
            void *info, int type)
{
    char *escaped;
    int i;
    if (!info) {
        return;
    }
    for (i=0;i<source->num_yp_directories;i++) {
        switch (type) {
        case YP_SERVER_NAME:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->server_name) {
                        free(source->ypdata[i]->server_name);
                    }
                    source->ypdata[i]->server_name = 
                         malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->server_name, (char *)escaped);
                    stats_event(source->mount, stat_name, (char *)info);
                    free(escaped);
                }
                break;
        case YP_SERVER_DESC:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->server_desc) {
                        free(source->ypdata[i]->server_desc);
                    }
                    source->ypdata[i]->server_desc = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->server_desc, (char *)escaped);
                    stats_event(source->mount, stat_name, (char *)info);
                    free(escaped);
                }
                break;
        case YP_SERVER_GENRE:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->server_genre) {
                        free(source->ypdata[i]->server_genre);
                    }
                    source->ypdata[i]->server_genre = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->server_genre, (char *)escaped);
                    stats_event(source->mount, stat_name, (char *)info);
                    free(escaped);
                }
                break;
        case YP_SERVER_URL:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->server_url) {
                        free(source->ypdata[i]->server_url);
                    }
                    source->ypdata[i]->server_url = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->server_url, (char *)escaped);
                    stats_event(source->mount, stat_name, (char *)info);
                    free(escaped);
                }
                break;
        case YP_BITRATE:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->bitrate) {
                        free(source->ypdata[i]->bitrate);
                    }
                    source->ypdata[i]->bitrate = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->bitrate, (char *)escaped);
                    stats_event(source->mount, stat_name, (char *)info);
                    free(escaped);
                }
                break;
        case YP_AUDIO_INFO:
                if (source->ypdata[i]->audio_info) {
                    free(source->ypdata[i]->audio_info);
                }
                source->ypdata[i]->audio_info = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->audio_info, (char *)info);
                break;
        case YP_SERVER_TYPE:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->server_type) {
                        free(source->ypdata[i]->server_type);
                    }
                    source->ypdata[i]->server_type = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->server_type, (char *)escaped);
                    free(escaped);
                }
                break;
        case YP_CURRENT_SONG:
                escaped = util_url_escape(info);
                if (escaped) {
                    if (source->ypdata[i]->current_song) {
                        free(source->ypdata[i]->current_song);
                    }
                    source->ypdata[i]->current_song = 
                        malloc(strlen((char *)escaped) +1);
                    strcpy(source->ypdata[i]->current_song, (char *)escaped);
                    stats_event(source->mount, "yp_currently_playing", 
                        (char *)info);
                    free(escaped);
                }
                break;
        }
    }
}
