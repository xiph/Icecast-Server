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

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>


#define CATMODULE "geturl" 

static curl_connection curl_connections[NUM_CONNECTIONS];
static mutex_t _curl_mutex;

size_t curl_write_memory_callback(void *ptr, size_t size, 
        size_t nmemb, void *data)
{
    register int realsize = size * nmemb;

    struct curl_memory_struct *mem = (struct curl_memory_struct *)data;

    if ((realsize + mem->size) < YP_RESPONSE_SIZE-1) {
        strncat(mem->memory, ptr, realsize);
    }

    return realsize;
}

size_t curl_header_memory_callback(void *ptr, size_t size, 
        size_t nmemb, void *data)
{
    char *p1 = 0;
    char *p2 = 0;
    int copylen = 0;
    register int realsize = size * nmemb;
    struct curl_memory_struct2 *mem = (struct curl_memory_struct2 *)data;

    if (!strncmp(ptr, "SID: ", strlen("SID: "))) {
        p1 = (char *)ptr + strlen("SID: ");
        p2 = strchr((const char *)p1, '\r');
        memset(mem->sid, '\000', sizeof(mem->sid));
        if (p2) {
            if (p2-p1 > sizeof(mem->sid)-1) {
                copylen = sizeof(mem->sid)-1;
            }
            else {
                copylen = p2-p1;
            }
            strncpy(mem->sid, p1, copylen);
        }
        else {
            strncpy(mem->sid, p1, sizeof(mem->sid)-1);
        }
    }
    if (!strncmp(ptr, "YPMessage: ", strlen("YPMessage: "))) {
        p1 = (char *)ptr + strlen("YPMessage: ");
        p2 = strchr((const char *)p1, '\r');
        memset(mem->message, '\000', sizeof(mem->message));
        if (p2) {
            if (p2-p1 > sizeof(mem->message)-1) {
                copylen = sizeof(mem->message)-1;
            }
            else {
                copylen = p2-p1;
            }
            strncpy(mem->message, p1, copylen);
        }
        else {
            strncpy(mem->message, p1, sizeof(mem->message)-1);
        }
    }
    if (!strncmp(ptr, "TouchFreq: ", strlen("TouchFreq: "))) {
        p1 = (char *)ptr + strlen("TouchFreq: ");
        mem->touch_interval = atoi(p1);
    }
    if (!strncmp(ptr, "YPResponse: ", strlen("YPResponse: "))) {
        p1 = (char *)ptr + strlen("YPResponse: ");
        mem->response = atoi(p1);
    }
    return realsize;
}
int curl_initialize()
{
    int i = 0;
    thread_mutex_create(&_curl_mutex);

    memset(&curl_connections, 0, sizeof(curl_connections));
    for (i=0; i<NUM_CONNECTIONS; i++) {
        curl_connections[i].curl_handle = curl_easy_init();
        curl_easy_setopt(curl_connections[i].curl_handle, 
                CURLOPT_WRITEFUNCTION, curl_write_memory_callback);
        curl_easy_setopt(curl_connections[i].curl_handle, 
                CURLOPT_WRITEHEADER, 
                (void *)&(curl_connections[i].header_result));
        curl_easy_setopt(curl_connections[i].curl_handle, 
                CURLOPT_HEADERFUNCTION, curl_header_memory_callback);
        curl_easy_setopt(curl_connections[i].curl_handle, 
                CURLOPT_FILE, (void *)&(curl_connections[i].result));
    }
    return(1);
}
void curl_shutdown()
{
    int i = 0;
    for (i=0; i<NUM_CONNECTIONS; i++) {
        curl_easy_cleanup(curl_connections[i].curl_handle);
        memset(&(curl_connections[i]), 0, sizeof(curl_connections[i]));
    }
}
int curl_get_connection()
{
    int found = 0;
    int curl_connection = -1;
    int i = 0;
    while (!found) {
        thread_mutex_lock(&_curl_mutex);
        for (i=0; i<NUM_CONNECTIONS; i++) {
            if (!curl_connections[i].in_use) {
                found = 1;
                curl_connections[i].in_use = 1;
                curl_connection = i;
                break;
            }
        }
        thread_mutex_unlock(&_curl_mutex);
#ifdef WIN32
        Sleep(200);
#else
        usleep(200);
#endif
    }
    return(curl_connection);
}
int curl_release_connection(int which)
{
    thread_mutex_lock(&_curl_mutex);
    curl_connections[which].in_use = 0;
    memset(&(curl_connections[which].result), 0, 
                sizeof(curl_connections[which].result));
    memset(&(curl_connections[which].header_result), 0, 
                sizeof(curl_connections[which].header_result));
    thread_mutex_unlock(&_curl_mutex);
    return 1;
}
void curl_print_header_result(struct curl_memory_struct2 *mem) {
    DEBUG1("SID -> (%s)", mem->sid);
    DEBUG1("Message -> (%s)", mem->message);
    DEBUG1("Touch Freq -> (%d)", mem->touch_interval);
    DEBUG1("Response -> (%d)", mem->response);
}


CURL *curl_get_handle(int which)
{
    return curl_connections[which].curl_handle;
}

struct curl_memory_struct *curl_get_result(int which)
{
    return &(curl_connections[which].result);
}

struct curl_memory_struct2 *curl_get_header_result(int which)
{
    return &(curl_connections[which].header_result);
}
