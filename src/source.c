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
#ifdef USE_YP
#include "geturl.h"
#endif
#include "source.h"
#include "format.h"

#undef CATMODULE
#define CATMODULE "source"

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _free_client(void *key);
static int _parse_audio_info(source_t *source, char *s);

source_t *source_create(client_t *client, connection_t *con, 
    http_parser_t *parser, const char *mount, format_type_t type, 
    mount_proxy *mountinfo)
{
    source_t *src;

    src = (source_t *)malloc(sizeof(source_t));
    src->client = client;
    src->mount = (char *)strdup(mount);
    src->fallback_mount = NULL;
    src->format = format_get_plugin(type, src->mount, parser);
    src->con = con;
    src->parser = parser;
    src->client_tree = avl_tree_new(_compare_clients, NULL);
    src->pending_tree = avl_tree_new(_compare_clients, NULL);
    src->running = 1;
    src->num_yp_directories = 0;
    src->listeners = 0;
    src->max_listeners = -1;
    src->send_return = 0;
    src->dumpfilename = NULL;
    src->dumpfile = NULL;
    src->audio_info = util_dict_new();
    src->yp_public = 0;

    if(mountinfo != NULL) {
        if (mountinfo->fallback_mount != NULL)
            src->fallback_mount = strdup (mountinfo->fallback_mount);
        src->max_listeners = mountinfo->max_listeners;
        if (mountinfo->dumpfile != NULL)
            src->dumpfilename = strdup (mountinfo->dumpfile);
    }

    if(src->dumpfilename != NULL) {
        src->dumpfile = fopen(src->dumpfilename, "ab");
        if(src->dumpfile == NULL) {
            WARN2("Cannot open dump file \"%s\" for appending: %s, disabling.",
                    src->dumpfilename, strerror(errno));
        }
    }

    return src;
}

static int source_remove_source(void *key)
{
    return 1;
}

/* you must already have a read lock on the global source tree
** to call this function
*/
source_t *source_find_mount(const char *mount)
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

int source_compare_sources(void *arg, void *a, void *b)
{
    source_t *srca = (source_t *)a;
    source_t *srcb = (source_t *)b;

    return strcmp(srca->mount, srcb->mount);
}

int source_free_source(void *key)
{
    source_t *source = key;
#ifdef USE_YP
    int i;
#endif

    free(source->mount);
    free(source->fallback_mount);
    free(source->dumpfilename);
    client_destroy(source->client);
    avl_tree_free(source->pending_tree, _free_client);
    avl_tree_free(source->client_tree, _free_client);
    source->format->free_plugin(source->format);
#ifdef USE_YP
    for (i=0; i<source->num_yp_directories; i++) {
        yp_destroy_ypdata(source->ypdata[i]);
    }
#endif
    util_dict_free(source->audio_info);
    free(source);

    return 1;
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
    

void *source_main(void *arg)
{
    source_t *source = (source_t *)arg;
    source_t *fallback_source;
    char buffer[4096];
    long bytes, sbytes;
    int ret, timeout;
    client_t *client;
    avl_node *client_node;

    refbuf_t *refbuf, *abuf;
    int data_done;

    int listeners = 0;
#ifdef USE_YP
    char *s;
    long current_time;
    int    i;
    char *ai;
    int listen_url_size;
#endif

    long queue_limit;
    ice_config_t *config;
    char *hostname;
    int port;

    config = config_get_config();
    
    queue_limit = config->queue_size_limit;
    timeout = config->source_timeout;
    hostname = config->hostname;
    port = config->port;

#ifdef USE_YP
    for (i=0;i<config->num_yp_directories;i++) {
        if (config->yp_url[i]) {
            source->ypdata[source->num_yp_directories] = yp_create_ypdata();
            source->ypdata[source->num_yp_directories]->yp_url = 
                config->yp_url[i];
            source->ypdata[source->num_yp_directories]->yp_url_timeout = 
                config->yp_url_timeout[i];
            source->ypdata[source->num_yp_directories]->yp_touch_interval = 0;
            source->num_yp_directories++;
        }
    }
#endif
    
    config_release_config();

    /* grab a read lock, to make sure we get a chance to cleanup */
    thread_rwlock_rlock(source->shutdown_rwlock);

    avl_tree_wlock(global.source_tree);
    /* Now, we must do a final check with write lock taken out that the
     * mountpoint is available..
     */
    if (source_find_mount(source->mount) != NULL) {
        avl_tree_unlock(global.source_tree);
        if(source->send_return) {
            client_send_404(source->client, "Mountpoint in use");
        }
        global_lock();
        global.sources--;
        global_unlock();
        thread_rwlock_unlock(source->shutdown_rwlock);
        thread_exit(0);
        return NULL;
    }
    /* insert source onto source tree */
    avl_insert(global.source_tree, (void *)source);
    /* release write lock on global source tree */
    avl_tree_unlock(global.source_tree);

    /* If we connected successfully, we can send the message (if requested)
     * back
     */
    if(source->send_return) {
        source->client->respcode = 200;
        bytes = sock_write(source->client->con->sock, 
                "HTTP/1.0 200 OK\r\n\r\n");
        if(bytes > 0) source->client->con->sent_bytes = bytes;
    }

    /* start off the statistics */
    source->listeners = 0;
    stats_event(source->mount, "listeners", "0");
    stats_event(source->mount, "type", source->format->format_description);
#ifdef USE_YP
    /* ice-* is icecast, icy-* is shoutcast */
    if ((s = httpp_getvar(source->parser, "ice-url"))) {
        add_yp_info(source, "server_url", s, YP_SERVER_URL);
    }
    if ((s = httpp_getvar(source->parser, "ice-name"))) {
        add_yp_info(source, "server_name", s, YP_SERVER_NAME);
    }
    if ((s = httpp_getvar(source->parser, "icy-name"))) {
        add_yp_info(source, "server_name", s, YP_SERVER_NAME);
    }
    if ((s = httpp_getvar(source->parser, "ice-url"))) {
        add_yp_info(source, "server_url", s, YP_SERVER_URL);
    }
    if ((s = httpp_getvar(source->parser, "icy-url"))) {
        add_yp_info(source, "server_url", s, YP_SERVER_URL);
    }
    if ((s = httpp_getvar(source->parser, "ice-genre"))) {
        add_yp_info(source, "genre", s, YP_SERVER_GENRE);
    }
    if ((s = httpp_getvar(source->parser, "icy-genre"))) {
        add_yp_info(source, "genre", s, YP_SERVER_GENRE);
    }
    if ((s = httpp_getvar(source->parser, "ice-bitrate"))) {
        add_yp_info(source, "bitrate", s, YP_BITRATE);
    }
    if ((s = httpp_getvar(source->parser, "icy-br"))) {
        add_yp_info(source, "bitrate", s, YP_BITRATE);
    }
    if ((s = httpp_getvar(source->parser, "ice-description"))) {
        add_yp_info(source, "server_description", s, YP_SERVER_DESC);
    }
    if ((s = httpp_getvar(source->parser, "ice-public"))) {
        stats_event(source->mount, "public", s);
        source->yp_public = atoi(s);
    }
    if ((s = httpp_getvar(source->parser, "icy-pub"))) {
        stats_event(source->mount, "public", s);
        source->yp_public = atoi(s);
    }
    if ((s = httpp_getvar(source->parser, "ice-audio-info"))) {
        stats_event(source->mount, "audio_info", s);
        if (_parse_audio_info(source, s)) {
            ai = util_dict_urlencode(source->audio_info, '&');
            add_yp_info(source, "audio_info", 
                    ai,
                    YP_AUDIO_INFO);
            if (ai) {
                free(ai);
            }
        }
    }
    for (i=0;i<source->num_yp_directories;i++) {
        add_yp_info(source, "server_type", 
                     source->format->format_description,
                     YP_SERVER_TYPE);
        if (source->ypdata[i]->listen_url) {
            free(source->ypdata[i]->listen_url);
        }
        /* 6 for max size of port */
        listen_url_size = strlen("http://") + 
            strlen(hostname) + 
            strlen(":") + 6 + strlen(source->mount) + 1;
        source->ypdata[i]->listen_url = malloc(listen_url_size);
        sprintf(source->ypdata[i]->listen_url, "http://%s:%d%s", 
                hostname, port, source->mount);
    }

    if(source->yp_public) {

        current_time = time(NULL);

        for (i=0;i<source->num_yp_directories;i++) {
            /* Give the source 5 seconds to update the metadata
               before we do our first touch */
            /* Don't permit touch intervals of less than 30 seconds */
            if (source->ypdata[i]->yp_touch_interval <= 30) {
                source->ypdata[i]->yp_touch_interval = 30;
            }
            source->ypdata[i]->yp_last_touch = 0;
        }
    }
#endif

    DEBUG0("Source creation complete");

    while (global.running == ICE_RUNNING && source->running) {
        ret = source->format->get_buffer(source->format, NULL, 0, &refbuf);
        if(ret < 0) {
            WARN0("Bad data from source");
            break;
        }
        bytes = 1; /* Set to > 0 so that the post-loop check won't be tripped */
        while (refbuf == NULL) {
            bytes = 0;
            while (bytes <= 0) {
                ret = util_timed_wait_for_fd(source->con->sock, timeout*1000);

                if (ret < 0 && sock_recoverable (sock_error()))
                   continue;
                if (ret <= 0) { /* timeout expired */
                    WARN1("Disconnecting source: socket timeout (%d s) expired",
                           timeout);
                    bytes = 0;
                    break;
                }

                bytes = sock_read_bytes(source->con->sock, buffer, 4096);
                if (bytes == 0 || (bytes < 0 && !sock_recoverable(sock_error()))) {
                    DEBUG1("Disconnecting source due to socket read error: %s",
                            strerror(sock_error()));
                    break;
                }
            }
            if (bytes <= 0) break;
            source->client->con->sent_bytes += bytes;
            ret = source->format->get_buffer(source->format, buffer, bytes, &refbuf);
            if(ret < 0) {
                WARN0("Bad data from source");
                goto done;
            }
        }

        if (bytes <= 0) {
            INFO0("Removing source following disconnection");
            break;
        }

        /* we have a refbuf buffer, which a data block to be sent to 
        ** all clients.  if a client is not able to send the buffer
        ** immediately, it should store it on its queue for the next
        ** go around.
        **
        ** instead of sending the current block, a client should send
        ** all data in the queue, plus the current block, until either
        ** it runs out of data, or it hits a recoverable error like
        ** EAGAIN.  this will allow a client that got slightly lagged
        ** to catch back up if it can
        */

        /* First, stream dumping, if enabled */
        if(source->dumpfile) {
            if(fwrite(refbuf->data, 1, refbuf->len, source->dumpfile) !=
                    refbuf->len) 
            {
                WARN1("Write to dump file failed, disabling: %s", 
                        strerror(errno));
                fclose(source->dumpfile);
                source->dumpfile = NULL;
            }
        }

        /* acquire read lock on client_tree */
        avl_tree_rlock(source->client_tree);

        client_node = avl_get_first(source->client_tree);
        while (client_node) {
            /* acquire read lock on node */
            avl_node_wlock(client_node);

            client = (client_t *)client_node->key;
            
            data_done = 0;

            /* do we have any old buffers? */
            abuf = refbuf_queue_remove(&client->queue);
            while (abuf) {
                if (client->pos > 0)
                    bytes = abuf->len - client->pos;
                else
                    bytes = abuf->len;

                sbytes = source->format->write_buf_to_client(source->format,
                        client, &abuf->data[client->pos], bytes);
                if (sbytes >= 0) {
                    if(sbytes != bytes) {
                        /* We didn't send the entire buffer. Leave it for
                         * the moment, handle it in the next iteration.
                         */
                        client->pos += sbytes;
                        refbuf_queue_insert(&client->queue, abuf);
                        data_done = 1;
                        break;
                    }
                }
                else {
                    DEBUG0("Client has unrecoverable error catching up. Client has probably disconnected");
                    client->con->error = 1;
                    data_done = 1;
                    refbuf_release(abuf);
                    break;
                }
                
                /* we're done with that refbuf, release it and reset the pos */
                refbuf_release(abuf);
                client->pos = 0;

                abuf = refbuf_queue_remove(&client->queue);
            }
            
            /* now send or queue the new data */
            if (data_done) {
                refbuf_addref(refbuf);
                refbuf_queue_add(&client->queue, refbuf);
            } else {
                sbytes = source->format->write_buf_to_client(source->format,
                        client, refbuf->data, refbuf->len);
                if (sbytes >= 0) {
                    if(sbytes != refbuf->len) {
                        /* Didn't send the entire buffer, queue it */
                        client->pos = sbytes;
                        refbuf_addref(refbuf);
                        refbuf_queue_insert(&client->queue, refbuf);
                    }
                }
                else {
                    DEBUG0("Client had unrecoverable error with new data, probably due to client disconnection");
                    client->con->error = 1;
                }
            }

            /* if the client is too slow, its queue will slowly build up.
            ** we need to make sure the client is keeping up with the
            ** data, so we'll kick any client who's queue gets to large.
            */
            if (refbuf_queue_length(&client->queue) > queue_limit) {
                DEBUG0("Client has fallen too far behind, removing");
                client->con->error = 1;
            }

            /* release read lock on node */
            avl_node_unlock(client_node);

            /* get the next node */
            client_node = avl_get_next(client_node);
        }
        /* release read lock on client_tree */
        avl_tree_unlock(source->client_tree);

        refbuf_release(refbuf);

        /* acquire write lock on client_tree */
        avl_tree_wlock(source->client_tree);

        /** delete bad clients **/
        client_node = avl_get_first(source->client_tree);
        while (client_node) {
            client = (client_t *)client_node->key;
            if (client->con->error) {
                client_node = avl_get_next(client_node);
                avl_delete(source->client_tree, (void *)client, _free_client);
                listeners--;
                stats_event_args(source->mount, "listeners", "%d", listeners);
                source->listeners = listeners;
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
            avl_insert(source->client_tree, client_node->key);
            /* listener count may have changed */
            listeners = source->listeners;
            listeners++;
            DEBUG0("Client added");
            stats_event_inc(NULL, "clients");
            stats_event_inc(source->mount, "connections");
            stats_event_args(source->mount, "listeners", "%d", listeners);
            source->listeners = listeners;

            /* we have to send cached headers for some data formats
            ** this is where we queue up the buffers to send
            */
            if (source->format->has_predata) {
                client = (client_t *)client_node->key;
                client->queue = source->format->get_predata(source->format);
            }

            client_node = avl_get_next(client_node);
        }

        /** clear pending tree **/
        while (avl_get_first(source->pending_tree)) {
            avl_delete(source->pending_tree, avl_get_first(source->pending_tree)->key, source_remove_client);
        }

        /* release write lock on pending_tree */
        avl_tree_unlock(source->pending_tree);

        /* release write lock on client_tree */
        avl_tree_unlock(source->client_tree);
    }

done:

    INFO1("Source \"%s\" exiting", source->mount);

#ifdef USE_YP
    if(source->yp_public) {
        yp_remove(source);
    }
#endif
    
    avl_tree_rlock(global.source_tree);
    fallback_source = source_find_mount(source->fallback_mount);
    avl_tree_unlock(global.source_tree);

    /* Now, we must remove this source from the source tree before
     * removing the clients, otherwise new clients can sneak into the pending
     * tree after we've cleared it
     */
    avl_tree_wlock(global.source_tree);
    avl_delete(global.source_tree, source, source_remove_source);
    avl_tree_unlock(global.source_tree);

    /* we need to empty the client and pending trees */
    avl_tree_wlock(source->pending_tree);
    while (avl_get_first(source->pending_tree)) {
        client_t *client = (client_t *)avl_get_first(
                source->pending_tree)->key;
        if(fallback_source) {
            avl_delete(source->pending_tree, client, source_remove_client);

            /* TODO: reset client local format data?  */
            avl_tree_wlock(fallback_source->pending_tree);
            avl_insert(fallback_source->pending_tree, (void *)client);
            avl_tree_unlock(fallback_source->pending_tree);
        }
        else {
            avl_delete(source->pending_tree, client, _free_client);
        }
    }
    avl_tree_unlock(source->pending_tree);

    avl_tree_wlock(source->client_tree);
    while (avl_get_first(source->client_tree)) {
        client_t *client = (client_t *)avl_get_first(source->client_tree)->key;

        if(fallback_source) {
            avl_delete(source->client_tree, client, source_remove_client);

            /* TODO: reset client local format data?  */
            avl_tree_wlock(fallback_source->pending_tree);
            avl_insert(fallback_source->pending_tree, (void *)client);
            avl_tree_unlock(fallback_source->pending_tree);
        }
        else {
            avl_delete(source->client_tree, client, _free_client);
        }
    }
    avl_tree_unlock(source->client_tree);

    /* delete this sources stats */
    stats_event_dec(NULL, "sources");
    stats_event(source->mount, "listeners", NULL);

    global_lock();
    global.sources--;
    global_unlock();

    if(source->dumpfile)
        fclose(source->dumpfile);

    /* release our hold on the lock so the main thread can continue cleaning up */
    thread_rwlock_unlock(source->shutdown_rwlock);

    source_free_source(source);

    thread_exit(0);
      
    return NULL;
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

static int _parse_audio_info(source_t *source, char *s)
{
    char *token = NULL;
    char *pvar = NULL;
    char *variable = NULL;
    char *value = NULL;

    while ((token = strtok(s,";")) != NULL) {
        pvar = strchr(token, '=');
        if (pvar) {
            variable = (char *)malloc(pvar-token+1);
            strncpy(variable, token, pvar-token);    
            variable[pvar-token] = 0;
            pvar++;
            if (strlen(pvar)) {
                value = util_url_unescape(pvar);
                util_dict_set(source->audio_info, variable, value);
                stats_event(source->mount, variable, value);
                if (value) {
                    free(value);
                }
            }
            if (variable) {
                free(variable);
            }
        }
        s = NULL;
    }
    return 1;
}
