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
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#else
#include <winsock2.h>
#include <windows.h>
#define snprintf _snprintf
#endif

#ifndef _WIN32
/* for __setup_empty_script_environment() */
#include <sys/stat.h>
#include <fcntl.h>
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
static int _free_client(void *key);
static void _parse_audio_info (source_t *source, const char *s);
static void source_shutdown (source_t *source);
#ifdef _WIN32
#define source_run_script(x,y)  ICECAST_LOG_WARN("on [dis]connect scripts disabled");
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

    if(mount[0] != '/')
        ICECAST_LOG_WARN("Source at \"%s\" does not start with '/', clients will be "
                "unable to connect", mount);

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

        src->client_tree = avl_tree_new(_compare_clients, NULL);
        src->pending_tree = avl_tree_new(_compare_clients, NULL);

        /* make duplicates for strings or similar */
        src->mount = strdup (mount);
        src->max_listeners = -1;
        thread_mutex_create(&src->lock);

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
        mountinfo = config_find_mount (config, mount, MOUNT_TYPE_NORMAL);
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
    int c;

    ICECAST_LOG_DEBUG("clearing source \"%s\"", source->mount);

    avl_tree_wlock (source->pending_tree);
    client_destroy(source->client);
    source->client = NULL;
    source->parser = NULL;
    source->con = NULL;

    /* log bytes read in access log */
    if (source->client && source->format)
        source->client->con->sent_bytes = source->format->read_bytes;

    if (source->dumpfile)
    {
        ICECAST_LOG_INFO("Closing dumpfile for %s", source->mount);
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }

    /* lets kick off any clients that are left on here */
    avl_tree_wlock (source->client_tree);
    c=0;
    while (1)
    {
        avl_node *node = avl_get_first (source->client_tree);
        if (node)
        {
            client_t *client = node->key;
            if (client->respcode == 200)
                c++; /* only count clients that have had some processing */
            avl_delete (source->client_tree, client, _free_client);
            continue;
        }
        break;
    }
    if (c)
    {
        stats_event_sub (NULL, "listeners", source->listeners);
        ICECAST_LOG_INFO("%d active listeners on %s released", c, source->mount);
    }
    avl_tree_unlock (source->client_tree);

    while (avl_get_first (source->pending_tree))
    {
        avl_delete (source->pending_tree,
                avl_get_first(source->pending_tree)->key, _free_client);
    }

    if (source->format && source->format->free_plugin)
        source->format->free_plugin (source->format);
    source->format = NULL;

    /* Lets clear out the source queue too */
    while (source->stream_data)
    {
        refbuf_t *p = source->stream_data;
        source->stream_data = p->next;
        p->next = NULL;
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
    source->max_listeners = -1;
    source->prev_listeners = 0;
    source->hidden = 0;
    source->shoutcast_compat = 0;
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
    avl_tree_unlock (source->pending_tree);
}


/* Remove the provided source from the global tree and free it */
void source_free_source (source_t *source)
{
    ICECAST_LOG_DEBUG("freeing source \"%s\"", source->mount);
    avl_tree_wlock (global.source_tree);
    avl_delete (global.source_tree, source, NULL);
    avl_tree_unlock (global.source_tree);

    avl_tree_free(source->pending_tree, _free_client);
    avl_tree_free(source->client_tree, _free_client);

    /* make sure all YP entries have gone */
    yp_remove (source->mount);

    free (source->mount);
    free (source);

    return;
}


client_t *source_find_client(source_t *source, int id)
{
    client_t fakeclient;
    void *result;
    connection_t fakecon;

    fakeclient.con = &fakecon;
    fakeclient.con->id = id;

    avl_tree_rlock(source->client_tree);
    if(avl_get_by_key(source->client_tree, &fakeclient, &result) == 0)
    {
        avl_tree_unlock(source->client_tree);
        return result;
    }

    avl_tree_unlock(source->client_tree);
    return NULL;
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
        ICECAST_LOG_WARN("src and dst are the same \"%s\", skipping", source->mount);
        return;
    }
    /* we don't want the two write locks to deadlock in here */
    thread_mutex_lock (&move_clients_mutex);

    /* if the destination is not running then we can't move clients */

    avl_tree_wlock (dest->pending_tree);
    if (dest->running == 0 && dest->on_demand == 0)
    {
        ICECAST_LOG_WARN("destination mount %s not running, unable to move clients ", dest->mount);
        avl_tree_unlock (dest->pending_tree);
        thread_mutex_unlock (&move_clients_mutex);
        return;
    }

    do
    {
        client_t *client;

        /* we need to move the client and pending trees - we must take the
         * locks in this order to avoid deadlocks */
        avl_tree_wlock (source->pending_tree);
        avl_tree_wlock (source->client_tree);

        if (source->on_demand == 0 && source->format == NULL)
        {
            ICECAST_LOG_INFO("source mount %s is not available", source->mount);
            break;
        }
        if (source->format && dest->format)
        {
            if (source->format->type != dest->format->type)
            {
                ICECAST_LOG_WARN("stream %s and %s are of different types, ignored", source->mount, dest->mount);
                break;
            }
        }

        while (1)
        {
            avl_node *node = avl_get_first (source->pending_tree);
            if (node == NULL)
                break;
            client = (client_t *)(node->key);
            avl_delete (source->pending_tree, client, NULL);

            /* when switching a client to a different queue, be wary of the 
             * refbuf it's referring to, if it's http headers then we need
             * to write them so don't release it.
             */
            if (client->check_buffer != format_check_http_buffer)
            {
                client_set_queue (client, NULL);
                client->check_buffer = format_check_file_buffer;
                if (source->con == NULL)
                    client->intro_offset = -1;
            }

            avl_insert (dest->pending_tree, (void *)client);
            count++;
        }

        while (1)
        {
            avl_node *node = avl_get_first (source->client_tree);
            if (node == NULL)
                break;

            client = (client_t *)(node->key);
            avl_delete (source->client_tree, client, NULL);

            /* when switching a client to a different queue, be wary of the 
             * refbuf it's referring to, if it's http headers then we need
             * to write them so don't release it.
             */
            if (client->check_buffer != format_check_http_buffer)
            {
                client_set_queue (client, NULL);
                client->check_buffer = format_check_file_buffer;
                if (source->con == NULL)
                    client->intro_offset = -1;
            }
            avl_insert (dest->pending_tree, (void *)client);
            count++;
        }
        ICECAST_LOG_INFO("passing %lu listeners to \"%s\"", count, dest->mount);

        source->listeners = 0;
        stats_event (source->mount, "listeners", "0");

    } while (0);

    avl_tree_unlock (source->pending_tree);
    avl_tree_unlock (source->client_tree);

    /* see if we need to wake up an on-demand relay */
    if (dest->running == 0 && dest->on_demand && count)
        dest->on_demand_req = 1;

    avl_tree_unlock (dest->pending_tree);
    thread_mutex_unlock (&move_clients_mutex);
}


/* get some data from the source. The stream data is placed in a refbuf
 * and sent back, however NULL is also valid as in the case of a short
 * timeout and there's no data pending.
 */
static refbuf_t *get_next_buffer (source_t *source)
{
    refbuf_t *refbuf = NULL;
    int delay = 250;

    if (source->short_delay)
        delay = 0;
    while (global.running == ICECAST_RUNNING && source->running)
    {
        int fds = 0;
        time_t current = time (NULL);

        if (source->client)
            fds = util_timed_wait_for_fd (source->con->sock, delay);
        else
        {
            thread_sleep (delay*1000);
            source->last_read = current;
        }

        if (current >= source->client_stats_update)
        {
            stats_event_args (source->mount, "total_bytes_read",
                    "%"PRIu64, source->format->read_bytes);
            stats_event_args (source->mount, "total_bytes_sent",
                    "%"PRIu64, source->format->sent_bytes);
            source->client_stats_update = current + 5;
        }
        if (fds < 0)
        {
            if (! sock_recoverable (sock_error()))
            {
                ICECAST_LOG_WARN("Error while waiting on socket, Disconnecting source");
                source->running = 0;
            }
            break;
        }
        if (fds == 0)
        {
            thread_mutex_lock(&source->lock);
            if ((source->last_read + (time_t)source->timeout) < current)
            {
                ICECAST_LOG_DEBUG("last %ld, timeout %d, now %ld", (long)source->last_read,
                        source->timeout, (long)current);
                ICECAST_LOG_WARN("Disconnecting source due to socket timeout");
                source->running = 0;
            }
            thread_mutex_unlock(&source->lock);
            break;
        }
        source->last_read = current;
        refbuf = source->format->get_buffer (source);
        if (source->client->con && source->client->con->error)
        {
            ICECAST_LOG_INFO("End of Stream %s", source->mount);
            source->running = 0;
            continue;
        }
        if (refbuf)
            break;
    }

    return refbuf;
}


/* general send routine per listener.  The deletion_expected tells us whether
 * the last in the queue is about to disappear, so if this client is still
 * referring to it after writing then drop the client as it's fallen too far
 * behind 
 */ 
static void send_to_listener (source_t *source, client_t *client, int deletion_expected)
{
    int bytes;
    int loop = 10;   /* max number of iterations in one go */
    int total_written = 0;

    while (1)
    {
        /* check for limited listener time */
        if (client->con->discon_time)
            if (time(NULL) >= client->con->discon_time)
            {
                ICECAST_LOG_INFO("time limit reached for client #%lu", client->con->id);
                client->con->error = 1;
            }

        /* jump out if client connection has died */
        if (client->con->error)
            break;

        /* lets not send too much to one client in one go, but don't
           sleep for too long if more data can be sent */
        if (total_written > 20000 || loop == 0)
        {
            if (client->check_buffer != format_check_file_buffer)
                source->short_delay = 1;
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
    source->format->sent_bytes += total_written;

    /* the refbuf referenced at head (last in queue) may be marked for deletion
     * if so, check to see if this client is still referring to it */
    if (deletion_expected && client->refbuf && client->refbuf == source->stream_data)
    {
        ICECAST_LOG_INFO("Client %lu (%s) has fallen too far behind, removing",
                client->con->id, client->con->ip);
        stats_event_inc (source->mount, "slow_listeners");
        client->con->error = 1;
    }
}


/* Open the file for stream dumping.
 * This function should do all processing of the filename.
 */
static FILE * source_open_dumpfile(const char * filename) {
#ifndef _WIN32
    /* some of the below functions seems not to be standard winapi functions */
    char buffer[PATH_MAX];
    time_t curtime;
    struct tm *loctime;

    /* Get the current time. */
    curtime = time (NULL);

    /* Convert it to local time representation. */
    loctime = localtime (&curtime);

    strftime (buffer, sizeof(buffer), filename, loctime);
    filename = buffer;
#endif

    return fopen (filename, "ab");
}

/* Perform any initialisation just before the stream data is processed, the header
 * info is processed by now and the format details are setup
 */
static void source_init (source_t *source)
{
    ice_config_t *config = config_get_config();
    char *listenurl;
    const char *str;
    int listen_url_size;
    mount_proxy *mountinfo;

    /* 6 for max size of port */
    listen_url_size = strlen("http://") + strlen(config->hostname) +
        strlen(":") + 6 + strlen(source->mount) + 1;

    listenurl = malloc (listen_url_size);
    memset (listenurl, '\000', listen_url_size);
    snprintf (listenurl, listen_url_size, "http://%s:%d%s",
            config->hostname, config->port, source->mount);
    config_release_config();

    str = httpp_getvar(source->parser, "ice-audio-info");
    source->audio_info = util_dict_new();
    if (str)
    {
        _parse_audio_info (source, str);
        stats_event (source->mount, "audio_info", str);
    }

    stats_event (source->mount, "listenurl", listenurl);

    free(listenurl);

    if (source->dumpfilename != NULL)
    {
        source->dumpfile = source_open_dumpfile (source->dumpfilename);
        if (source->dumpfile == NULL)
        {
            ICECAST_LOG_WARN("Cannot open dump file \"%s\" for appending: %s, disabling.",
                    source->dumpfilename, strerror(errno));
        }
    }

    /* grab a read lock, to make sure we get a chance to cleanup */
    thread_rwlock_rlock (source->shutdown_rwlock);

    /* start off the statistics */
    source->listeners = 0;
    stats_event_inc (NULL, "source_total_connections");
    stats_event (source->mount, "slow_listeners", "0");
    stats_event_args (source->mount, "listeners", "%lu", source->listeners);
    stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);
    stats_event_time (source->mount, "stream_start");
    stats_event_time_iso8601 (source->mount, "stream_start_iso8601");

    ICECAST_LOG_DEBUG("Source creation complete");
    source->last_read = time (NULL);
    source->prev_listeners = -1;
    source->running = 1;

    mountinfo = config_find_mount (config_get_config(), source->mount, MOUNT_TYPE_NORMAL);
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
}


void source_main (source_t *source)
{
    refbuf_t *refbuf;
    client_t *client;
    avl_node *client_node;

    source_init (source);

    while (global.running == ICECAST_RUNNING && source->running) {
        int remove_from_q;

        refbuf = get_next_buffer (source);

        remove_from_q = 0;
        source->short_delay = 0;

        if (refbuf)
        {
            /* append buffer to the in-flight data queue,  */
            if (source->stream_data == NULL)
            {
                source->stream_data = refbuf;
                source->burst_point = refbuf;
            }
            if (source->stream_data_tail)
                source->stream_data_tail->next = refbuf;
            source->stream_data_tail = refbuf;
            source->queue_size += refbuf->len;
            /* new buffer is referenced for burst */
            refbuf_addref (refbuf);

            /* new data on queue, so check the burst point */
            source->burst_offset += refbuf->len;
            while (source->burst_offset > source->burst_size)
            {
                refbuf_t *to_release = source->burst_point;

                if (to_release->next)
                {
                    source->burst_point = to_release->next;
                    source->burst_offset -= to_release->len;
                    refbuf_release (to_release);
                    continue;
                }
                break;
            }

            /* save stream to file */
            if (source->dumpfile && source->format->write_buf_to_file)
                source->format->write_buf_to_file (source, refbuf);
        }
        /* lets see if we have too much data in the queue, but don't remove it until later */
        thread_mutex_lock(&source->lock);
        if (source->queue_size > source->queue_size_limit)
            remove_from_q = 1;
        thread_mutex_unlock(&source->lock);

        /* acquire write lock on pending_tree */
        avl_tree_wlock(source->pending_tree);

        /* acquire write lock on client_tree */
        avl_tree_wlock(source->client_tree);

        client_node = avl_get_first(source->client_tree);
        while (client_node) {
            client = (client_t *)client_node->key;

            send_to_listener (source, client, remove_from_q);

            if (client->con->error) {
                client_node = avl_get_next(client_node);
                if (client->respcode == 200)
                    stats_event_dec (NULL, "listeners");
                avl_delete(source->client_tree, (void *)client, _free_client);
                source->listeners--;
                ICECAST_LOG_DEBUG("Client removed");
                continue;
            }
            client_node = avl_get_next(client_node);
        }

        /** add pending clients **/
        client_node = avl_get_first(source->pending_tree);
        while (client_node) {

            if(source->max_listeners != -1 && 
                    source->listeners >= (unsigned long)source->max_listeners) 
            {
                /* The common case is caught in the main connection handler,
                 * this deals with rarer cases (mostly concerning fallbacks)
                 * and doesn't give the listening client any information about
                 * why they were disconnected
                 */
                client = (client_t *)client_node->key;
                client_node = avl_get_next(client_node);
                avl_delete(source->pending_tree, (void *)client, _free_client);

                ICECAST_LOG_INFO("Client deleted, exceeding maximum listeners for this "
                        "mountpoint (%s).", source->mount);
                continue;
            }
            
            /* Otherwise, the client is accepted, add it */
            avl_insert(source->client_tree, client_node->key);

            source->listeners++;
            ICECAST_LOG_DEBUG("Client added for mountpoint (%s)", source->mount);
            stats_event_inc(source->mount, "connections");

            client_node = avl_get_next(client_node);
        }

        /** clear pending tree **/
        while (avl_get_first(source->pending_tree)) {
            avl_delete(source->pending_tree, 
                    avl_get_first(source->pending_tree)->key, 
                    source_remove_client);
        }

        /* release write lock on pending_tree */
        avl_tree_unlock(source->pending_tree);

        /* update the stats if need be */
        if (source->listeners != source->prev_listeners)
        {
            source->prev_listeners = source->listeners;
            ICECAST_LOG_INFO("listener count on %s now %lu", source->mount, source->listeners);
            if (source->listeners > source->peak_listeners)
            {
                source->peak_listeners = source->listeners;
                stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);
            }
            stats_event_args (source->mount, "listeners", "%lu", source->listeners);
            if (source->listeners == 0 && source->on_demand)
                source->running = 0;
        }

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
                    ICECAST_LOG_ERROR("queue state is unexpected");
                    source->running = 0;
                    break;
                }
                source->stream_data = to_go->next;
                source->queue_size -= to_go->len;
                to_go->next = NULL;
                refbuf_release (to_go);
            }
        }

        /* release write lock on client_tree */
        avl_tree_unlock(source->client_tree);
    }
    source_shutdown (source);
}


static void source_shutdown (source_t *source)
{
    mount_proxy *mountinfo;

    source->running = 0;
    ICECAST_LOG_INFO("Source from %s at \"%s\" exiting", source->con->ip, source->mount);

    mountinfo = config_find_mount (config_get_config(), source->mount, MOUNT_TYPE_NORMAL);
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
            source_move_clients (source, fallback_source);

        avl_tree_unlock (global.source_tree);
    }

    /* delete this sources stats */
    stats_event(source->mount, NULL, NULL);

    /* we don't remove the source from the tree here, it may be a relay and
       therefore reserved */
    source_clear_source (source);

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

int source_remove_client(void *key)
{
    return 1;
}

static int _free_client(void *key)
{
    client_t *client = (client_t *)key;

    /* if no response has been sent then send a 404 */
    if (client->respcode == 0)
        client_send_404 (client, "Mount unavailable");
    else
        client_destroy(client);
    
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
                util_dict_set (source->audio_info, name, esc);
                stats_event (source->mount, name, esc);
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

    ICECAST_LOG_DEBUG("Applying mount information for \"%s\"", source->mount);
    avl_tree_rlock (source->client_tree);
    stats_event_args (source->mount, "listener_peak", "%lu", source->peak_listeners);

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

    /* to be done before possible non-utf8 stats */
    if (source->format && source->format->apply_settings)
        source->format->apply_settings (source->client, source->format, mountinfo);

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
        ICECAST_LOG_DEBUG("YP changed to %d", val);
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
            str = "Unspecified description";
        } while (0);
        if (source->format)
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

    if (mountinfo && mountinfo->fallback_mount)
    {
        char *mount = source->fallback_mount;
        source->fallback_mount = strdup (mountinfo->fallback_mount);
        free (mount);
    }
    else
        source->fallback_mount = NULL;

    if (mountinfo && mountinfo->dumpfile)
    {
        char *filename = source->dumpfilename;
        source->dumpfilename = strdup (mountinfo->dumpfile);
        free (filename);
    }
    else
        source->dumpfilename = NULL;

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

            f = fopen (path, "rb");
            if (f)
                source->intro_file = f;
            else
                ICECAST_LOG_WARN("Cannot open intro file \"%s\": %s", path, strerror(errno));
            free (path);
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

    avl_tree_unlock (source->client_tree);
}


/* update the specified source with details from the config or mount.
 * mountinfo can be NULL in which case default settings should be taken
 * This function is called by the Slave thread
 */
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo)
{
    thread_mutex_lock(&source->lock);
    /*  skip if source is a fallback to file */
    if (source->running && source->client == NULL)
    {
        stats_event_hidden (source->mount, NULL, 1);
        thread_mutex_unlock(&source->lock);
        return;
    }
    /* set global settings first */
    source->queue_size_limit = config->queue_size_limit;
    source->timeout = config->source_timeout;
    source->burst_size = config->burst_size;

    stats_event_args (source->mount, "listenurl", "http://%s:%d%s",
            config->hostname, config->port, source->mount);

    source_apply_mount (source, mountinfo);

    if (source->fallback_mount)
        ICECAST_LOG_DEBUG("fallback %s", source->fallback_mount);
    if (mountinfo && mountinfo->intro_filename)
        ICECAST_LOG_DEBUG("intro file is %s", mountinfo->intro_filename);
    if (source->dumpfilename)
        ICECAST_LOG_DEBUG("Dumping stream to %s", source->dumpfilename);
    if (mountinfo && mountinfo->on_connect)
        ICECAST_LOG_DEBUG("connect script \"%s\"", mountinfo->on_connect);
    if (mountinfo && mountinfo->on_disconnect)
        ICECAST_LOG_DEBUG("disconnect script \"%s\"", mountinfo->on_disconnect);
    if (source->on_demand)
    {
        ICECAST_LOG_DEBUG("on_demand set");
        stats_event (source->mount, "on_demand", "1");
        stats_event_args (source->mount, "listeners", "%ld", source->listeners);
    }
    else
        stats_event (source->mount, "on_demand", NULL);

    if (source->hidden)
    {
        stats_event_hidden (source->mount, NULL, 1);
        ICECAST_LOG_DEBUG("hidden from public");
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
    ICECAST_LOG_DEBUG("public set to %d", source->yp_public);
    ICECAST_LOG_DEBUG("max listeners to %ld", source->max_listeners);
    ICECAST_LOG_DEBUG("queue size to %u", source->queue_size_limit);
    ICECAST_LOG_DEBUG("burst size to %u", source->burst_size);
    ICECAST_LOG_DEBUG("source timeout to %u", source->timeout);
    ICECAST_LOG_DEBUG("fallback_when_full to %u", source->fallback_when_full);
    thread_mutex_unlock(&source->lock);
}


void *source_client_thread (void *arg)
{
    source_t *source = arg;

    stats_event_inc(NULL, "source_client_connections");
    stats_event (source->mount, "listeners", "0");

    source_main (source);

    source_free_source (source);
    slave_update_all_mounts();

    return NULL;
}


void source_client_callback (client_t *client, void *arg)
{
    const char *agent;
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
    agent = httpp_getvar (source->client->parser, "user-agent");
    if (agent)
        stats_event (source->mount, "user_agent", agent);

    thread_create ("Source Thread", source_client_thread,
            source, THREAD_DETACHED);
}


#ifndef _WIN32
/* this sets up the new environment for script execution.
 * We ignore most failtures as we can not handle them anyway.
 */
static inline void __setup_empty_script_environment(void) {
    int i;

    /* close at least the first 1024 handles */
    for (i = 0; i < 1024; i++)
        close(i);

    /* open null device */
    i = open("/dev/null", O_RDWR);
    if (i != -1) {
        /* attach null device to stdin, stdout and stderr */
        if (i != 0)
            dup2(i, 0);
        if (i != 1)
            dup2(i, 1);
        if (i != 2)
            dup2(i, 2);

        /* close null device */
        if (i > 2)
            close(i);
    }
}

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
                    ICECAST_LOG_ERROR("Unable to fork %s (%s)", command, strerror (errno));
                    break;
                case 0:  /* child */
                    if (access(command, R_OK|X_OK) != 0) {
                        ICECAST_LOG_ERROR("Unable to run command %s (%s)", command, strerror(errno));
                        exit(1);
                    }
                    ICECAST_LOG_DEBUG("Starting command %s", command);
                    __setup_empty_script_environment();
                    /* consider to add action here as well */
                    execl(command, command, mountpoint, (char *)NULL);
                    exit(1);
                default: /* parent */
                    break;
            }
            exit (0);
        case -1:
            ICECAST_LOG_ERROR("Unable to fork %s", strerror (errno));
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
        config = config_get_config ();
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
            ICECAST_LOG_WARN("unable to open file \"%s\"", path);
            free (path);
            break;
        }
        free (path);
        source = source_reserve (mount);
        if (source == NULL)
        {
            ICECAST_LOG_WARN("mountpoint \"%s\" already reserved", mount);
            break;
        }
        ICECAST_LOG_INFO("mountpoint %s is reserved", mount);
        type = fserve_content_type (mount);
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        httpp_setvar (parser, "content-type", type);
        free (type);

        source->hidden = 1;
        source->yp_public = 0;
        source->intro_file = file;
        source->parser = parser;
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


/* rescan the mount list, so that xsl files are updated to show
 * unconnected but active fallback mountpoints
 */
void source_recheck_mounts (int update_all)
{
    ice_config_t *config;
    mount_proxy *mount;

    avl_tree_rlock (global.source_tree);
    config = config_get_config();
    mount = config->mounts;

    if (update_all)
        stats_clear_virtual_mounts ();

    for (; mount; mount = mount->next)
    {
        if (mount->mounttype != MOUNT_TYPE_NORMAL)
	    continue;

        source_t *source = source_find_mount (mount->mountname);

        if (source)
        {
            source = source_find_mount_raw (mount->mountname);
            if (source)
            {
                mount_proxy *mountinfo = config_find_mount (config, source->mount, MOUNT_TYPE_NORMAL);
                source_update_settings (config, source, mountinfo);
            }
            else if (update_all)
            {
                stats_event_hidden (mount->mountname, NULL, mount->hidden);
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
        if (global.running == ICECAST_RUNNING && mount->fallback_mount)
        {
            source_t *fallback = source_find_mount (mount->fallback_mount);
            if (fallback == NULL)
            {
                thread_create ("Fallback file thread", source_fallback_file,
                        strdup (mount->fallback_mount), THREAD_DETACHED);
            }
        }
    }
    avl_tree_unlock (global.source_tree);
    config_release_config();
}

