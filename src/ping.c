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

#include "thread/thread.h"

#include "ping.h"
#include "logging.h"
#include "curl.h"

#define CATMODULE "ping"

static bool ping_running = false;
static thread_type *_ping_thread_id;
static mutex_t _ping_mutex;

static void *_ping_thread(void *arg)
{
    CURLM *curl_multi = curl_multi_init();

    while (ping_running) {
        break;
    }

    curl_multi_cleanup(curl_multi);

    return NULL;
}

void ping_initialize(void)
{
    if (ping_running)
        return;

    thread_mutex_create(&_ping_mutex);

    ping_running = true;
    _ping_thread_id = thread_create("Ping Thread", _ping_thread, NULL, THREAD_ATTACHED);
}

void ping_shutdown(void)
{
    if (!ping_running)
        return;

    ping_running = false;
    ICECAST_LOG_DEBUG("waiting for ping thread");
    thread_join(_ping_thread_id);
    thread_mutex_destroy(&_ping_mutex);
}
