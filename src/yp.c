#include <stdio.h>
#include <string.h>

#include <thread/thread.h>

#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "format.h"
#include "geturl.h"
#include "source.h"
#include "config.h"

#define CATMODULE "yp" 

int yp_submit_url(int curl_con, char *yp_url, char *url, char *type)
{
    int ret = 0;

    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_URL, yp_url);
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_POSTFIELDS, url);
    curl_easy_setopt(curl_get_handle(curl_con), CURLOPT_TIMEOUT, config_get_config()->yp_url_timeout);

    /* get it! */
    memset(curl_get_result(curl_con), '\000', sizeof(struct curl_memory_struct));
    memset(curl_get_header_result(curl_con), '\000', sizeof(struct curl_memory_struct2));

    curl_easy_perform(curl_get_handle(curl_con));

    curl_print_header_result(curl_get_header_result(curl_con));

    if (curl_get_header_result(curl_con)->response == ACK) {
        INFO2("Successfull ACK from %s (%s)", type, yp_url);
        ret = 1;
    }
    else {
        if (strlen(curl_get_header_result(curl_con)->message) > 0) {
            ERROR3("Got a NAK from %s(%s) (%s)", type,curl_get_header_result(curl_con)->message, yp_url);
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
    yp_touch((source_t *)arg);
    thread_exit(0);
    return NULL;
}
int yp_remove(void *psource)
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    char *p1 = NULL;
    int i = 0;

    int regen_sid = 0;
    long current_time = 0;
    source_t *source = (source_t *)psource;

    current_time = time(&current_time);

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
            url = (char *)malloc(url_size);
            memset(url, '\000', url_size);
            sprintf(url, "action=remove&sid=%s", 
            source->ypdata[i]->sid);
            curl_con = curl_get_connection();
            if (curl_con < 0) {
                ERROR0("Unable to get auth connection");
            }
            else {
            /* specify URL to get */
                ret = yp_submit_url(curl_con, source->ypdata[i]->yp_url, url, "yp_remove");
            }
            if (url) {
                free(url);
            }
            curl_release_connection(curl_con);
        }
    }
    return 1;
}
int yp_touch(void *psource)
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    char *p1 = NULL;
    int i = 0;
    int regen_sid = 0;
    long current_time = 0;
    source_t *source = (source_t *)psource;

    current_time = time(&current_time);
    for (i=0; i<source->num_yp_directories; i++) {
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
            if (!yp_add(source, i)) {
                return 0;
            }
        }
        if (source->ypdata) {
            url_size = strlen("action=touch&sid=&st=&listeners=") + 1;
            if (source->ypdata[i]->current_song) {
                url_size += strlen(source->ypdata[i]->current_song);
            }
            else {
                source->ypdata[i]->current_song = (char *)malloc(1);
                memset(source->ypdata[i]->current_song, '\000', 1);
            }
            if (source->ypdata[i]->sid) {
                url_size += strlen(source->ypdata[i]->sid);
            }
            else {
                source->ypdata[i]->sid = (char *)malloc(1);
                memset(source->ypdata[i]->sid, '\000', 1);
            }
            url_size += 1024;
            url = (char *)malloc(url_size);
            memset(url, '\000', url_size);
            sprintf(url, "action=touch&sid=%s&st=%s&listeners=%d", 
                        source->ypdata[i]->sid,
                        source->ypdata[i]->current_song,
                        source->listeners);

            curl_con = curl_get_connection();
            if (curl_con < 0) {
                ERROR0("Unable to get auth connection");
            }
            else {
            /* specify URL to get */
                ret = yp_submit_url(curl_con, source->ypdata[i]->yp_url, url, "yp_touch");
                if (!ret) {
                    source->ypdata[i]->sid[0] = '\000';
                }
            }
           if (url) {
               free(url);
           } 
           curl_release_connection(curl_con);
        }
    }
    return 1;
}
int yp_add(void *psource, int which)
{
    char *url = NULL;
    int url_size = 0;
    int ret = 0;
    int curl_con = 0;
    char *p1 = NULL;
    int i = 0;
    int ok = 0;
    source_t *source = (source_t *)psource;

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
                url_size = strlen("action=add&sn=&genre=&cpswd=&desc=&url=&listenurl=&type=&b=") + 1;
                if (source->ypdata[i]->server_name) {
                    url_size += strlen(source->ypdata[i]->server_name);
                }
                else {
                    source->ypdata[i]->server_name = (char *)malloc(1);
                    memset(source->ypdata[i]->server_name, '\000', 1);
                }
                if (source->ypdata[i]->server_desc) {
                    url_size += strlen(source->ypdata[i]->server_desc);
                }
                else {
                    source->ypdata[i]->server_desc = (char *)malloc(1);
                    memset(source->ypdata[i]->server_desc, '\000', 1);
                }
                if (source->ypdata[i]->server_genre) {
                    url_size += strlen(source->ypdata[i]->server_genre);
                }
                else {
                    source->ypdata[i]->server_genre = (char *)malloc(1);
                    memset(source->ypdata[i]->server_genre, '\000', 1);
                }
                if (source->ypdata[i]->cluster_password) {
                    url_size += strlen(source->ypdata[i]->cluster_password);
                }
                else {
                    source->ypdata[i]->cluster_password = (char *)malloc(1);
                    memset(source->ypdata[i]->cluster_password, '\000', 1);
                }
                if (source->ypdata[i]->server_url) {
                    url_size += strlen(source->ypdata[i]->server_url);
                }
                else {
                    source->ypdata[i]->server_url = (char *)malloc(1);
                    memset(source->ypdata[i]->server_url, '\000', 1);
                }
                if (source->ypdata[i]->listen_url) {
                    url_size += strlen(source->ypdata[i]->listen_url);
                }
                else {
                    source->ypdata[i]->listen_url = (char *)malloc(1);
                    memset(source->ypdata[i]->listen_url, '\000', 1);
                }
                if (source->ypdata[i]->server_type) {
                    url_size += strlen(source->ypdata[i]->server_type);
                }
                else {
                    source->ypdata[i]->server_type = (char *)malloc(1);
                    memset(source->ypdata[i]->server_type, '\000', 1);
                }
                if (source->ypdata[i]->bitrate) {
                    url_size += strlen(source->ypdata[i]->bitrate);
                }
                else {
                    source->ypdata[i]->bitrate = (char *)malloc(1);
                    memset(source->ypdata[i]->bitrate, '\000', 1);
                }
                if (source->ypdata[i]->current_song) {
                    url_size += strlen(source->ypdata[i]->current_song);
                }
                else {
                    source->ypdata[i]->current_song = (char *)malloc(1);
                    memset(source->ypdata[i]->current_song, '\000', 1);
                }
                url_size += 1024;
                url = (char *)malloc(url_size);
                memset(url, '\000', url_size);
                sprintf(url, "action=add&sn=%s&genre=%s&cpswd=%s&desc=%s&url=%s&listenurl=%s&type=%s&b=%s", 
                    source->ypdata[i]->server_name,
                    source->ypdata[i]->server_genre,
                    source->ypdata[i]->cluster_password,
                    source->ypdata[i]->server_desc,
                    source->ypdata[i]->server_url,
                    source->ypdata[i]->listen_url,
                    source->ypdata[i]->server_type,
                    source->ypdata[i]->bitrate);

               curl_con = curl_get_connection();
               if (curl_con < 0) {
                   ERROR0("Unable to get auth connection");
               }
               else {
                   /* specify URL to get */
                   ret = yp_submit_url(curl_con, source->ypdata[i]->yp_url, url, "yp_add");

                   if (ret) {
                       if (strlen(curl_get_header_result(curl_con)->sid) > 0) {
                           if (source->ypdata) {
                               if (source->ypdata[i]->sid) {
                                   free(source->ypdata[i]->sid);
                                   source->ypdata[i]->sid = NULL;
                               }
                               source->ypdata[i]->sid = (char *)malloc(strlen(curl_get_header_result(curl_con)->sid) +1);
                               memset(source->ypdata[i]->sid, '\000', strlen(curl_get_header_result(curl_con)->sid) +1);
                               strcpy(source->ypdata[i]->sid, curl_get_header_result(curl_con)->sid);
                               source->ypdata[i]->yp_touch_freq = curl_get_header_result(curl_con)->touch_freq;
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
    ypdata_t *tmp;

    tmp = (ypdata_t *)malloc(sizeof(ypdata_t));
    memset(tmp, '\000', sizeof(ypdata_t));
    return(tmp);
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
        free(ypdata);
    }
}
