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
#include "auth.h"

#undef CATMODULE
#define CATMODULE "source"

#define MAX_FALLBACK_DEPTH 10

mutex_t move_clients_mutex;

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _free_client(void *key);
static void _parse_audio_info (source_t *source, const char *s);
static void source_shutdown (source_t *source);

/* Allocate a new source with the stated mountpoint, if one already
 * exists with that mountpoint in the global source tree then return
 * NULL.
 */
source_t *source_reserve (const char *mount)
{
    source_t *src = NULL;

    if(mount[0] != '/')
        WARN1("Source at \"%s\" does not start with '/', clients will be "
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
    while (mount != NULL)
    {
        /* limit the number of times through, maybe infinite */
        if (depth > MAX_FALLBACK_DEPTH)
        {
            source = NULL;
            break;
        }

        source = source_find_mount_raw(mount);

        if (source != NULL && source->running)
            break;

        /* source is not running, meaning that the fallback is not configured
           within the source, we need to check the mount list */
        mountinfo = config->mounts;
        source = NULL;
        while (mountinfo)
        {
            if (strcmp (mountinfo->mountname, mount) == 0)
                break;
            mountinfo = mountinfo->next;
        }
        if (mountinfo)
            mount = mountinfo->fallback_mount;
        else
            mount = NULL;
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
    source->parser = NULL;
    source->con = NULL;

    if (source->dumpfile)
    {
        INFO1 ("Closing dumpfile for %s", source->mount);
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }

    /* lets kick off any clients that are left on here */
    avl_tree_rlock (source->client_tree);
    while (avl_get_first (source->client_tree))
    {
        avl_delete (source->client_tree,
                avl_get_first (source->client_tree)->key, _free_client);
    }
    avl_tree_unlock (source->client_tree);

    avl_tree_rlock (source->pending_tree);
    while (avl_get_first (source->pending_tree))
    {
        avl_delete (source->pending_tree,
                avl_get_first(source->pending_tree)->key, _free_client);
    }
    avl_tree_unlock (source->pending_tree);

    if (source->format && source->format->free_plugin)
    {
        source->format->free_plugin (source->format);
    }
    source->format = NULL;
    if (source->yp_public)
        yp_remove (source->mount);

    source->burst_point = NULL;
    source->burst_size = 0;
    source->burst_offset = 0;
    source->queue_size = 0;
    source->queue_size_limit = 0;
    source->listeners = 0;
    source->no_mount = 0;
    source->max_listeners = -1;
    source->yp_public = 0;
    util_dict_free (source->audio_info);
    source->audio_info = NULL;

    free(source->fallback_mount);
    source->fallback_mount = NULL;

    free(source->dumpfilename);
    source->dumpfilename = NULL;

    /* Lets clear out the source queue too */
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
}


/* Remove the provided source from the global tree and free it */
void source_free_source (source_t *source)
{
    DEBUG1 ("freeing source \"%s\"", source->mount);
    avl_tree_wlock (global.source_tree);
    avl_delete (global.source_tree, source, NULL);
    avl_tree_unlock (global.source_tree);

    avl_tree_free(source->pending_tree, _free_client);
    avl_tree_free(source->client_tree, _free_client);

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
    /* we don't want the two write locks to deadlock in here */
    thread_mutex_lock (&move_clients_mutex);

    /* if the destination is not running then we can't move clients */

    if (dest->running == 0)
    {
        WARN1 ("destination mount %s not running, unable to move clients ", dest->mount);
        thread_mutex_unlock (&move_clients_mutex);
        return;
    }

    avl_tree_wlock (dest->pending_tree);
    do
    {
        client_t *client;

        /* we need to move the client and pending trees */
        avl_tree_wlock (source->pending_tree);

        if (source->format == NULL)
        {
            INFO1 ("source mount %s is not available", source->mount);
            break;
        }
        if (source->format->type != dest->format->type)
        {
            WARN2 ("stream %s and %s are of different types, ignored", source->mount, dest->mount);
            break;
        }

        while (1)
        {
            avl_node *node = avl_get_first (source->pending_tree);
            if (node == NULL)
                break;
            client = (client_t *)(node->key);
            avl_delete (source->pending_tree, client, NULL);

            /* switch client to different queue */
            client_set_queue (client, dest->stream_data_tail);
            avl_insert (dest->pending_tree, (void *)client);
        }

        avl_tree_wlock (source->client_tree);
        while (1)
        {
            avl_node *node = avl_get_first (source->client_tree);
            if (node == NULL)
                break;

            client = (client_t *)(node->key);
            avl_delete (source->client_tree, client, NULL);

            /* switch client to different queue */
            client_set_queue (client, dest->stream_data_tail);
            avl_insert (dest->pending_tree, (void *)client);
        }
        source->listeners = 0;
        stats_event (source->mount, "listeners", "0");
        avl_tree_unlock (source->client_tree);

    } while (0);

    avl_tree_unlock (source->pending_tree);
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
    while (global.running == ICE_RUNNING && source->running)
    {
        int fds;
        time_t current = time (NULL);

        fds = util_timed_wait_for_fd (source->con->sock, delay);

        if (fds < 0)
        {
            if (! sock_recoverable (sock_error()))
            {
                WARN0 ("Error while waiting on socket, Disconnecting source");
                source->running = 0;
            }
            break;
        }
        if (fds == 0)
        {
            if (source->last_read + (time_t)source->timeout < current)
            {
                DEBUG3 ("last %ld, timeout %ld, now %ld", source->last_read, source->timeout, current);
                WARN0 ("Disconnecting source due to socket timeout");
                source->running = 0;
            }
            break;
        }
        source->last_read = current;
        refbuf = source->format->get_buffer (source);
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

    /* new users need somewhere to start from */
    if (client->refbuf == NULL)
    {
        /* make clients start at the per source burst point on the queue */
        client_set_queue (client, source->burst_point);
        if (client->refbuf == NULL)
           return;
    }

    while (1)
    {
        /* jump out if client connection has died */
        if (client->con->error)
            break;

        /* lets not send too much to one client in one go, but don't
           sleep for too long if more data can be sent */
        if (total_written > 20000 || loop == 0)
        {
            source->short_delay = 1;
            break;
        }

        loop--;

        bytes = source->format->write_buf_to_client (source->format, client);
        if (bytes <= 0)
            break;  /* can't write any more */

        total_written += bytes;
    }

    /* the refbuf referenced at head (last in queue) may be marked for deletion
     * if so, check to see if this client is still referring to it */
    if (deletion_expected && client->refbuf == source->stream_data)
    {
        DEBUG0("Client has fallen too far behind, removing");
        client->con->error = 1;
    }
}


static void source_init (source_t *source)
{
    ice_config_t *config = config_get_config();
    char *listenurl, *str;
    int listen_url_size;

    /* 6 for max size of port */
    listen_url_size = strlen("http://") + strlen(config->hostname) +
        strlen(":") + 6 + strlen(source->mount) + 1;

    listenurl = malloc (listen_url_size);
    memset (listenurl, '\000', listen_url_size);
    snprintf (listenurl, listen_url_size, "http://%s:%d%s",
            config->hostname, config->port, source->mount);
    config_release_config();

    /* maybe better in connection.c */
    if ((str = httpp_getvar(source->parser, "ice-public")))
        source->yp_public = atoi(str);
    if ((str = httpp_getvar(source->parser, "icy-pub")))
        source->yp_public = atoi(str);
    if (str == NULL)
       str = "0";
    stats_event (source->mount, "public", str);

    str = httpp_getvar(source->parser, "ice-audio-info");
    source->audio_info = util_dict_new();
    if (str)
    {
        _parse_audio_info (source, str);
        stats_event (source->mount, "audio_info", str);
    }

    stats_event (source->mount, "listenurl", listenurl);

    if (listenurl) {
        free(listenurl);
    }

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
    source->listeners = 0;
    stats_event_inc (NULL, "sources");
    stats_event_inc (NULL, "source_total_connections");
    stats_event (source->mount, "listeners", "0");
    stats_event (source->mount, "type", source->format->format_description);

    sock_set_blocking (source->con->sock, SOCK_NONBLOCK);

    DEBUG0("Source creation complete");
    source->last_read = time (NULL);
    source->running = 1;

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
    if (source->yp_public)
        yp_add (source);
}


void source_main (source_t *source)
{
    unsigned int listeners;
    refbuf_t *refbuf;
    client_t *client;
    avl_node *client_node;

    source_init (source);

    while (global.running == ICE_RUNNING && source->running) {
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
            if (source->burst_offset > source->burst_size)
            {
                if (source->burst_point->next)
                {
                    refbuf_release (source->burst_point);
                    source->burst_point = source->burst_point->next;
                    source->burst_offset -= source->burst_point->len;
                }
            }

            /* save stream to file */
            if (source->dumpfile && source->format->write_buf_to_file)
                source->format->write_buf_to_file (source, refbuf);
        }
        /* lets see if we have too much data in the queue, but don't remove it until later */
        if (source->queue_size > source->queue_size_limit)
            remove_from_q = 1;

        /* acquire write lock on client_tree */
        avl_tree_wlock(source->client_tree);

        listeners = source->listeners;
        client_node = avl_get_first(source->client_tree);
        while (client_node) {
            client = (client_t *)client_node->key;

            send_to_listener (source, client, remove_from_q);

            if (client->con->error) {
                client_node = avl_get_next(client_node);
                avl_delete(source->client_tree, (void *)client, _free_client);
                source->listeners--;
                DEBUG0("Client removed");
                continue;
            }
            client_node = avl_get_next(client_node);
        }

        /* acquire write lock on pending_tree */
        avl_tree_wlock(source->pending_tree);

        /** add pending clients **/
        client_node = avl_get_first(source->pending_tree);
        while (client_node) {
            if(source->max_listeners != -1 && 
                    source->listeners >= source->max_listeners) 
            {
                /* The common case is caught in the main connection handler,
                 * this deals with rarer cases (mostly concerning fallbacks)
                 * and doesn't give the listening client any information about
                 * why they were disconnected
                 */
                client = (client_t *)client_node->key;
                client_node = avl_get_next(client_node);
                avl_delete(source->pending_tree, (void *)client, _free_client);

                INFO0("Client deleted, exceeding maximum listeners for this "
                        "mountpoint.");
                continue;
            }
            
            /* Otherwise, the client is accepted, add it */
            avl_insert(source->client_tree, client_node->key);

            source->listeners++;
            DEBUG0("Client added");
            stats_event_inc(NULL, "clients");
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
        if (source->listeners != listeners)
        {
            INFO2("listener count on %s now %d", source->mount, source->listeners);
            stats_event_args (source->mount, "listeners", "%d", source->listeners);
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
                    ERROR0 ("queue state is unexpected");
                    source->running = 0;
                    break;
                }
                source->stream_data = to_go->next;
                source->queue_size -= to_go->len;
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
    source->running = 0;
    INFO1("Source \"%s\" exiting", source->mount);

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
    stats_event_dec(NULL, "sources");
    stats_event(source->mount, "listeners", NULL);

    /* we don't remove the source from the tree here, it may be a relay and
       therefore reserved */
    source_clear_source (source);

    global_lock();
    global.sources--;
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

    global_lock();
    global.clients--;
    global_unlock();
    stats_event_dec(NULL, "clients");
    
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
            char name[100], value[100];
            char *esc;

            sscanf (start, "%199[^=]=%199[^;\r\n]", name, value);
            esc = util_url_unescape (value);
            if (esc)
            {
                util_dict_set (source->audio_info, name, esc);
                stats_event (source->mount, name, value);
                free (esc);
            }
        }
        start = s;
    }
}


void source_apply_mount (source_t *source, mount_proxy *mountinfo)
{
    DEBUG1("Applying mount information for \"%s\"", source->mount);
    source->max_listeners = mountinfo->max_listeners;
    source->fallback_override = mountinfo->fallback_override;
    source->no_mount = mountinfo->no_mount;
    if (mountinfo->fallback_mount)
    {
        source->fallback_mount = strdup (mountinfo->fallback_mount);
        DEBUG1 ("fallback %s", mountinfo->fallback_mount);
    }
    if (mountinfo->auth_type != NULL)
    {
        source->authenticator = auth_get_authenticator(
                mountinfo->auth_type, mountinfo->auth_options);
        stats_event(source->mount, "authenticator", mountinfo->auth_type);
    }
    if (mountinfo->dumpfile)
    {
        DEBUG1("Dumping stream to %s", mountinfo->dumpfile);
        source->dumpfilename = strdup (mountinfo->dumpfile);
    }
    if (mountinfo->queue_size_limit)
    {
        source->queue_size_limit = mountinfo->queue_size_limit;
        DEBUG1 ("queue size to %u", source->queue_size_limit);
    }
    if (mountinfo->source_timeout)
    {
        source->timeout = mountinfo->source_timeout;
        DEBUG1 ("source timeout to %u", source->timeout);
    }
    if (mountinfo->burst_size > -1)
        source->burst_size = mountinfo->burst_size;
    DEBUG1 ("amount to burst on client connect set to %u", source->burst_size);
}


void *source_client_thread (void *arg)
{
    source_t *source = arg;
    const char ok_msg[] = "HTTP/1.0 200 OK\r\n\r\n";
    int bytes;

    source->client->respcode = 200;
    bytes = sock_write_bytes (source->client->con->sock, ok_msg, sizeof (ok_msg)-1);
    if (bytes < sizeof (ok_msg)-1)
    {
        global_lock();
        global.sources--;
        global_unlock();
        WARN0 ("Error writing 200 OK message to source client");
    }
    else
    {
        source->client->con->sent_bytes += bytes;

        stats_event_inc(NULL, "source_client_connections");
        source_main (source);
    }
    source_free_source (source);
    return NULL;
}

