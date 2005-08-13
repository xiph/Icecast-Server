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
#define snprintf _snprintf
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
#include "os.h"

#undef CATMODULE
#define CATMODULE "source"

#define MAX_FALLBACK_DEPTH 10

mutex_t move_clients_mutex;

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static void _parse_audio_info (source_t *source, const char *s);
static void source_shutdown (source_t *source);
static void process_listeners (source_t *source, int fast_clients_only, int deletion_expected);
static int source_free_client (source_t *source, client_t *client);
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
        src->max_listeners = -1;

        thread_mutex_create (src->mount, &src->lock);

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
    DEBUG1 ("clearing source \"%s\"", source->mount);
    client_destroy(source->client);
    source->client = NULL;

    if (source->dumpfile)
    {
        INFO1 ("Closing dumpfile for %s", source->mount);
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }

    /* lets drop any clients still connected */
    while (source->active_clients)
    {
        client_t *client = source->active_clients;
        source->active_clients = client->next;
        source_free_client (source, client);
    }
    source->fast_clients_p = &source->active_clients;

    format_free_plugin (source->format);
    source->format = NULL;

    /* flush out the stream data, we don't want any left over */
    while (source->stream_data)
    {
        refbuf_t *p = source->stream_data;
        source->stream_data = p->next;
        /* can be referenced by burst handler as well */
        while (p->_count > 1)
            refbuf_release (p);
        refbuf_release (p);
    }
    source->stream_data_tail = NULL;

    source->burst_point = NULL;
    source->burst_size = 0;
    source->burst_offset = 0;
    source->queue_size = 0;
    source->queue_size_limit = 0;
    source->listeners = 0;
    source->prev_listeners = 0;
    source->shoutcast_compat = 0;
    source->max_listeners = -1;
    source->hidden = 0;
    source->client_stats_update = 0;
    util_dict_free (source->audio_info);
    source->audio_info = NULL;

    free(source->fallback_mount);
    source->fallback_mount = NULL;

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

    return;
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

        /* we need to move the client and pending trees */
        while (source->active_clients)
        {
            client_t *client = source->active_clients;
            source->active_clients = client->next;

            /* when switching a client to a different queue, be wary of the 
             * refbuf it's referring to, if it's http headers then we need
             * to write them so don't release it.
             */
            if (client->check_buffer != format_check_http_buffer)
            {
                client_set_queue (client, NULL);
                client->check_buffer = format_check_file_buffer;
                if (source->client && source->client->con == NULL)
                    client->intro_offset = -1;
            }

            client->next = dest->active_clients;
            dest->active_clients = client;
            count++;
        }
        if (count != source->listeners)
            WARN2 ("count %lu, listeners %lu", count, source->listeners);

        INFO2 ("passing %lu listeners to \"%s\"", count, dest->mount);

        dest->listeners += count;
        source->listeners = 0;
        stats_event (source->mount, "listeners", "0");

    } while (0);

    thread_mutex_unlock (&source->lock);
    /* see if we need to wake up an on-demand relay */
    if (dest->running == 0 && dest->on_demand && count)
    {
        dest->on_demand_req = 1;
        slave_rebuild_mounts();
    }
    thread_mutex_unlock (&dest->lock);
    thread_mutex_unlock (&move_clients_mutex);
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
        time_t current = global.time;
        int delay = 200;

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

        if (source->client->con)
            fds = util_timed_wait_for_fd (source->client->con->sock, delay);
        else
        {
            thread_sleep (delay*1000);
            source->last_read = current;
        }

        /* take the lock */
        thread_mutex_lock (&source->lock);

        if (current >= source->client_stats_update)
        {
            stats_event_args (source->mount, "outgoing_bitrate", "%ld", 
                    8 * rate_avg (source->format->out_bitrate));
            stats_event_args (source->mount, "incoming_bitrate", "%ld", 
                    8 * rate_avg (source->format->in_bitrate));
            stats_event_args (source->mount, "total_bytes_read",
                    FORMAT_UINT64, source->format->read_bytes);
            stats_event_args (source->mount, "total_bytes_sent",
                    FORMAT_UINT64, source->format->sent_bytes);
            source->client_stats_update = current + 5;
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
            rate_add (source->format->in_bitrate, 0, global.time);
            break;
        }
        source->last_read = current;
        refbuf = source->format->get_buffer (source);
        if (refbuf)
        {
            /* append buffer to the in-flight data queue,  */
            if (source->stream_data == NULL)
            {
                source->stream_data = refbuf;
                source->burst_point = refbuf;
                source->burst_offset = 0;
            }
            if (source->stream_data_tail)
                source->stream_data_tail->next = refbuf;
            source->stream_data_tail = refbuf;
            source->queue_size += refbuf->len;
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
            if (source->client->con && source->client->con->error)
            {
                INFO1 ("End of Stream %s", source->mount);
                source->running = 0;
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
    int total_written = 0;
    int ret = 0;

    while (1)
    {
        /* check for limited listener time */
        if (client->con->discon_time)
            if (global.time >= client->con->discon_time)
            {
                INFO1 ("time limit reached for client #%lu", client->con->id);
                client->con->error = 1;
            }

        /* jump out if client connection has died */
        if (client->con->error)
            break;

        /* lets not send too much to one client in one go, but don't
           sleep for too long if more data can be sent */
        if (total_written > 20000 || loop == 0)
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
    rate_add (source->format->out_bitrate, total_written, global.time);
    source->format->sent_bytes += total_written;

    /* the refbuf referenced at head (last in queue) may be marked for deletion
     * if so, check to see if this client is still referring to it */
    if (deletion_expected && client->refbuf && client->refbuf == source->stream_data)
    {
        INFO2 ("Client %lu (%s) has fallen too far behind, removing",
                client->con->id, client->con->ip);
        stats_event_inc (source->mount, "slow_listeners");
        client->con->error = 1;
        ret = 1;
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

            *client_p = client->next;
            client = client->next;

            source_free_client (source, to_go);
            source->listeners--;
            DEBUG0("Client removed");
            continue;
        }
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
            rate_add (source->format->out_bitrate, 0, 0);
        if (source->listeners == 0 && source->on_demand)
            source->running = 0;
    }
}


/* Perform any initialisation before the stream data is processed, the header
 * info is processed by now and the format details are setup
 */
static void source_init (source_t *source)
{
    char *str;
    mount_proxy *mountinfo;

    thread_mutex_lock (&source->lock);

    if (source->dumpfilename != NULL)
    {
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
    stats_event (source->mount, "slow_listeners", "0");
    stats_event (source->mount, "server_type", source->format->contenttype);
    stats_event (source->mount, "listener_peak", "0");
    stats_event_time (source->mount, "stream_start");

    DEBUG0("Source creation complete");
    source->last_read = global.time;
    source->prev_listeners = -1;
    source->running = 1;

    source->fast_clients_p = &source->active_clients;
    source->audio_info = util_dict_new();
    str = httpp_getvar(source->client->parser, "ice-audio-info");
    if (str)
    {
        _parse_audio_info (source, str);
        stats_event (source->mount, "audio_info", str);
    }

    thread_mutex_unlock (&source->lock);

    mountinfo = config_find_mount (config_get_config(), source->mount);
    if (mountinfo)
    {
        if (mountinfo->on_connect)
            source_run_script (mountinfo->on_connect, source->mount);
        auth_stream_start (mountinfo, source->mount);
    }
    config_release_config();

    /*
    ** Now, if we have a fallback source and override is on, we want
    ** to steal its clients, because it means we've come back online
    ** after a failure and they should be gotten back from the waiting
    ** loop or jingle track or whatever the fallback is used for
    */

    if (source->fallback_override && source->fallback_mount)
    {
        source_t *fallback_source;

        avl_tree_rlock(global.source_tree);
        fallback_source = source_find_mount(source->fallback_mount);

        if (fallback_source)
            source_move_clients (fallback_source, source);

        avl_tree_unlock(global.source_tree);
    }
    thread_mutex_lock (&source->lock);
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
            /* normal unreferenced queue data will have a refcount 1, but
             * burst queue data will be at least 2, active clients will also
             * increase refcount */
            while (source->stream_data->_count == 1)
            {
                refbuf_t *to_go = source->stream_data;

                if (to_go->next == NULL || source->burst_point == to_go)
                {
                    /* this should not happen */
                    ERROR0 ("queue state is unexpected");
                    source->running = 0;
                    break;
                }
                source->stream_data = to_go->next;
                source->queue_size -= to_go->len;
                refbuf_release (to_go);
            }
        }
    }
    source->running = 0;

    source_shutdown (source);
}


static void source_shutdown (source_t *source)
{
    mount_proxy *mountinfo;

    INFO1("Source \"%s\" exiting", source->mount);
    source->running = 0;

    mountinfo = config_find_mount (config_get_config(), source->mount);
    if (mountinfo)
    {
        if (mountinfo->on_disconnect)
            source_run_script (mountinfo->on_disconnect, source->mount);
        auth_stream_end (mountinfo, source->mount);
    }
    config_release_config();

    /* we have de-activated the source now, so no more clients will be
     * added, now move the listeners we have to the fallback (if any)
     */
    if (source->fallback_mount)
    {
        source_t *fallback_source;

        avl_tree_rlock(global.source_tree);
        fallback_source = source_find_mount (source->fallback_mount);

        if (fallback_source != NULL)
        {
            /* be careful wrt to deadlocking */
            thread_mutex_unlock (&source->lock);
            source_move_clients (source, fallback_source);
            thread_mutex_lock (&source->lock);
        }

        avl_tree_unlock (global.source_tree);
    }

    /* delete this sources stats */
    stats_event(source->mount, NULL, NULL);

    /* we don't remove the source from the tree here, it may be a relay and
       therefore reserved */
    source_clear_source (source);

    thread_mutex_unlock (&source->lock);

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


int source_free_client (source_t *source, client_t *client)
{
    /* if no response has been sent then send a 404 */
    if (client->respcode == 0)
        client_send_404 (client, "Mount unavailable");
    else
        client_destroy (client);
    
    return 1;
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
    char *str;
    int val;
    http_parser_t *parser = NULL;

    if (mountinfo == NULL || strcmp (mountinfo->mountname, source->mount) == 0)
        INFO1 ("Applying mount information for \"%s\"", source->mount);
    else
        INFO2 ("Applying mount information for \"%s\" from \"%s\"",
                source->mount, mountinfo->mountname);

    if (mountinfo)
    {
        source->max_listeners = mountinfo->max_listeners;
        source->fallback_override = mountinfo->fallback_override;
        source->hidden = mountinfo->hidden;
    }

    /* if a setting is available in the mount details then use it, else
     * check the parser details. */

    if (source->client)
        parser = source->client->parser;

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
        str = mountinfo->stream_name;
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
    }
    stats_event (source->mount, "server_name", str);

    /* stream description */
    if (mountinfo && mountinfo->stream_description)
        str = mountinfo->stream_description;
    else
    {
        do {
            str = httpp_getvar (parser, "ice-description");
            if (str) break;
            str = httpp_getvar (parser, "icy-description");
            if (str) break;
            str = httpp_getvar (parser, "x-audiocast-description");
            if (str) break;
            str = "Unspecified description";
        } while (0);
    }
    stats_event (source->mount, "server_description", str);

    /* stream URL */
    if (mountinfo && mountinfo->stream_url)
        str = mountinfo->stream_url;
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
    }
    stats_event (source->mount, "server_url", str);

    /* stream genre */
    if (mountinfo && mountinfo->stream_genre)
        str = mountinfo->stream_genre;
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
    }
    stats_event (source->mount, "genre", str);

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

    free (source->fallback_mount);
    source->fallback_mount = NULL;
    if (mountinfo && mountinfo->fallback_mount)
        source->fallback_mount = strdup (mountinfo->fallback_mount);

    /* needs a better mechanism, probably via a client_t handle */
    if (mountinfo && mountinfo->dumpfile)
    {
        free (source->dumpfilename);
        source->dumpfilename = strdup (mountinfo->dumpfile);
    }
    /* handle changes in intro file setting */
    if (source->intro_file)
    {
        int close_it = 0;
        if (mountinfo)
        {
            if (mountinfo->intro_filename && strcmp (mountinfo->intro_filename,
                        source->intro_filename) != 0)
                close_it = 1;
        }
        else
            close_it = 1;
        fclose (source->intro_file);
        source->intro_file = NULL;
        free (source->intro_filename);
        source->intro_filename = NULL;
    }
    if (mountinfo && mountinfo->intro_filename)
    {
        ice_config_t *config = config_get_config_unlocked ();
        unsigned int len  = strlen (config->webroot_dir) +
            strlen (mountinfo->intro_filename) + 2;
        char *path = malloc (len);
        if (path)
        {
            snprintf (path, len, "%s" PATH_SEPARATOR "%s", config->webroot_dir,
                    mountinfo->intro_filename);

            source->intro_file = fopen (path, "rb");
            if (source->intro_file == NULL)
                WARN2 ("Cannot open intro file \"%s\": %s", path, strerror(errno));
            free (path);
            source->intro_filename = strdup (mountinfo->intro_filename);
        }
    }

    if (mountinfo && mountinfo->queue_size_limit)
        source->queue_size_limit = mountinfo->queue_size_limit;

    if (mountinfo && mountinfo->source_timeout)
        source->timeout = mountinfo->source_timeout;

    if (mountinfo && mountinfo->burst_size >= 0)
        source->burst_size = (unsigned int)mountinfo->burst_size;

    if (mountinfo && mountinfo->fallback_when_full)
        source->fallback_when_full = mountinfo->fallback_when_full;

    if (source->format && source->format->apply_settings)
        source->format->apply_settings (source->client, source->format, mountinfo);
}


/* update the specified source with details from the config or mount.
 * mountinfo can be NULL in which case default settings should be taken
 */
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo)
{
    /*  skip if source is a fallback to file */
    if (source->running && source->client->con == NULL)
    {
        stats_event_hidden (source->mount, NULL, 1);
        return;
    }
    thread_mutex_lock (&source->lock);
    /* set global settings first */
    source->queue_size_limit = config->queue_size_limit;
    source->timeout = config->source_timeout;
    source->burst_size = config->burst_size;

    source_apply_mount (source, mountinfo);

    if (source->fallback_mount)
        DEBUG1 ("fallback %s", source->fallback_mount);
    if (source->intro_filename)
        DEBUG1 ("intro file is %s", source->intro_filename);
    if (source->dumpfilename)
        DEBUG1 ("Dumping stream to %s", source->dumpfilename);
    if (mountinfo && mountinfo->on_connect)
        DEBUG1 ("connect script \"%s\"", mountinfo->on_connect);
    if (mountinfo && mountinfo->on_disconnect)
        DEBUG1 ("disconnect script \"%s\"", mountinfo->on_disconnect);
    if (source->on_demand)
    {
        DEBUG0 ("on_demand set");
        stats_event (source->mount, "on_demand", "1");
    }
    else
        stats_event (source->mount, "on_demand", NULL);

    if (source->hidden)
    {
        stats_event_hidden (source->mount, NULL, 1);
        DEBUG0 ("hidden from xsl");
    }
    else
        stats_event_hidden (source->mount, NULL, 0);

    if (source->max_listeners == -1)
        stats_event (source->mount, "max_listeners", "unlimited");
    else
    {
        char buf [10];
        snprintf (buf, sizeof (buf), "%ld", source->max_listeners);
        stats_event (source->mount, "max_listeners", buf);
    }
    DEBUG1 ("public set to %d", source->yp_public);
    DEBUG1 ("max listeners to %ld", source->max_listeners);
    DEBUG1 ("queue size to %u", source->queue_size_limit);
    DEBUG1 ("burst size to %u", source->burst_size);
    DEBUG1 ("source timeout to %u", source->timeout);
    DEBUG1 ("fallback_when_full to %u", source->fallback_when_full);
    thread_mutex_unlock (&source->lock);
}


void *source_client_thread (void *arg)
{
    source_t *source = arg;

    stats_event_inc(NULL, "source_client_connections");
    stats_event (source->mount, "listeners", "0");

    source_main (source);

    source_free_source (source);
    source_recheck_mounts ();

    return NULL;
}


void source_client_callback (client_t *client, void *arg)
{
    const char *agent;
    source_t *source = arg;

    if (client->con->error)
    {
        global_lock();
        global.sources--;
        global_unlock();
        source_free_source (source);
        client_destroy (client);
        return;
    }
    stats_event (source->mount, "source_ip", source->client->con->ip);
    agent = httpp_getvar (source->client->parser, "user-agent");
    if (agent)
        stats_event (source->mount, "user_agent", agent);

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
                    execl (command, command, mountpoint, NULL);
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
    const char *type;
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
            DEBUG1 ("unable to open file \"%s\"", path);
            free (path);
            break;
        }
        free (path);
        source = source_reserve (mount);
        if (source == NULL)
        {
            DEBUG1 ("mountpoint \"%s\" already reserved", mount);
            break;
        }
        type = fserve_content_type (mount);
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        httpp_setvar (parser, "content-type", type);

        source->hidden = 1;
        source->yp_public = 0;
        source->intro_file = file;
        file = NULL;

        if (connection_complete_source (source, NULL, parser) < 0)
            break;
        source_client_thread (source);
    } while (0);
    if (file)
        fclose (file);
    free (mount);
    return NULL;
}


/* rescan the mount list, so that xsl files are updated to show
 * unconnected but active fallback mountpoints
 */
void source_recheck_mounts (void)
{
    ice_config_t *config = config_get_config();
    mount_proxy *mount = config->mounts;

    avl_tree_rlock (global.source_tree);

    while (mount)
    {
        source_t *source = source_find_mount (mount->mountname);

        if (source)
        {
            source = source_find_mount_raw (mount->mountname);
            if (source)
            {
                mount_proxy *mountinfo = config_find_mount (config, source->mount);
                source_update_settings (config, source, mountinfo);
            }
            else
            {
                stats_event_hidden (mount->mountname, NULL, mount->hidden);
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

