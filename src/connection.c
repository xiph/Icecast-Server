#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#else
#define snprintf _snprintf
#define strcasecmp stricmp
#endif

#include "os.h"

#include "thread.h"
#include "avl.h"
#include "sock.h"
#include "log.h"
#include "httpp.h"

#include "config.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "format.h"
#include "logging.h"

#include "source.h"

#define CATMODULE "connection"

typedef struct con_queue_tag {
	connection_t *con;
	struct con_queue_tag *next;
} con_queue_t;

typedef struct _thread_queue_tag {
	long thread_id;
	struct _thread_queue_tag *next;
} thread_queue_t;

static mutex_t _connection_mutex;
static unsigned long _current_id = 0;
static int _initialized = 0;
static cond_t _pool_cond;

static con_queue_t *_queue = NULL;
static mutex_t _queue_mutex;

static thread_queue_t *_conhands = NULL;

static rwlock_t _source_shutdown_rwlock;

static void *_handle_connection(void *arg);

void connection_initialize(void)
{
	if (_initialized) return;
	
	thread_mutex_create(&_connection_mutex);
	thread_mutex_create(&_queue_mutex);
	thread_rwlock_create(&_source_shutdown_rwlock);
	thread_cond_create(&_pool_cond);

	_initialized = 1;
}

void connection_shutdown(void)
{
	if (!_initialized) return;
	
	thread_cond_destroy(&_pool_cond);
	thread_rwlock_destroy(&_source_shutdown_rwlock);
	thread_mutex_destroy(&_queue_mutex);
	thread_mutex_destroy(&_connection_mutex);

	_initialized = 0;
}

static connection_t *_create_connection(void)
{
	connection_t *con;

	con = (connection_t *)malloc(sizeof(connection_t));
	memset(con, 0, sizeof(connection_t));

	return con;
}

static unsigned long _next_connection_id(void)
{
	unsigned long id;

	thread_mutex_lock(&_connection_mutex);
	id = _current_id++;
	thread_mutex_unlock(&_connection_mutex);

	return id;
}

static connection_t *_accept_connection(void)
{
	int sock;
	fd_set rfds;
	connection_t *con;
	struct timeval tv;
	char *ip;

	FD_ZERO(&rfds);
	FD_SET(global.serversock, &rfds);

	tv.tv_sec = 0;
	tv.tv_usec = 30000;

	if (select(global.serversock + 1, &rfds, NULL, NULL, &tv) <= 0) {
		return NULL;
	}

	/* malloc enough room for 123.123.123.123\0 */
	ip = (char *)malloc(16);

	sock = sock_accept(global.serversock, ip, 16);
	if (sock >= 0) {
		con = _create_connection();

		con->sock = sock;
		con->con_time = time(NULL);
		con->id = _next_connection_id();
		con->ip = ip;

		return con;
	}

	if (!sock_recoverable(sock_error()))
		WARN2("accept() failed with error %d: %s", sock_error(), strerror(sock_error()));
	
	free(ip);

	return NULL;
}

static void _add_connection(connection_t *con)
{
	con_queue_t *node;

	node = (con_queue_t *)malloc(sizeof(con_queue_t));
	
	thread_mutex_lock(&_queue_mutex);
	node->con = con;
	node->next = _queue;
	_queue = node;
	thread_mutex_unlock(&_queue_mutex);

	printf("connection added....\n");
}

static void _signal_pool(void)
{
	thread_cond_signal(&_pool_cond);
}

static void _push_thread(thread_queue_t **queue, long thread_id)
{
	/* create item */
	thread_queue_t *item = (thread_queue_t *)malloc(sizeof(thread_queue_t));
	item->thread_id = thread_id;
	item->next = NULL;


	thread_mutex_lock(&_queue_mutex);
	if (*queue == NULL) {
		*queue = item;
	} else {
		item->next = *queue;
		*queue = item;
	}
	thread_mutex_unlock(&_queue_mutex);
}

static long _pop_thread(thread_queue_t **queue)
{
	long id;
	thread_queue_t *item;

	thread_mutex_lock(&_queue_mutex);

	item = *queue;
	if (item == NULL) {
		thread_mutex_unlock(&_queue_mutex);
		return -1;
	}

	*queue = item->next;
	item->next = NULL;
	id = item->thread_id;
	free(item);

	thread_mutex_unlock(&_queue_mutex);

	return id;
}

static void _build_pool(void)
{
	ice_config_t *config;
	int i, tid;
	char buff[64];

	config = config_get_config();

	for (i = 0; i < config->threadpool_size; i++) {
		snprintf(buff, 64, "Connection Thread #%d", i);
		tid = thread_create(buff, _handle_connection, NULL, THREAD_ATTACHED);
		_push_thread(&_conhands, tid);
	}
}

static void _destroy_pool(void)
{
	long id;
	int i;

	i = 0;

	thread_cond_broadcast(&_pool_cond);
	id = _pop_thread(&_conhands);
	while (id != -1) {
		thread_join(id);
		_signal_pool();
		id = _pop_thread(&_conhands);
	}
}

void connection_accept_loop(void)
{
	connection_t *con;

	_build_pool();

	while (global.running == ICE_RUNNING) {
		con = _accept_connection();

		if (con) {
			_add_connection(con);
			_signal_pool();
		}
	}

	_destroy_pool();

	/* wait for all the sources to shutdown */
	thread_rwlock_wlock(&_source_shutdown_rwlock);
	thread_rwlock_unlock(&_source_shutdown_rwlock);
}

static connection_t *_get_connection(void)
{
	con_queue_t *node = NULL;
	con_queue_t *oldnode = NULL;
	connection_t *con = NULL;

	thread_mutex_lock(&_queue_mutex);
	if (_queue) {
		node = _queue;
		while (node->next) {
			oldnode = node;
			node = node->next;
		}
		
		/* node is now the last node
		** and oldnode is the previous one, or NULL
		*/
		if (oldnode) oldnode->next = NULL;
		else (_queue) = NULL;
	}
	thread_mutex_unlock(&_queue_mutex);

	if (node) {
		con = node->con;
		free(node);
	}

	return con;
}

static void *_handle_connection(void *arg)
{
	char header[4096];
	connection_t *con;
	http_parser_t *parser;
	source_t *source;
	stats_connection_t *stats;
	avl_node *node;
	http_var_t *var;
	client_t *client;
	int bytes;

	while (global.running == ICE_RUNNING) {
		memset(header, 0, 4096);

		thread_cond_wait(&_pool_cond);
		if (global.running != ICE_RUNNING) break;

		/* grab a connection and set the socket to blocking */
		con = _get_connection();

		stats_event_inc(NULL, "connections");

		sock_set_blocking(con->sock, SOCK_BLOCK);

		/* fill header with the http header */
		if (util_read_header(con->sock, header, 4096) == 0) {
			/* either we didn't get a complete header, or we timed out */
			connection_close(con);
			continue;
		}

		parser = httpp_create_parser();
		httpp_initialize(parser, NULL);
		if (httpp_parse(parser, header, strlen(header))) {
			/* handle the connection or something */

			if (strcmp("ice", httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) != 0 && strcmp("http", httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) != 0) {
				printf("DEBUG: bad protocol\n");
				connection_close(con);
				httpp_destroy(parser);
				continue;
			}

			if (parser->req_type == httpp_req_source) {
				printf("DEBUG: source logging in\n");
				stats_event_inc(NULL, "source_connections");
				
				if (strcmp((httpp_getvar(parser, "ice-password") != NULL) ? httpp_getvar(parser, "ice-password") : "", (config_get_config()->source_password != NULL) ? config_get_config()->source_password : "") != 0) {
					printf("DEBUG: bad password\n");
					INFO1("Source (%s) attempted to login with bad password", httpp_getvar(parser, HTTPP_VAR_URI));
					connection_close(con);
					httpp_destroy(parser);
					continue;
				}

				global_lock();
				if (global.sources >= config_get_config()->source_limit) {
					printf("TOO MANY SOURCE, KICKING THIS ONE\n");
					INFO1("Source (%d) logged in, but there are too many sources", httpp_getvar(parser, HTTPP_VAR_URI));
					connection_close(con);
					httpp_destroy(parser);
					global_unlock();
					continue;
				}
				global.sources++;
				global_unlock();

				stats_event_inc(NULL, "sources");

				source = source_create(con, parser, httpp_getvar(parser, HTTPP_VAR_URI), FORMAT_TYPE_VORBIS);
				source->shutdown_rwlock = &_source_shutdown_rwlock;

				sock_set_blocking(con->sock, SOCK_NONBLOCK);
				
				thread_create("Source Thread", source_main, (void *)source, THREAD_DETACHED);
				
				continue;
			} else if (parser->req_type == httpp_req_stats) {
				printf("DEBUG: stats connection...\n");
				stats_event_inc(NULL, "stats_connections");

				if (strcmp((httpp_getvar(parser, "ice-password") != NULL) ? httpp_getvar(parser, "ice-password") : "", (config_get_config()->source_password != NULL) ? config_get_config()->source_password : "") != 0) {
					printf("DEBUG: bad password\n");
					connection_close(con);
					httpp_destroy(parser);
					continue;
				}

				stats_event_inc(NULL, "stats");

				/* create stats connection and create stats handler thread */
				stats = (stats_connection_t *)malloc(sizeof(stats_connection_t));
				stats->parser = parser;
				stats->con = con;

				thread_create("Stats Connection", stats_connection, (void *)stats, THREAD_DETACHED);

				continue;
			} else if (parser->req_type == httpp_req_play || parser->req_type == httpp_req_get) {
				printf("DEBUG: client coming in...\n");

				/* make a client */
				client = client_create(con, parser);
				stats_event_inc(NULL, "client_connections");

				/* there are several types of HTTP GET clients
				** media clients, which are looking for a source (eg, URI = /stream.ogg)
				** stats clients, which are looking for /stats.xml
				** and director server authorizers, which are looking for /GUID-xxxxxxxx (where xxxxxx is the GUID in question
				** we need to handle the latter two before the former, as the latter two
				** aren't subject to the limits.
				*/
				// TODO: add GUID-xxxxxx
				if (strcmp(httpp_getvar(parser, HTTPP_VAR_URI), "/stats.xml") == 0) {
					printf("sending stats.xml\n");
					stats_sendxml(client);
					continue;
				}

				global_lock();
				if (global.clients >= config_get_config()->client_limit) {
					if (parser->req_type == httpp_req_get) {
						client->respcode = 504;
						bytes = sock_write(client->con->sock, "HTTP/1.0 504 Server Full\r\nContent-Type: text/html\r\n\r\n"\
								   "<b>The server is already full.  Try again later.</b>\r\n");
						if (bytes > 0) client->con->sent_bytes = bytes;
					}
					client_destroy(client);
					global_unlock();
					continue;
				}
				global_unlock();
				
				avl_tree_rlock(global.source_tree);
				source = source_find_mount(httpp_getvar(parser, HTTPP_VAR_URI));
				if (source) {
					printf("DEBUG: source found for client\n");

					global_lock();
					if (global.clients >= config_get_config()->client_limit) {
						if (parser->req_type == httpp_req_get) {
							client->respcode = 504;
							bytes = sock_write(client->con->sock, "HTTP/1.0 504 Server Full\r\nContent-Type: text/html\r\n\r\n"\
									   "<b>The server is already full.  Try again later.</b>\r\n");
							if (bytes > 0) client->con->sent_bytes = bytes;
						}
						client_destroy(client);
						global_unlock();
						continue;
					}
					global.clients++;
					global_unlock();

					if (parser->req_type == httpp_req_get) {
						client->respcode = 200;
						sock_write(client->con->sock, "HTTP/1.0 200 OK\r\nContent-Type: application/x-ogg\r\n");
						/* iterate through source http headers and send to client */
						avl_tree_rlock(source->parser->vars);
						node = avl_get_first(source->parser->vars);
						while (node) {
						        var = (http_var_t *)node->key;
							if (strcasecmp(var->name, "ice-password") && !strncasecmp("ice-", var->name, 4)) {
								printf("DEBUG: sending %s: %s\n", var->name, var->value);
								sock_write(client->con->sock, "%s: %s\r\n", var->name, var->value);
							}
							node = avl_get_next(node);
						}
						avl_tree_unlock(source->parser->vars);

						sock_write(client->con->sock, "\r\n");
						
						sock_set_blocking(client->con->sock, SOCK_NONBLOCK);
					}

					avl_tree_wlock(source->pending_tree);
					avl_insert(source->pending_tree, (void *)client);
					avl_tree_unlock(source->pending_tree);
				}
				
				avl_tree_unlock(global.source_tree);
				
				if (!source) {
					printf("DEBUG: source not found for client\n");
					if (parser->req_type == httpp_req_get) {
						client->respcode = 404;
						bytes = sock_write(client->con->sock, "HTTP/1.0 404 Source Not Found\r\nContent-Type: text/html\r\n\r\n"\
							"<b>The source you requested could not be found.</b>\r\n");
						if (bytes > 0) client->con->sent_bytes = bytes;
					}
					client_destroy(client);
				}
				
				continue;
			} else {
				printf("DEBUG: wrong request type\n");
				connection_close(con);
				httpp_destroy(parser);
				continue;
			}
		} else {
			printf("DEBUG: parsing failed\n");
			connection_close(con);
			httpp_destroy(parser);
			continue;
		}
	}

	thread_exit(0);

	return NULL;
}

void connection_close(connection_t *con)
{
	sock_close(con->sock);
	if (con->ip) free(con->ip);
	if (con->host) free(con->host);
	free(con);
}
