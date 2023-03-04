/* Icecast
 *
 *  Copyright 2023      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "thread/thread.h"

#include "ping.h"
#include "logging.h"
#include "curl.h"

#define CATMODULE "ping"

typedef struct ping_queue_tag ping_queue_t;

struct ping_queue_tag {
    CURL *curl;
    ping_queue_t *next;
};

static bool          ping_running = false;
static thread_type  *ping_thread_id;
static mutex_t       ping_mutex;
static ping_queue_t *ping_queue;
static cond_t        ping_cond;

static void on_done(ping_queue_t *entry)
{
    icecast_curl_free(entry->curl);
    free(entry);
}

static void *ping_thread(void *arg)
{
    CURLM *curl_multi = curl_multi_init();

    while (ping_running) {
        ping_queue_t *take;
        int status;

        thread_mutex_lock(&ping_mutex);
        take = ping_queue;
        ping_queue = NULL;
        thread_mutex_unlock(&ping_mutex);

        while (take) {
            ping_queue_t *entry = take;

            take = take->next;
            entry->next = NULL;

            curl_easy_setopt(entry->curl, CURLOPT_PRIVATE, entry);
            curl_multi_add_handle(curl_multi, entry->curl);
        }

        if (curl_multi_perform(curl_multi, &status) != CURLM_OK)
            break;

        if (!status) {
            if (!ping_running)
                break;
            thread_cond_wait(&ping_cond);
            continue;
        }

        if (curl_multi_wait(curl_multi, NULL, 0, 1000, &status) != CURLM_OK)
            break;

        while (true) {
            struct CURLMsg *m = curl_multi_info_read(curl_multi, &status);

            if (!m)
                break;

            if (m->msg == CURLMSG_DONE) {
                ping_queue_t *entry = NULL;
                CURL *e = m->easy_handle;
                curl_multi_remove_handle(curl_multi, e);
                curl_easy_getinfo(e, CURLINFO_PRIVATE, &entry);
                on_done(entry);
            }
        }
    }

    curl_multi_cleanup(curl_multi);

    return NULL;
}

static void ping_add_to_queue(ping_queue_t *entry)
{
    thread_mutex_lock(&ping_mutex);
    entry->next = ping_queue;
    ping_queue = entry;
    thread_mutex_unlock(&ping_mutex);
    thread_cond_broadcast(&ping_cond);
}

void ping_simple(const char *url, const char *username, const char *password, const char *data)
{
    ping_queue_t *entry = calloc(1, sizeof(*entry));

    if (!entry)
        return;

    entry->curl = icecast_curl_new(url, NULL);
    if (!entry->curl) {
        free(entry);
        return;
    }

    if (strchr(url, '@') == NULL) {
        if (username)
            curl_easy_setopt(entry->curl, CURLOPT_USERNAME, username);
        if (password)
            curl_easy_setopt(entry->curl, CURLOPT_PASSWORD, password);
    }

    if (data)
        curl_easy_setopt(entry->curl, CURLOPT_COPYPOSTFIELDS, data);

    ping_add_to_queue(entry);
}

void ping_initialize(void)
{
    if (ping_running)
        return;

    thread_mutex_create(&ping_mutex);
    thread_cond_create(&ping_cond);

    ping_running = true;
    ping_thread_id = thread_create("Ping Thread", ping_thread, NULL, THREAD_ATTACHED);
}

void ping_shutdown(void)
{
    if (!ping_running)
        return;

    ping_running = false;
    ICECAST_LOG_DEBUG("Waiting for ping thread");
    thread_cond_broadcast(&ping_cond);
    thread_join(ping_thread_id);
    thread_mutex_destroy(&ping_mutex);
    thread_cond_destroy(&ping_cond);
    ICECAST_LOG_DEBUG("Joined ping thread, good job!");
}
