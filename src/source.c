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

#include "thread.h"
#include "avl.h"
#include "httpp.h"
#include "sock.h"

#include "connection.h"
#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "log.h"
#include "logging.h"
#include "config.h"
#include "util.h"
#include "geturl.h"
#include "source.h"
#include "format.h"

#undef CATMODULE
#define CATMODULE "source"

#define  YP_SERVER_NAME 1
#define  YP_SERVER_DESC 2
#define  YP_SERVER_GENRE 3
#define  YP_SERVER_URL 4
#define  YP_BITRATE 5
#define  YP_AUDIO_INFO 6
#define  YP_SERVER_TYPE 7
#define  YP_CURRENT_SONG 8
#define  YP_URL_TIMEOUT 9
#define  YP_TOUCH_INTERVAL 10
#define  YP_LAST_TOUCH 11

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _remove_client(void *key);
static int _free_client(void *key);
static int _parse_audio_info(source_t *source, char *s);
static void _add_yp_info(source_t *source, char *stat_name, 
            void *info, int type);

source_t *source_create(client_t *client, connection_t *con, 
    http_parser_t *parser, const char *mount, format_type_t type, 
    mount_proxy *mountinfo)
{
	int	i = 0;
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
	for (i=0;i<config_get_config()->num_yp_directories;i++) {
		if (config_get_config()->yp_url[i]) {
			src->ypdata[src->num_yp_directories] = yp_create_ypdata();
			src->ypdata[src->num_yp_directories]->yp_url = 
                config_get_config()->yp_url[i];
			src->ypdata[src->num_yp_directories]->yp_url_timeout = 
                config_get_config()->yp_url_timeout[i];
			src->ypdata[src->num_yp_directories]->yp_touch_interval = 0;
			src->num_yp_directories++;
		}
	}

    if(mountinfo != NULL) {
        src->fallback_mount = mountinfo->fallback_mount;
        src->max_listeners = mountinfo->max_listeners;
        src->dumpfilename = mountinfo->dumpfile;
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
	source_t *source = (source_t *)key;
	int i=0;

	free(source->mount);
    free(source->fallback_mount);
    client_destroy(source->client);
	avl_tree_free(source->pending_tree, _free_client);
	avl_tree_free(source->client_tree, _free_client);
	source->format->free_plugin(source->format);
    for (i=0; i<source->num_yp_directories; i++) {
        yp_destroy_ypdata(source->ypdata[i]);
    }
    util_dict_free(source->audio_info);
	free(source);

	return 1;
}
	

/* The caller MUST have a current write lock on global.source_tree when calling
 * this
 */
void *source_main(void *arg)
{
	source_t *source = (source_t *)arg;
    source_t *fallback_source;
	char buffer[4096];
	long bytes, sbytes;
	int ret, timeout;
	client_t *client;
	avl_node *client_node;
	char *s;
	long current_time;
	char current_song[256];

	refbuf_t *refbuf, *abuf;
	int data_done;

    int listeners = 0;
    int	i=0;
    int	suppress_yp = 0;
    char *ai;

    long queue_limit = config_get_config()->queue_size_limit;

	timeout = config_get_config()->source_timeout;

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
	stats_event(source->mount, "listeners", "0");
	source->listeners = 0;
	if ((s = httpp_getvar(source->parser, "ice-name"))) {
        _add_yp_info(source, "server_name", s, YP_SERVER_NAME);
	}
	if ((s = httpp_getvar(source->parser, "ice-url"))) {
        _add_yp_info(source, "server_url", s, YP_SERVER_URL);
	}
	if ((s = httpp_getvar(source->parser, "ice-genre"))) {
        _add_yp_info(source, "genre", s, YP_SERVER_GENRE);
	}
	if ((s = httpp_getvar(source->parser, "ice-bitrate"))) {
        _add_yp_info(source, "bitrate", s, YP_BITRATE);
	}
	if ((s = httpp_getvar(source->parser, "ice-description"))) {
        _add_yp_info(source, "server_description", s, YP_SERVER_DESC);
	}
	if ((s = httpp_getvar(source->parser, "ice-private"))) {
		stats_event(source->mount, "public", s);
		suppress_yp = atoi(s);
	}
	if ((s = httpp_getvar(source->parser, "ice-audio-info"))) {
        if (_parse_audio_info(source, s)) {
            ai = util_dict_urlencode(source->audio_info, '&');
            _add_yp_info(source, "audio_info", 
                    ai,
                    YP_AUDIO_INFO);
        }
	}
	for (i=0;i<source->num_yp_directories;i++) {
		if (source->ypdata[i]->server_type) {
			free(source->ypdata[i]->server_type);
		}
		source->ypdata[i]->server_type = malloc(
                strlen(source->format->format_description) + 1);
		strcpy(source->ypdata[i]->server_type, 
                source->format->format_description);
	}
    stats_event(source->mount, "type", source->format->format_description);

	for (i=0;i<source->num_yp_directories;i++) {
        int listen_url_size;
		if (source->ypdata[i]->listen_url) {
			free(source->ypdata[i]->listen_url);
		}
		/* 6 for max size of port */
		listen_url_size = strlen("http://") + 
            strlen(config_get_config()->hostname) + 
            strlen(":") + 6 + strlen(source->mount) + 1;
		source->ypdata[i]->listen_url = malloc(listen_url_size);
		sprintf(source->ypdata[i]->listen_url, "http://%s:%d%s", 
                config_get_config()->hostname, config_get_config()->port, 
                source->mount);
	}

	if(!suppress_yp) {
        yp_add(source, YP_ADD_ALL);

    	current_time = time(NULL);

        _add_yp_info(source, "last_touch", (void *)current_time, 
            YP_LAST_TOUCH);

	    for (i=0;i<source->num_yp_directories;i++) {
            /* Give the source 5 seconds to update the metadata
               before we do our first touch */
            source->ypdata[i]->yp_last_touch = current_time - 
                source->ypdata[i]->yp_touch_interval + 5;
            /* Don't permit touch intervals of less than 30 seconds */
	    	if (source->ypdata[i]->yp_touch_interval <= 30) {
		    	source->ypdata[i]->yp_touch_interval = 30;
    		}
	    }
    }

    DEBUG0("Source creation complete");

	while (global.running == ICE_RUNNING && source->running) {
		if(!suppress_yp) {
            current_time = time(NULL);
			for (i=0;i<source->num_yp_directories;i++) {
				if (current_time > (source->ypdata[i]->yp_last_touch + 
                            source->ypdata[i]->yp_touch_interval)) {
                    current_song[0] = 0;
					if (stats_get_value(source->mount, "artist")) {
						strncat(current_song, 
                                stats_get_value(source->mount, "artist"), 
                                sizeof(current_song) - 1);
						if (strlen(current_song) + 4 < sizeof(current_song)) {
							strncat(current_song, " - ", 3);
						}
					}
					if (stats_get_value(source->mount, "title")) {
						if (strlen(current_song) + 
                                strlen(stats_get_value(source->mount, "title"))
                                < sizeof(current_song) -1) 
                        {
							strncat(current_song, 
                                    stats_get_value(source->mount, "title"), 
                                    sizeof(current_song) - 1 - 
                                    strlen(current_song));
						}
					}
                    
					if (source->ypdata[i]->current_song) {
						free(source->ypdata[i]->current_song);
						source->ypdata[i]->current_song = NULL;
					}
	
					source->ypdata[i]->current_song = 
                        malloc(strlen(current_song) + 1);
					strcpy(source->ypdata[i]->current_song, current_song);
	
					thread_create("YP Touch Thread", yp_touch_thread, 
                            (void *)source, THREAD_DETACHED); 
				}
			}
		}

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
			avl_delete(source->pending_tree, avl_get_first(source->pending_tree)->key, _remove_client);
		}

		/* release write lock on pending_tree */
		avl_tree_unlock(source->pending_tree);

		/* release write lock on client_tree */
		avl_tree_unlock(source->client_tree);
	}

done:

    DEBUG0("Source exiting");
	if(!suppress_yp) {
		yp_remove(source);
	}

    avl_tree_rlock(global.source_tree);
    fallback_source = source_find_mount(source->fallback_mount);
    avl_tree_unlock(global.source_tree);

	/* we need to empty the client and pending trees */
	avl_tree_wlock(source->pending_tree);
	while (avl_get_first(source->pending_tree)) {
        client_t *client = (client_t *)avl_get_first(
                source->pending_tree)->key;
        if(fallback_source) {
            avl_delete(source->pending_tree, client, _remove_client);

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
            avl_delete(source->client_tree, client, _remove_client);

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

	/* release our hold on the lock so the main thread can continue cleaning up */
	thread_rwlock_unlock(source->shutdown_rwlock);

	avl_tree_wlock(global.source_tree);
	avl_delete(global.source_tree, source, source_free_source);
	avl_tree_unlock(global.source_tree);

    if(source->dumpfile)
        fclose(source->dumpfile);

	thread_exit(0);
      
	return NULL;
}

static int _compare_clients(void *compare_arg, void *a, void *b)
{
	connection_t *cona = (connection_t *)a;
    connection_t *conb = (connection_t *)b;

	if (cona->id < conb->id) return -1;
	if (cona->id > conb->id) return 1;

	return 0;
}

static int _remove_client(void *key)
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
            memset(variable, '\000', pvar-token+1);
            strncpy(variable, token, pvar-token);	
            pvar++;
            if (strlen(pvar)) {
                value = (char *)malloc(strlen(pvar)+1);
                memset(value, '\000', strlen(pvar)+1);
                strncpy(value, pvar, strlen(pvar));	
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

static void _add_yp_info(source_t *source, char *stat_name, 
            void *info, int type)
{
    int i;
    if (!info) {
        return;
    }
    for (i=0;i<source->num_yp_directories;i++) {
        switch (type) {
        case YP_SERVER_NAME:
        if (source->ypdata[i]->server_name) {
                free(source->ypdata[i]->server_name);
                }
                source->ypdata[i]->server_name = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->server_name, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
                break;
        case YP_SERVER_DESC:
                if (source->ypdata[i]->server_desc) {
                    free(source->ypdata[i]->server_desc);
                }
                source->ypdata[i]->server_desc = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->server_desc, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
                break;
        case YP_SERVER_GENRE:
                if (source->ypdata[i]->server_genre) {
                    free(source->ypdata[i]->server_genre);
                }
                source->ypdata[i]->server_genre = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->server_genre, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
                break;
        case YP_SERVER_URL:
                if (source->ypdata[i]->server_url) {
                    free(source->ypdata[i]->server_url);
                }
                source->ypdata[i]->server_url = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->server_url, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
                break;
        case YP_BITRATE:
                if (source->ypdata[i]->bitrate) {
                    free(source->ypdata[i]->bitrate);
                }
                source->ypdata[i]->bitrate = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->bitrate, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
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
                if (source->ypdata[i]->server_type) {
                    free(source->ypdata[i]->server_type);
                }
                source->ypdata[i]->server_type = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->server_type, (char *)info);
                break;
        case YP_CURRENT_SONG:
                if (source->ypdata[i]->current_song) {
                    free(source->ypdata[i]->current_song);
                }
                source->ypdata[i]->current_song = 
                    malloc(strlen((char *)info) +1);
                strcpy(source->ypdata[i]->current_song, (char *)info);
                stats_event(source->mount, stat_name, (char *)info);
                break;
        case YP_URL_TIMEOUT:
                source->ypdata[i]->yp_url_timeout = (int)info;
                break;
        case YP_LAST_TOUCH:
                source->ypdata[i]->yp_last_touch = (int)info;
                break;
        case YP_TOUCH_INTERVAL:
                source->ypdata[i]->yp_touch_interval = (int)info;
                break;
        }
    }
}
