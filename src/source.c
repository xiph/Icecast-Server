#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ogg/ogg.h>

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
#include "format.h"
#include "log.h"
#include "logging.h"
#include "config.h"
#include "util.h"

#include "source.h"

#undef CATMODULE
#define CATMODULE "source"

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _remove_client(void *key);
static int _free_client(void *key);

source_t *source_create(connection_t *con, http_parser_t *parser, const char *mount, format_type_t type)
{
	source_t *src;

	src = (source_t *)malloc(sizeof(source_t));
	src->mount = (char *)strdup(mount);
	src->format = format_get_plugin(type, src->mount);
	src->con = con;
	src->parser = parser;
	src->client_tree = avl_tree_new(_compare_clients, NULL);
	src->pending_tree = avl_tree_new(_compare_clients, NULL);

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

	free(source->mount);
	connection_close(source->con);
	httpp_destroy(source->parser);
	avl_tree_free(source->pending_tree, _free_client);
	avl_tree_free(source->client_tree, _free_client);
	source->format->free_plugin(source->format);
	free(source);

	return 1;
}
	

void *source_main(void *arg)
{
	source_t *source = (source_t *)arg;
	char buffer[4096];
	long bytes, sbytes;
	int ret, timeout;
	client_t *client;
	avl_node *client_node;
	char *s;

	refbuf_t *refbuf, *abuf;
	int data_done;

	int listeners = 0;

	timeout = config_get_config()->source_timeout;

	/* grab a read lock, to make sure we get a chance to cleanup */
	thread_rwlock_rlock(source->shutdown_rwlock);

	/* get a write lock on the global source tree */
	avl_tree_wlock(global.source_tree);
	/* insert source onto source tree */
	avl_insert(global.source_tree, (void *)source);
	/* release write lock on global source tree */
	avl_tree_unlock(global.source_tree);


	/* start off the statistics */
	stats_event(source->mount, "listeners", "0");
	if ((s = httpp_getvar(source->parser, "ice-name")))
		stats_event(source->mount, "name", s);
	if ((s = httpp_getvar(source->parser, "ice-url")))
		stats_event(source->mount, "url", s);
	if ((s = httpp_getvar(source->parser, "ice-bitrate")))
		stats_event(source->mount, "bitrate", s);
	if ((s = httpp_getvar(source->parser, "ice-description")))
		stats_event(source->mount, "description", s);

	while (global.running == ICE_RUNNING) {
		ret = source->format->get_buffer(source->format, NULL, 0, &refbuf);
        if(ret < 0) {
            WARN0("Bad data from source");
            break;
        }
		while (refbuf == NULL) {
			bytes = 0;
			while (bytes <= 0) {
                ret = util_timed_wait_for_fd(source->con->sock, timeout*1000);

				if (ret <= 0) { /* timeout expired */
					bytes = 0;
					break;
				}

				bytes = sock_read_bytes(source->con->sock, buffer, 4096);
				if (bytes == 0 || (bytes < 0 && !sock_recoverable(sock_error()))) 
                    break;
			}
			if (bytes <= 0) break;
			ret = source->format->get_buffer(source->format, buffer, bytes, &refbuf);
            if(ret < 0) {
                WARN0("Bad data from source");
                goto done;
            }
		}

		if (bytes <= 0) {
			printf("DEBUG: got 0 bytes reading data, the source must have disconnected...\n");
			INFO0("Disconnecting lame source...");
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

				sbytes = sock_write_bytes(client->con->sock, &abuf->data[client->pos], bytes);
				if (sbytes >= 0) {
                    client->con->sent_bytes += sbytes;
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
				if (sbytes < 0) {
					if (!sock_recoverable(sock_error())) {
						printf("SOURCE: Client had unrecoverable error catching up (%ld/%ld)\n", sbytes, bytes);
						client->con->error = 1;
					} else {
						printf("SOURCE: client had recoverable error...\n");
						/* put the refbuf back on top of the queue, since we didn't finish with it */
						refbuf_queue_insert(&client->queue, abuf);
					}
					
					data_done = 1;
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
				sbytes = sock_write_bytes(client->con->sock, refbuf->data, refbuf->len);
				if (sbytes >= 0) {
                    client->con->sent_bytes += sbytes;
                    if(sbytes != refbuf->len) {
                        /* Didn't send the entire buffer, queue it */
                        client->pos = sbytes;
						refbuf_addref(refbuf);
                        refbuf_queue_insert(&client->queue, refbuf);
                    }
                }
				if (sbytes < 0) {
					bytes = sock_error();
					if (!sock_recoverable(bytes)) {
						printf("SOURCE: client had unrecoverable error %ld with new data (%ld/%ld)\n", bytes, sbytes, refbuf->len);
						client->con->error = 1;
					} else {
						printf("SOURCE: recoverable error %ld\n", bytes);
						client->pos = 0;
						refbuf_addref(refbuf);
						refbuf_queue_insert(&client->queue, refbuf);
					}
				}
			}

			/* if the client is too slow, its queue will slowly build up.
			** we need to make sure the client is keeping up with the
			** data, so we'll kick any client who's queue gets to large.
			** the queue_limit might need to be tuned, but should work fine.
			** TODO: put queue_limit in a config file
			*/
			if (refbuf_queue_size(&client->queue) > 25) {
				printf("SOURCE: client is too lagged... kicking\n");
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
				global_lock();
				global.clients--;
				global_unlock();
				stats_event_dec(NULL, "clients");
				stats_event_args(source->mount, "listeners", "%d", listeners);
				printf("DEBUG: Client dropped...\n");
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
			printf("Client added\n");
			stats_event_inc(NULL, "clients");
			stats_event_inc(source->mount, "connections");
			stats_event_args(source->mount, "listeners", "%d", listeners);

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

	printf("DEBUG: we're going down...\n");

	/* we need to empty the client and pending trees */
	avl_tree_wlock(source->pending_tree);
	while (avl_get_first(source->pending_tree)) {
		avl_delete(source->pending_tree, avl_get_first(source->pending_tree)->key, _free_client);
	}
	avl_tree_unlock(source->pending_tree);
	avl_tree_wlock(source->client_tree);
	while (avl_get_first(source->client_tree)) {
		avl_delete(source->client_tree, avl_get_first(source->client_tree)->key, _free_client);
	}
	avl_tree_unlock(source->client_tree);

	/* delete this sources stats */
	stats_event_dec(NULL, "sources");
	stats_event(source->mount, "listeners", NULL);

	printf("DEBUG: source_main() is now exiting...\n");

	global_lock();
	global.sources--;
	global_unlock();

	/* release our hold on the lock so the main thread can continue cleaning up */
	thread_rwlock_unlock(source->shutdown_rwlock);

	avl_tree_wlock(global.source_tree);
	avl_delete(global.source_tree, source, source_free_source);
	avl_tree_unlock(global.source_tree);

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
	
	client_destroy(client);
	
	return 1;
}
