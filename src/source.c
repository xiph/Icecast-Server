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
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <sys/types.h>
#include <ogg/ogg.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#else
#include <winsock2.h>
#include <windows.h>
#endif

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "net/sock.h"

#include "connection.h"
#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "cfgfile.h"
#include "util.h"
#include "source.h"
#include "format.h"
#include "fserve.h"
#include "auth.h"
#include "compat.h"

#undef CATMODULE
#define CATMODULE "source"

#define MAX_FALLBACK_DEPTH 10

mutex_t move_clients_mutex;

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static void _parse_audio_info (source_t *source, const char *s);
static void source_shutdown (source_t *source);
static void process_listeners (source_t *source, int fast_clients_only, int deletion_expected);
#ifdef _WIN32
#define source_run_script(x,y)  WARN0("on [dis]connect scripts disabled");
#else
static void source_run_script (char *command, char *mountpoint);
#endif

/* Allocate a new source with the stated mountpoint, if one already
 * exists with that mountpoint in the global source tree then return
 * NULL.
 */
source_t *source_reserve (const char *mount)
{
    source_t *src = NULL;

    do
    {
        avl_tree_wlock (global.source_tree);
        src = source_find_mount_raw (mount);
        if (src)
        {
            src = NULL;
            break;
        }

        src = calloc (1, sizeof(source_t));
        if (src == NULL)
            break;

        /* make duplicates for strings or similar */
        src->mount = strdup (mount);
        src->avg_bitrate_duration = 60;
        src->listener_send_trigger = 10000;

        thread_mutex_create (&src->lock);

        avl_insert (global.source_tree, src);

    } while (0);

    avl_tree_unlock (global.source_tree);
    return src;
}


/* Find a mount with this raw name - ignoring fallbacks. You should have the
 * global source tree locked to call this.
 */
source_t *source_find_mount_raw(const char *mount)
{
    source_t *source;
    avl_node *node;
    int cmp;

    if (!mount) {
        return NULL;
    }
    /* get the root node */
    node = global.source_tree->root->right;
    
    while (node) {
        source = (source_t *)node->key;
        cmp = strcmp(mount, source->mount);
        if (cmp < 0) 
            node = node->left;
        else if (cmp > 0)
            node = node->right;
        else
            return source;
    }
    
    /* didn't find it */
    return NULL;
}


/* Search for mount, if the mount is there but not currently running then
 * check the fallback, and so on.  Must have a global source lock to call
 * this function.
 */
source_t *source_find_mount (const char *mount)
{
    source_t *source = NULL;
    ice_config_t *config;
    mount_proxy *mountinfo;
    int depth = 0;

    config = config_get_config();
    while (mount && depth < MAX_FALLBACK_DEPTH)
    {
        source = source_find_mount_raw(mount);

        if (source)
        {
            if (source->running || source->on_demand)
                break;
        }

        /* we either have a source which is not active (relay) or no source
         * at all. Check the mounts list for fallback settings
         */
        mountinfo = config_find_mount (config, mount);
        source = NULL;

        if (mountinfo == NULL)
            break;
        mount = mountinfo->fallback_mount;
        depth++;
    }

    config_release_config();
    return source;
}


int source_compare_sources(void *arg, void *a, void *b)
{
    source_t *srca = (source_t *)a;
    source_t *srcb = (source_t *)b;

    return strcmp(srca->mount, srcb->mount);
}


void source_clear_source (source_t *source)
{
    int i;
    ice_config_t *config;
    mount_proxy *mountinfo;
    refbuf_t *p;

    DEBUG1 ("clearing source \"%s\"", source->mount);

    /* log bytes read in access log */
    if (source->client)
    {
        source->client->authenticated = 0;
        if (source->format)
            source->client->con->sent_bytes = source->format->read_bytes;
    }
    client_destroy(source->client);
    source->client = NULL;
    source->parser = NULL;

    if (source->dumpfile)
    {
        INFO1 ("Closing dumpfile for %s", source->mount);
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }

    /* lets drop any listeners still connected */
    i = 0;
    config = config_get_config ();
    mountinfo = config_find_mount (config, source->mount);
    while (source->active_clients)
    {
        client_t *client = source->active_clients;
        source->active_clients = client->next;
        client->next = NULL;
        /* do not count listeners who have joined but haven't done any processing */
        if (client->respcode == 200)
            i++;
        auth_release_listener (client, source->mount, mountinfo);
    }
    config_release_config ();

    if (i)
        stats_event_sub (NULL, "listeners", i);

    source->fast_clients_p = &source->active_clients;

    format_free_plugin (source->format);
    source->format = NULL;

    /* flush out the stream data, we don't want any left over */

    /* first remove the reference for refbufs marked for burst */
    p = source->burst_point;
    while (p)
    {
        refbuf_t *to_go = p;
        p = to_go->next;
        to_go->next = NULL;
        refbuf_release (to_go);
    }
    source->burst_point = NULL;
    /* then the normal queue, there could be listeners still pending */
    p = source->stream_data;
    while (p)
    {
        refbuf_t *to_go = p;
        p = to_go->next;
        to_go->next = NULL;
        if (to_go->_count > 1)
            WARN1 ("buffer is %d", to_go->_count);
        refbuf_release (to_go);
    }
    /* the source holds 2 references on the very latest so that one
     * always exists */
    if (p)
        refbuf_release (p);
    source->stream_data = NULL;
    source->stream_data_tail = NULL;

    source->burst_size = 0;
    source->burst_offset = 0;
    source->queue_size = 0;
    source->queue_size_limit = 0;
    source->listeners = 0;
    source->prev_listeners = 0;
    source->shoutcast_compat = 0;
    source->client_stats_update = 0;
    util_dict_free (source->audio_info);
    source->audio_info = NULL;

    free(source->dumpfilename);
    source->dumpfilename = NULL;

    if (source->intro_file)
    {
        fclose (source->intro_file);
        source->intro_file = NULL;
    }

    source->on_demand_req = 0;
}


/* Remove the provided source from the global tree and free it */
void source_free_source (source_t *source)
{
    DEBUG1 ("freeing source \"%s\"", source->mount);
    avl_tree_wlock (global.source_tree);
    avl_delete (global.source_tree, source, NULL);
    avl_tree_unlock (global.source_tree);

    /* make sure all YP entries have gone */
    yp_remove (source->mount);

    /* There should be no listeners on this mount */
    if (source->active_clients)
        WARN1("active listeners on mountpoint %s", source->mount);

    thread_mutex_destroy (&source->lock);

    free (source->mount);
    free (source);
}


client_t *source_find_client(source_t *source, int id)
{
    client_t fakeclient, *client = NULL;
    connection_t fakecon;

    fakeclient.con = &fakecon;
    fakeclient.con->id = id;

    client = source->active_clients;
    while (client)
    {
        if (_compare_clients (NULL, client, &fakeclient) == 0)
            break;
        client = client->next;
    }

    return client;
}


/* Move clients from source to dest provided dest is running
 * and that the stream format is the same.
 * The only lock that should be held when this is called is the
 * source tree lock
 */
void source_move_clients (source_t *source, source_t *dest)
{
    unsigned long count = 0;
    if (strcmp (source->mount, dest->mount) == 0)
    {
        WARN1 ("src and dst are the same \"%s\", skipping", source->mount);
        return;
    }
    /* we don't want the two write locks to deadlock in here */
    thread_mutex_lock (&move_clients_mutex);
    thread_mutex_lock (&dest->lock);

    /* if the destination is not running then we can't move clients */
    if (dest->running == 0 && dest->on_demand == 0)
    {
        WARN1 ("destination mount %s not running, unable to move clients ", dest->mount);
        thread_mutex_unlock (&dest->lock);
        thread_mutex_unlock (&move_clients_mutex);
        return;
    }

    do
    {
        client_t *leave_list = NULL;

        thread_mutex_lock (&source->lock);

        if (source->on_demand == 0 && source->format == NULL)
        {
            INFO1 ("source mount %s is not available", source->mount);
            break;
        }
        if (source->format && dest->format)
        {
            if (source->format->type != dest->format->type)
            {
                WARN2 ("stream %s and %s are of different types, ignored", source->mount, dest->mount);
                break;
            }
        }

        /* we need to move the active listeners */
        while (source->active_clients)
        {
            client_t *client = source->active_clients;
            source->active_clients = client->next;

            /* don't move known slave relays to streams which are not timed (fallback file) */
            if (dest->client == NULL && client->is_slave)
            {
                client->next = leave_list;
                leave_list = client;
                continue;
            }
            /* when switching a client to a different queue, be wary of the 
             * refbuf it's referring to, if it's http headers then we need
             * to write them so don't release it.
             */
            if (client->check_buffer != format_check_http_buffer)
            {
                client_set_queue (client, NULL);
                client->check_buffer = format_check_file_buffer;
                if (source->client == NULL)
                    client->intro_offset = -1;
            }

            client->next = dest->active_clients;
            dest->active_clients = client;
            count++;
        }
        source->active_clients = leave_list;
        INFO2 ("passing %lu listeners to \"%s\"", count, dest->mount);

        dest->listeners += count;
        source->listeners -= count;
        stats_event_args (source->mount, "listeners", "%lu", source->listeners);

    } while (0);

    thread_mutex_unlock (&source->lock);
    /* see if we need to wake up an on-demand relay */
    if (dest->running == 0 && dest->on_demand && count)
        dest->on_demand_req = 1;

    thread_mutex_unlock (&dest->lock);
    thread_mutex_unlock (&move_clients_mutex);
}

/* Update stats from source processing, this should be called regulary (every
 * few seconds) to keep totals up to date.
 */
static void update_source_stats (source_t *source)
{
    unsigned long incoming_rate = 8 * rate_avg (source->format->in_bitrate);
    unsigned long kbytes_sent = source->bytes_sent_since_update/1024;
    unsigned long kbytes_read = source->bytes_read_since_update/1024;
    time_t now = time(NULL);

    source->format->sent_bytes += kbytes_sent*1024;
    stats_event_args (source->mount, "outgoing_kbitrate", "%ld",
            (8 * rate_avg (source->format->out_bitrate))/1000);
    stats_event_args (source->mount, "incoming_bitrate", "%ld", incoming_rate);
    stats_event_args (source->mount, "total_bytes_read",
            "%"PRIu64, source->format->read_bytes);
    stats_event_args (source->mount, "total_bytes_sent",
            "%"PRIu64, source->format->sent_bytes);
    stats_event_args (source->mount, "total_mbytes_sent",
            "%"PRIu64, source->format->sent_bytes/(1024*1024));
    if (source->client)
        stats_event_args (source->mount, "connected", "%"PRIu64,
                (uint64_t)(now - source->client->con->con_time));
    stats_event_add (NULL, "stream_kbytes_sent", kbytes_sent);
    stats_event_add (NULL, "stream_kbytes_read", kbytes_read);

    source->bytes_sent_since_update %= 1024;
    source->bytes_read_since_update %= 1024;

    if (source->running && source->limit_rate)
    {
        if (incoming_rate >= source->limit_rate)
        {
            /* when throttling, we perform a sleep so that the input is not read as quickly, we
             * don't do precise timing here as this just makes sure the incoming bitrate is not
             * excessive. lower bitrate stream have higher sleep counts */
            float kbits = incoming_rate/8.0f;
            if (kbits < 1200)
                kbits = 1200;
            source->throttle_stream = (int)(1000000 / kbits * 1000);

            /* if bitrate is consistently excessive then terminate the stream */
            if (source->throttle_termination == 0)
            {
                source->throttle_termination = now + source->avg_bitrate_duration/2;
                /* DEBUG1 ("throttle termination set at %ld", source->throttle_termination); */
            }
            else
            {
                if (now >= source->throttle_termination)
                {
                    source->running = 0;
                    WARN3 ("%s terminating, exceeding bitrate limits (%dk/%dk)",
                            source->mount, incoming_rate/1024, source->limit_rate/1024);
                }
            }
        }
        else
        {
            source->throttle_stream = 0;
            source->throttle_termination = 0;
        }
    }
}


/* get some data from the source. The stream data is placed in a refbuf
 * and sent back, however NULL is also valid as in the case of a short
 * timeout and there's no data pending.
 */
static void get_next_buffer (source_t *source)
{
    refbuf_t *refbuf = NULL;
    int no_delay_count = 0;

    while (global.running == ICE_RUNNING && source->running)
    {
        int fds = 0;
        time_t current = time(NULL);
        int delay = 200;

        source->amount_added_to_queue = 0;

        /* service fast clients but jump out once in a while to check on
         * normal clients */
        if (no_delay_count == 10)
            return;

        if (*source->fast_clients_p)
        {
            delay = 0;
            no_delay_count++;
        }

        thread_mutex_unlock (&source->lock);

        if (source->throttle_stream > 0)
            thread_sleep (source->throttle_stream);

        if (source->client)
            fds = util_timed_wait_for_fd (source->client->con->sock, delay);
        else
        {
            thread_sleep (delay*1000);
            source->last_read = current;
        }

        /* take the lock */
        thread_mutex_lock (&source->lock);

        if (source->client && current >= source->client_stats_update)
        {
            update_source_stats (source);
            source->client_stats_update = current + source->stats_interval;
        }
        if (fds < 0)
        {
            if (! sock_recoverable (sock_error()))
            {
                WARN0 ("Error while waiting on socket, Disconnecting source");
                source->running = 0;
            }
            continue;
        }

        if (fds == 0)
        {
            if (source->last_read + (time_t)source->timeout < current)
            {
                DEBUG3 ("last %ld, timeout %d, now %ld", (long)source->last_read,
                        source->timeout, (long)current);
                WARN0 ("Disconnecting source due to socket timeout");
                source->running = 0;
                break;
            }
            if (delay == 0)
            {
                process_listeners (source, 1, 0);
                continue;
            }
            rate_add (source->format->in_bitrate, 0, current);
            break;
        }
        source->last_read = current;
        refbuf = source->format->get_buffer (source);
        if (refbuf)
        {
            source->bytes_read_since_update += refbuf->len;
            source->amount_added_to_queue = refbuf->len;

            /* the latest refbuf is counted twice so that it stays */
            refbuf_addref (refbuf);

            /* append buffer to the in-flight data queue,  */
            if (source->stream_data == NULL)
            {
                source->stream_data = refbuf;
                source->burst_point = refbuf;
                source->burst_offset = 0;
            }
            if (source->stream_data_tail)
            {
                source->stream_data_tail->next = refbuf;
                refbuf_release (source->stream_data_tail);
            }
            source->stream_data_tail = refbuf;
            source->queue_size += refbuf->len;

            /* increase refcount for keeping burst data */
            refbuf_addref (refbuf);

            /* move the starting point for new listeners */
            source->burst_offset += refbuf->len;
            while (source->burst_offset > source->burst_size)
            {
                refbuf_t *to_release = source->burst_point;
                if (to_release->next)
                {
                    source->burst_offset -= to_release->len;
                    source->burst_point = to_release->next;
                    refbuf_release (to_release);
                    continue;
                }
                break;
            }

            /* save stream to file */
            if (source->dumpfile && source->format->write_buf_to_file)
                source->format->write_buf_to_file (source, refbuf);
        }
        else
        {
            if (source->client->con && source->client->con->error)
            {
                INFO1 ("End of Stream %s", source->mount);
                source->running = 0;
            }
        }
        break;
    }
}


/* general send routine per listener.  The deletion_expected tells us whether
 * the last in the queue is about to disappear, so if this client is still
 * referring to it after writing then drop the client as it's fallen too far
 * behind
 *
 * return 1 for fast client, limiter kicked in
 *        0 for normal case.
 */
static int send_to_listener (source_t *source, client_t *client, int deletion_expected)
{
    int bytes;
    int loop = 10;   /* max number of iterations in one go */
    long total_written = 0;
    int ret = 0;

    /* check for limited listener time */
    if (client->con->discon_time && time(NULL) >= client->con->discon_time)
    {
        INFO1 ("time limit reached for client #%lu", client->con->id);
        client->con->error = 1;
        return 0;
    }

    if (source->amount_added_to_queue)
        client->lag += source->amount_added_to_queue;

    while (1)
    {
        /* jump out if client connection has died */
        if (client->con->error)
            break;

        if (loop == 0)
        {
            ret = 0;
            break;
        }
        /* lets not send too much to one client in one go, but don't
           sleep for too long if more data can be sent */
        if (total_written > source->listener_send_trigger)
        {
            ret = 1;
            break;
        }

        loop--;

        if (client->check_buffer (source, client) < 0)
            break;

        bytes = client->write_to_client (client);
        if (bytes <= 0)
            break;  /* can't write any more */

        total_written += bytes;
    }
    rate_add (source->format->out_bitrate, total_written, timing_get_time());
    source->bytes_sent_since_update += total_written;

    global_add_bitrates (global.out_bitrate, total_written);

    /* the refbuf referenced at head (last in queue) may be marked for deletion
     * if so, check to see if this client is still referring to it */
    if (deletion_expected && client->refbuf && client->refbuf == source->stream_data)
    {
        INFO2 ("Client %lu (%s) has fallen too far behind, removing",
                client->con->id, client->con->ip);
        stats_event_inc (source->mount, "slow_listeners");
        client->con->error = 1;
    }
    return ret;
}


/* run through the queue of listeners, the fast ones are at the back of the
 * queue so you may want to process only those. If a buffer is going to be
 * removed from the stream queue then flag it so that listeners can be
 * dropped if need be.
 */
static void process_listeners (source_t *source, int fast_clients_only, int deletion_expected)
{
    client_t *client, **client_p;
    client_t *fast_clients = NULL, **fast_client_tail = &fast_clients;
    unsigned long listener_count = 0;

    /* where do we start from */
    if (fast_clients_only)
        client_p = source->fast_clients_p;
    else
        client_p = &source->active_clients;
    client = *client_p;

    while (client)
    {
        int fast_client = send_to_listener (source, client, deletion_expected);

        if (client->con->error)
        {
            client_t *to_go = client;
            ice_config_t *config;
            mount_proxy *mountinfo;

            *client_p = client->next;
            client = client->next;

            config = config_get_config ();
            mountinfo = config_find_mount (config, source->mount);
            auth_release_listener (to_go, source->mount, mountinfo);
            config_release_config();

            source->listeners--;
            stats_event_dec (NULL, "listeners");
            DEBUG0("Client removed");
            continue;
        }
        listener_count++;
        if (fast_client && client->check_buffer != format_check_file_buffer)
        {
            client_t *to_move = client;

            *client_p = client->next;
            client = client->next;

            to_move->next = NULL;
            *fast_client_tail = to_move;
            fast_client_tail = &to_move->next;
            continue;
        }
        client_p = &client->next;
        client = *client_p;
    }
    source->fast_clients_p = client_p;
    /* place fast clients list at the end */
    if (fast_clients)
        *client_p = fast_clients;

    /* consistency check, these should match */
    if (fast_clients_only == 0 && listener_count != source->listeners)
        ERROR3 ("mount %s has %lu, %lu", source->mount, listener_count, source->listeners);

    /* has the listener count changed */
    if (source->listeners != source->prev_listeners)
    {
        source->prev_listeners = source->listeners;
        INFO2("listener count on %s now %lu", source->mount, source->listeners);
        if (source->listeners > source->peak_listeners)
        {
            source->peak_listeners = source->listeners;
            stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);
        }
        stats_event_args (source->mount, "listeners", "%lu", source->listeners);
        if (source->listeners == 0)
            rate_reduce (source->format->out_bitrate, 0);
        /* change of listener numbers, so reduce scope of global sampling */
        global_reduce_bitrate_sampling (global.out_bitrate);
        /* do we need to shutdown an on-demand relay */
        if (source->listeners == 0 && source->on_demand)
            source->running = 0;
    }
}


/* Perform any initialisation before the stream data is processed, the header
 * info is processed by now and the format details are setup
 */
static void source_init (source_t *source)
{
    mount_proxy *mountinfo;

    if (source->dumpfilename != NULL)
    {
        INFO2 ("dumpfile \"%s\" for %s", source->dumpfilename, source->mount);
        source->dumpfile = fopen (source->dumpfilename, "ab");
        if (source->dumpfile == NULL)
        {
            WARN2("Cannot open dump file \"%s\" for appending: %s, disabling.",
                    source->dumpfilename, strerror(errno));
        }
    }

    /* grab a read lock, to make sure we get a chance to cleanup */
    thread_rwlock_rlock (source->shutdown_rwlock);

    /* start off the statistics */
    stats_event_inc (NULL, "source_total_connections");
    stats_event_hidden (source->mount, "slow_listeners", "0", STATS_COUNTERS);
    stats_event_hidden (source->mount, "listener_connections", "0", STATS_COUNTERS);
    stats_event (source->mount, "server_type", source->format->contenttype);
    stats_event_hidden (source->mount, "listener_peak", "0", STATS_COUNTERS);
    stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);
    stats_event_time (source->mount, "stream_start");
    stats_event_hidden (source->mount, "total_mbytes_sent", "0", STATS_COUNTERS);
    stats_event_hidden (source->mount, "total_bytes_sent", "0", STATS_COUNTERS);
    stats_event_hidden (source->mount, "total_bytes_read", "0", STATS_COUNTERS);

    DEBUG0("Source creation complete");
    source->last_read = time(NULL);
    source->prev_listeners = -1;
    source->bytes_sent_since_update = 0;
    source->stats_interval = 5;
    /* so the first set of average stats after 3 seconds */
    source->client_stats_update = source->last_read + 3;

    source->fast_clients_p = &source->active_clients;
    source->audio_info = util_dict_new();
    if (source->client)
    {
        const char *str = httpp_getvar(source->client->parser, "ice-audio-info");
        if (str)
        {
            _parse_audio_info (source, str);
            stats_event (source->mount, "audio_info", str);
        }
    }

    thread_mutex_unlock (&source->lock);

    /* on demand relays should of already called this */
    if (source->on_demand == 0)
        slave_update_all_mounts();

    mountinfo = config_find_mount (config_get_config(), source->mount);
    if (mountinfo)
    {
        if (mountinfo->on_connect)
            source_run_script (mountinfo->on_connect, source->mount);
        auth_stream_start (mountinfo, source->mount);

        /*
         ** Now, if we have a fallback source and override is on, we want
         ** to steal its clients, because it means we've come back online
         ** after a failure and they should be gotten back from the waiting
         ** loop or jingle track or whatever the fallback is used for
         */

        if (mountinfo->fallback_override && mountinfo->fallback_mount)
        {
            source_t *fallback_source;

            avl_tree_rlock(global.source_tree);
            fallback_source = source_find_mount (mountinfo->fallback_mount);

            if (fallback_source)
                source_move_clients (fallback_source, source);

            avl_tree_unlock(global.source_tree);
        }
    }
    config_release_config();

    thread_mutex_lock (&source->lock);

    source->format->in_bitrate = rate_setup (source->avg_bitrate_duration+1, 1);
    source->format->out_bitrate = rate_setup (120, 1000);
    source->running = 1;
}


void source_main (source_t *source)
{
    source_init (source);

    while (global.running == ICE_RUNNING && source->running)
    {
        int remove_from_q;

        get_next_buffer (source);

        remove_from_q = 0;

        /* lets see if we have too much data in the queue, but do not
           remove it until later */
        if (source->queue_size > source->queue_size_limit)
            remove_from_q = 1;

        process_listeners (source, 0, remove_from_q);

        /* lets reduce the queue, any lagging clients should of been
         * terminated by now
         */
        if (source->stream_data)
        {
            /* if we need to prune the queue to keep within the specific limit then
             * all other references to the last block need of been gone by now
             */
            if (remove_from_q && source->stream_data->_count > 1)
                ERROR1 ("unable to prune queue, size is at %ld", source->queue_size);

            /* normal unreferenced queue data will have a refcount 1, but
             * burst queue data will be at least 2, active clients will also
             * increase refcount
             */
            while (source->stream_data->_count == 1)
            {
                refbuf_t *to_go = source->stream_data;

                if (to_go->next == NULL || source->burst_point == to_go)
                {
                    /* this should not happen */
                    ERROR2 ("queue state is unexpected (%p, %d)", to_go->next, 
                            source->burst_point == to_go ? 1: 0);
                    source->running = 0;
                    break;
                }
                source->stream_data = to_go->next;
                source->queue_size -= to_go->len;
                to_go->next = NULL;
                refbuf_release (to_go);
            }
        }
    }
    source->running = 0;

    source_shutdown (source);
}


/* this function is expected to keep lock held on exit */
static void source_shutdown (source_t *source)
{
    mount_proxy *mountinfo;

    source->running = 0;
    INFO1("Source \"%s\" exiting", source->mount);

    update_source_stats (source);
    mountinfo = config_find_mount (config_get_config(), source->mount);
    if (mountinfo)
    {
        if (mountinfo->on_disconnect)
            source_run_script (mountinfo->on_disconnect, source->mount);
        auth_stream_end (mountinfo, source->mount);

        /* we have de-activated the source now, so no more clients will be
         * added, now move the listeners we have to the fallback (if any)
         */
        if (mountinfo->fallback_mount)
        {
            char *mount = strdup (mountinfo->fallback_mount);
            source_t *fallback_source;

            config_release_config();
            avl_tree_rlock (global.source_tree);
            fallback_source = source_find_mount (mount);
            free (mount);

            if (fallback_source != NULL)
            {
                /* be careful wrt to deadlocking */
                thread_mutex_unlock (&source->lock);
                source_move_clients (source, fallback_source);
                thread_mutex_lock (&source->lock);
            }

            avl_tree_unlock (global.source_tree);
        }
    }
    else
        config_release_config();

    /* delete this sources stats */
    stats_event(source->mount, NULL, NULL);

    /* we don't remove the source from the tree here, it may be a relay and
       therefore reserved */
    source_clear_source (source);

    global_reduce_bitrate_sampling (global.out_bitrate);

    global_lock();
    global.sources--;
    stats_event_args (NULL, "sources", "%d", global.sources);
    global_unlock();

    /* release our hold on the lock so the main thread can continue cleaning up */
    thread_rwlock_unlock(source->shutdown_rwlock);
}


static int _compare_clients(void *compare_arg, void *a, void *b)
{
    client_t *clienta = (client_t *)a;
    client_t *clientb = (client_t *)b;

    connection_t *cona = clienta->con;
    connection_t *conb = clientb->con;

    if (cona->id < conb->id) return -1;
    if (cona->id > conb->id) return 1;

    return 0;
}


static void _parse_audio_info (source_t *source, const char *s)
{
    const char *start = s;
    unsigned int len;

    while (start != NULL && *start != '\0')
    {
        if ((s = strchr (start, ';')) == NULL)
            len = strlen (start);
        else
        {
            len = (int)(s - start);
            s++; /* skip passed the ';' */
        }
        if (len)
        {
            char name[100], value[200];
            char *esc;

            sscanf (start, "%99[^=]=%199[^;\r\n]", name, value);
            esc = util_url_unescape (value);
            if (esc)
            {
                if (source->running)
                {
                    util_dict_set (source->audio_info, name, esc);
                    stats_event (source->mount, name, esc);
                }
                free (esc);
            }
        }
        start = s;
    }
}


/* Apply the mountinfo details to the source */
static void source_apply_mount (source_t *source, mount_proxy *mountinfo)
{
    const char *str;
    int val;
    http_parser_t *parser = NULL;

    if (mountinfo == NULL || strcmp (mountinfo->mountname, source->mount) == 0)
        INFO1 ("Applying mount information for \"%s\"", source->mount);
    else
        INFO2 ("Applying mount information for \"%s\" from \"%s\"",
                source->mount, mountinfo->mountname);

    stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);

    /* if a setting is available in the mount details then use it, else
     * check the parser details. */

    if (source->client)
        parser = source->client->parser;

    /* to be done before possible non-utf8 stats */
    if (source->format && source->format->apply_settings)
        source->format->apply_settings (source->client, source->format, mountinfo);

    str = httpp_getvar (parser, "user-agent");
    if (str && source->format)
        stats_event_conv (source->mount, "user_agent", str, source->format->charset);

    /* public */
    if (mountinfo && mountinfo->yp_public >= 0)
        val = mountinfo->yp_public;
    else
    {
        do {
            str = httpp_getvar (parser, "ice-public");
            if (str) break;
            str = httpp_getvar (parser, "icy-pub");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-public");
            if (str) break;
            /* handle header from icecast v2 release */
            str = httpp_getvar (parser, "icy-public");
            if (str) break;
            str = "0";
        } while (0);
        val = atoi (str);
    }
    stats_event_args (source->mount, "public", "%d", val);
    if (source->yp_public != val)
    {
        DEBUG1 ("YP changed to %d", val);
        if (val)
            yp_add (source->mount);
        else
            yp_remove (source->mount);
        source->yp_public = val;
    }

    /* stream name */
    if (mountinfo && mountinfo->stream_name)
        stats_event (source->mount, "server_name", mountinfo->stream_name);
    else
    {
        do {
            str = httpp_getvar (parser, "ice-name");
            if (str) break;
            str = httpp_getvar (parser, "icy-name");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-name");
            if (str) break;
            str = "Unspecified name";
        } while (0);
        if (source->format)
            stats_event_conv (source->mount, "server_name", str, source->format->charset);
    }

    /* stream description */
    if (mountinfo && mountinfo->stream_description)
        stats_event (source->mount, "server_description", mountinfo->stream_description);
    else
    {
        do {
            str = httpp_getvar (parser, "ice-description");
            if (str) break;
            str = httpp_getvar (parser, "icy-description");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-description");
            if (str) break;
        } while (0);
        if (str && source->format)
            stats_event_conv (source->mount, "server_description", str, source->format->charset);
    }

    /* stream URL */
    if (mountinfo && mountinfo->stream_url)
        stats_event (source->mount, "server_url", mountinfo->stream_url);
    else
    {
        do {
            str = httpp_getvar (parser, "ice-url");
            if (str) break;
            str = httpp_getvar (parser, "icy-url");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-url");
            if (str) break;
        } while (0);
        if (str && source->format)
            stats_event_conv (source->mount, "server_url", str, source->format->charset);
    }

    /* stream genre */
    if (mountinfo && mountinfo->stream_genre)
        stats_event (source->mount, "genre", mountinfo->stream_genre);
    else
    {
        do {
            str = httpp_getvar (parser, "ice-genre");
            if (str) break;
            str = httpp_getvar (parser, "icy-genre");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-genre");
            if (str) break;
            str = "various";
        } while (0);
        if (source->format)
            stats_event_conv (source->mount, "genre", str, source->format->charset);
    }

    /* stream bitrate */
    if (mountinfo && mountinfo->bitrate)
        str = mountinfo->bitrate;
    else
    {
        do {
            str = httpp_getvar (parser, "ice-bitrate");
            if (str) break;
            str = httpp_getvar (parser, "icy-br");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-bitrate");
        } while (0);
    }
    stats_event (source->mount, "bitrate", str);

    /* handle MIME-type */
    if (mountinfo && mountinfo->type)
        stats_event (source->mount, "server_type", mountinfo->type);
    else
        if (source->format)
            stats_event (source->mount, "server_type", source->format->contenttype);

    if (mountinfo && mountinfo->subtype)
        stats_event (source->mount, "subtype", mountinfo->subtype);

    if (mountinfo && mountinfo->auth)
        stats_event (source->mount, "authenticator", mountinfo->auth->type);
    else
        stats_event (source->mount, "authenticator", NULL);

    source->limit_rate = 0;
    if (mountinfo && mountinfo->limit_rate)
        source->limit_rate = mountinfo->limit_rate;

    if (mountinfo)
        source->avg_bitrate_duration = mountinfo->avg_bitrate_duration;
    else
        source->avg_bitrate_duration = 60;

    /* needs a better mechanism, probably via a client_t handle */
    free (source->dumpfilename);
    source->dumpfilename = NULL;
    if (mountinfo && mountinfo->dumpfile)
    {
        time_t now = time(NULL);
        struct tm local;
        char buffer[PATH_MAX];

        localtime_r (&now, &local);
        strftime (buffer, sizeof (buffer), mountinfo->dumpfile, &local);
        source->dumpfilename = strdup (buffer);
    }
    /* handle changes in intro file setting */
    if (source->intro_file)
    {
        fclose (source->intro_file);
        source->intro_file = NULL;
    }
    if (mountinfo && mountinfo->intro_filename)
    {
        ice_config_t *config = config_get_config_unlocked ();
        unsigned int len  = strlen (config->webroot_dir) +
            strlen (mountinfo->intro_filename) + 2;
        char *path = malloc (len);
        if (path)
        {
            FILE *f;
            snprintf (path, len, "%s" PATH_SEPARATOR "%s", config->webroot_dir,
                    mountinfo->intro_filename);

            DEBUG1 ("intro file is %s", mountinfo->intro_filename);
            f = fopen (path, "rb");
            if (f)
                source->intro_file = f;
            else
                WARN2 ("Cannot open intro file \"%s\": %s", path, strerror(errno));
            free (path);
        }
    }

    if (mountinfo && mountinfo->queue_size_limit)
        source->queue_size_limit = mountinfo->queue_size_limit;

    if (mountinfo && mountinfo->source_timeout)
        source->timeout = mountinfo->source_timeout;

    if (mountinfo && mountinfo->burst_size >= 0)
        source->burst_size = (unsigned int)mountinfo->burst_size;

    source->wait_time = 0;
    if (mountinfo && mountinfo->wait_time)
        source->wait_time = (time_t)mountinfo->wait_time;
}


/* update the specified source with details from the config or mount.
 * mountinfo can be NULL in which case default settings should be taken
 */
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo)
{
    /*  skip if source is a fallback to file */
    if (source->running && source->client == NULL)
    {
        stats_event_hidden (source->mount, NULL, NULL, STATS_HIDDEN);
        return;
    }
    /* set global settings first */
    source->queue_size_limit = config->queue_size_limit;
    source->timeout = config->source_timeout;
    source->burst_size = config->burst_size;

    stats_event_args (source->mount, "listenurl", "http://%s:%d%s",
            config->hostname, config->port, source->mount);

    source_apply_mount (source, mountinfo);

    if (source->dumpfilename)
        DEBUG1 ("Dumping stream to %s", source->dumpfilename);
    if (source->on_demand)
    {
        DEBUG0 ("on_demand set");
        stats_event (source->mount, "on_demand", "1");
        stats_event_args (source->mount, "listeners", "%ld", source->listeners);
    }
    else
        stats_event (source->mount, "on_demand", NULL);

    if (mountinfo)
    {
        if (mountinfo->on_connect)
            DEBUG1 ("connect script \"%s\"", mountinfo->on_connect);
        if (mountinfo->on_disconnect)
            DEBUG1 ("disconnect script \"%s\"", mountinfo->on_disconnect);
        if (mountinfo->fallback_when_full)
            DEBUG1 ("fallback_when_full to %u", mountinfo->fallback_when_full);
        DEBUG1 ("max listeners to %d", mountinfo->max_listeners);
        stats_event_args (source->mount, "max_listeners", "%d", mountinfo->max_listeners);
        stats_event_hidden (source->mount, "cluster_password", mountinfo->cluster_password, STATS_SLAVE|STATS_HIDDEN);
        if (mountinfo->hidden)
        {
            stats_event_hidden (source->mount, NULL, NULL, STATS_HIDDEN);
            DEBUG0 ("hidden from public");
        }
        else
            stats_event_hidden (source->mount, NULL, NULL, 0);
    }
    else
    {
        DEBUG0 ("max listeners is not specified");
        stats_event (source->mount, "max_listeners", "unlimited");
        stats_event_hidden (source->mount, "cluster_password", NULL, STATS_SLAVE);
        stats_event_hidden (source->mount, NULL, NULL, STATS_PUBLIC);
    }
    DEBUG1 ("public set to %d", source->yp_public);
    DEBUG1 ("queue size to %u", source->queue_size_limit);
    DEBUG1 ("burst size to %u", source->burst_size);
    DEBUG1 ("source timeout to %u", source->timeout);
}


void *source_client_thread (void *arg)
{
    source_t *source = arg;

    stats_event_inc(NULL, "source_client_connections");

    thread_mutex_lock (&source->lock);
    source_main (source);

    if (source->wait_time)
    {
        time_t release = source->wait_time + time(NULL);
        INFO2 ("keeping %s reserved for %d seconds", source->mount, source->wait_time);
        thread_mutex_unlock (&source->lock);
        while (global.running && release >= time(NULL))
            thread_sleep (1000000);
    }
    else
        thread_mutex_unlock (&source->lock);

    source_free_source (source);
    slave_update_all_mounts();

    return NULL;
}


void source_client_callback (client_t *client, void *arg)
{
    source_t *source = arg;
    refbuf_t *old_data = client->refbuf;

    if (client->con->error)
    {
        global_lock();
        global.sources--;
        global_unlock();
        source_clear_source (source);
        source_free_source (source);
        return;
    }
    client->refbuf = old_data->associated;
    old_data->associated = NULL;
    refbuf_release (old_data);
    stats_event (source->mount, "source_ip", source->client->con->ip);

    thread_create ("Source Thread", source_client_thread,
            source, THREAD_DETACHED);
}


#ifndef _WIN32
static void source_run_script (char *command, char *mountpoint)
{
    pid_t pid, external_pid;

    /* do a fork twice so that the command has init as parent */
    external_pid = fork();
    switch (external_pid)
    {
        case 0:
            switch (pid = fork ())
            {
                case -1:
                    ERROR2 ("Unable to fork %s (%s)", command, strerror (errno));
                    break;
                case 0:  /* child */
                    DEBUG1 ("Starting command %s", command);
                    execl (command, command, mountpoint, (char *)NULL);
                    ERROR2 ("Unable to run command %s (%s)", command, strerror (errno));
                    exit(0);
                default: /* parent */
                    break;
            }
            exit (0);
        case -1:
            ERROR1 ("Unable to fork %s", strerror (errno));
            break;
        default: /* parent */
            waitpid (external_pid, NULL, 0);
            break;
    }
}
#endif


static void *source_fallback_file (void *arg)
{
    char *mount = arg;
    char *type;
    char *path;
    unsigned int len;
    FILE *file = NULL;
    source_t *source = NULL;
    ice_config_t *config;
    http_parser_t *parser;

    do
    {
        if (mount == NULL || mount[0] != '/')
            break;
        config = config_get_config();
        len  = strlen (config->webroot_dir) + strlen (mount) + 1;
        path = malloc (len);
        if (path)
            snprintf (path, len, "%s%s", config->webroot_dir, mount);
        
        config_release_config ();
        if (path == NULL)
            break;

        file = fopen (path, "rb");
        if (file == NULL)
        {
            WARN1 ("unable to open file \"%s\"", path);
            free (path);
            break;
        }
        free (path);
        source = source_reserve (mount);
        if (source == NULL)
        {
            WARN1 ("mountpoint \"%s\" already reserved", mount);
            break;
        }
        INFO1 ("mountpoint %s is reserved", mount);
        type = fserve_content_type (mount);
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        httpp_setvar (parser, "content-type", type);
        free (type);

        stats_event_hidden (source->mount, NULL, NULL, STATS_HIDDEN);
        source->yp_public = 0;
        source->intro_file = file;
        source->parser = parser;
        source->avg_bitrate_duration = 20;
        source->listener_send_trigger = 4096;
        file = NULL;

        if (connection_complete_source (source, 0) < 0)
            break;
        source_client_thread (source);
        httpp_destroy (parser);
    } while (0);
    if (file)
        fclose (file);
    free (mount);
    return NULL;
}

static int is_mount_template (const char *mount)
{
    if (strchr (mount, '*') || strchr (mount, '?') || strchr (mount, '['))
        return 1;
    return 0;
}


/* rescan the mount list, so that xsl files are updated to show
 * unconnected but active fallback mountpoints
 */
void source_recheck_mounts (int update_all)
{
    ice_config_t *config = config_get_config();
    mount_proxy *mount = config->mounts;

    avl_tree_rlock (global.source_tree);

    if (update_all)
        stats_clear_virtual_mounts ();

    while (mount)
    {
        source_t *source;

        if (is_mount_template (mount->mountname))
        {
            mount = mount->next;
            continue;
        }
        source = source_find_mount (mount->mountname);

        if (source)
        {
            source = source_find_mount_raw (mount->mountname);
            if (source)
            {
                mount_proxy *mountinfo = config_find_mount (config, source->mount);
                thread_mutex_lock (&source->lock);
                source_update_settings (config, source, mountinfo);
                thread_mutex_unlock (&source->lock);
            }
            else if (update_all)
            {
                stats_event_hidden (mount->mountname, NULL, NULL, mount->hidden?STATS_HIDDEN:0);
                stats_event_args (mount->mountname, "listenurl", "http://%s:%d%s",
                        config->hostname, config->port, mount->mountname);
                stats_event (mount->mountname, "listeners", "0");
                if (mount->max_listeners < 0)
                    stats_event (mount->mountname, "max_listeners", "unlimited");
                else
                    stats_event_args (mount->mountname, "max_listeners", "%d", mount->max_listeners);
            }
        }
        else
            stats_event (mount->mountname, NULL, NULL);

        /* check for fallback to file */
        if (global.running == ICE_RUNNING && mount->fallback_mount)
        {
            source_t *fallback = source_find_mount (mount->fallback_mount);
            if (fallback == NULL)
            {
                thread_create ("Fallback file thread", source_fallback_file,
                        strdup (mount->fallback_mount), THREAD_DETACHED);
            }
        }

        mount = mount->next;
    }
    avl_tree_unlock (global.source_tree);
    config_release_config();
}


void source_startup (client_t *client, const char *uri)
{
    source_t *source;
    source = source_reserve (uri);

    if (source)
    {
        source->client = client;
        source->parser = client->parser;
        if (connection_complete_source (source, 1) < 0)
        {
            source_clear_source (source);
            source_free_source (source);
            return;
        }
        client->respcode = 200;
        if (client->server_conn->shoutcast_compat)
        {
            source->shoutcast_compat = 1;
            source_client_callback (client, source);
        }
        else
        {
            refbuf_t *ok = refbuf_new (PER_CLIENT_REFBUF_SIZE);
            snprintf (ok->data, PER_CLIENT_REFBUF_SIZE,
                    "HTTP/1.0 200 OK\r\n\r\n");
            ok->len = strlen (ok->data);
            /* we may have unprocessed data read in, so don't overwrite it */
            ok->associated = client->refbuf;
            client->refbuf = ok;
            fserve_add_client_callback (client, source_client_callback, source);
        }
    }
    else
    {
        client_send_403 (client, "Mountpoint in use");
        WARN1 ("Mountpoint %s in use", uri);
    }
}


