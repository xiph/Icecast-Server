#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
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
#include "xslt.h"
#include "fserve.h"

#include "source.h"

#define CATMODULE "connection"

typedef struct con_queue_tag {
	connection_t *con;
	struct con_queue_tag *next;
} con_queue_t;

typedef struct _thread_queue_tag {
	thread_type *thread_id;
	struct _thread_queue_tag *next;
} thread_queue_t;

static mutex_t _connection_mutex;
static unsigned long _current_id = 0;
static int _initialized = 0;
static cond_t _pool_cond;

static con_queue_t *_queue = NULL;
static mutex_t _queue_mutex;

static thread_queue_t *_conhands = NULL;

rwlock_t _source_shutdown_rwlock;

static void *_handle_connection(void *arg);

void connection_initialize(void)
{
	if (_initialized) return;
	
	thread_mutex_create(&_connection_mutex);
	thread_mutex_create(&_queue_mutex);
	thread_rwlock_create(&_source_shutdown_rwlock);
	thread_cond_create(&_pool_cond);
    thread_cond_create(&global.shutdown_cond);

	_initialized = 1;
}

void connection_shutdown(void)
{
	if (!_initialized) return;
	
    thread_cond_destroy(&global.shutdown_cond);
	thread_cond_destroy(&_pool_cond);
	thread_rwlock_destroy(&_source_shutdown_rwlock);
	thread_mutex_destroy(&_queue_mutex);
	thread_mutex_destroy(&_connection_mutex);

	_initialized = 0;
}

static unsigned long _next_connection_id(void)
{
	unsigned long id;

	thread_mutex_lock(&_connection_mutex);
	id = _current_id++;
	thread_mutex_unlock(&_connection_mutex);

	return id;
}

connection_t *create_connection(sock_t sock, char *ip) {
	connection_t *con;
	con = (connection_t *)malloc(sizeof(connection_t));
	memset(con, 0, sizeof(connection_t));
	con->sock = sock;
	con->con_time = time(NULL);
	con->id = _next_connection_id();
	con->ip = ip;
	return con;
}

static connection_t *_accept_connection(void)
{
	int sock;
	connection_t *con;
	char *ip;

    if (util_timed_wait_for_fd(global.serversock, 100) <= 0) {
		return NULL;
	}

	/* malloc enough room for 123.123.123.123\0 (TODO: ipv6?)*/
	ip = (char *)malloc(16);

	sock = sock_accept(global.serversock, ip, 16);
	if (sock >= 0) {
		con = create_connection(sock, ip);

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

}

static void _signal_pool(void)
{
	thread_cond_signal(&_pool_cond);
}

static void _push_thread(thread_queue_t **queue, thread_type *thread_id)
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

static thread_type *_pop_thread(thread_queue_t **queue)
{
	thread_type *id;
	thread_queue_t *item;

	thread_mutex_lock(&_queue_mutex);

	item = *queue;
	if (item == NULL) {
		thread_mutex_unlock(&_queue_mutex);
		return NULL;
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
	int i;
    thread_type *tid;
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
	thread_type *id;
	int i;

	i = 0;

	thread_cond_broadcast(&_pool_cond);
	id = _pop_thread(&_conhands);
	while (id != NULL) {
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

    /* Give all the other threads notification to shut down */
    thread_cond_broadcast(&global.shutdown_cond);

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

int connection_create_source(client_t *client, connection_t *con, http_parser_t *parser, char *mount) {
	source_t *source;
	char *contenttype;
    int bytes;

	/* check to make sure this source wouldn't
	** be over the limit
	*/
	global_lock();
	if (global.sources >= config_get_config()->source_limit) {
		INFO1("Source (%s) logged in, but there are too many sources", mount);
		global_unlock();
		return 0;
	}
	global.sources++;
	global_unlock();
	
	stats_event_inc(NULL, "sources");

	contenttype = httpp_getvar(parser, "content-type");

	if (contenttype != NULL) {
		format_type_t format = format_get_type(contenttype);
		if (format < 0) {
			WARN1("Content-type \"%s\" not supported, dropping source", contenttype);
            goto fail;
		} else {
			source = source_create(client, con, parser, mount, format);
		}
	} else {
		WARN0("No content-type header, cannot handle source");
        goto fail;
	}
    client->respcode = 200;
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\n\r\n");
    if(bytes > 0) client->con->sent_bytes = bytes;

	source->shutdown_rwlock = &_source_shutdown_rwlock;
	sock_set_blocking(con->sock, SOCK_NONBLOCK);
	thread_create("Source Thread", source_main, (void *)source, THREAD_DETACHED);
	return 1;

fail:
    global_lock();
    global.sources--;
    global_unlock();

    stats_event_dec(NULL, "sources");
    return 0;
}

static int _check_pass_http(http_parser_t *parser, 
        char *correctuser, char *correctpass)
{
    /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
    char *header = httpp_getvar(parser, "authorization");
    char *userpass, *tmp;
    char *username, *password;

    if(header == NULL)
        return 0;

    if(strncmp(header, "Basic ", 6))
        return 0;

    userpass = util_base64_decode(header+6);
    if(userpass == NULL) {
        WARN1("Base64 decode of Authorization header \"%s\" failed",
                header+6);
        return 0;
    }

    tmp = strchr(userpass, ':');
    if(!tmp) {
        free(userpass);
        return 0;
    }
    *tmp = 0;
    username = userpass;
    password = tmp+1;

    if(strcmp(username, correctuser) || strcmp(password, correctpass)) {
        free(userpass);
        return 0;
    }
    free(userpass);

    return 1;
}

static int _check_pass_ice(http_parser_t *parser, char *correctpass)
{
    char *password;

    password = httpp_getvar(parser, "ice-password");
    if(!password)
        password = "";

	if (strcmp(password, correctpass))
        return 0;
    else
        return 1;
}

static int _check_relay_pass(http_parser_t *parser)
{
    char *pass = config_get_config()->relay_password;
    if(!pass)
        pass = config_get_config()->source_password;

    return _check_pass_http(parser, "relay", pass);
}

static int _check_source_pass(http_parser_t *parser)
{
    char *pass = config_get_config()->source_password;
    int ret;
    if(!pass)
        pass = "";

    ret = _check_pass_http(parser, "source", pass);
    if(!ret && config_get_config()->ice_login)
    {
        ret = _check_pass_ice(parser, pass);
        if(ret)
            WARN0("Source is using deprecated icecast login");
    }

    return ret;
}

static void _handle_source_request(connection_t *con, 
        http_parser_t *parser, char *uri)
{
    client_t *client;

	client = client_create(con, parser);

    INFO1("Source logging in at mountpoint \"%s\"", uri);
    stats_event_inc(NULL, "source_connections");
				
	if (!_check_source_pass(parser)) {
		INFO1("Source (%s) attempted to login with invalid or missing password", uri);
        client_send_401(client);
        return;
	}

	/* check to make sure this source has
	** a unique mountpoint
	*/

	avl_tree_rlock(global.source_tree);
	if (source_find_mount(uri) != NULL) {
		INFO1("Source tried to log in as %s, but mountpoint is already used", uri);
        client_send_404(client, "Mountpoint in use");
		avl_tree_unlock(global.source_tree);
		return;
	}
	avl_tree_unlock(global.source_tree);

	if (!connection_create_source(client, con, parser, uri)) {
        client_destroy(client);
	}
}

static void _handle_stats_request(connection_t *con, 
        http_parser_t *parser, char *uri)
{
	stats_connection_t *stats;

	stats_event_inc(NULL, "stats_connections");
				
	if (!_check_source_pass(parser)) {
        ERROR0("Bad password for stats connection");
		connection_close(con);
		httpp_destroy(parser);
        return;
	}
					
	stats_event_inc(NULL, "stats");
					
	/* create stats connection and create stats handler thread */
	stats = (stats_connection_t *)malloc(sizeof(stats_connection_t));
	stats->parser = parser;
	stats->con = con;
					
	thread_create("Stats Connection", stats_connection, (void *)stats, THREAD_DETACHED);
}

static void _handle_get_request(connection_t *con,
        http_parser_t *parser, char *uri)
{
    char *fullpath;
	client_t *client;
	avl_node *node;
	http_var_t *var;
    int bytes;
	struct stat statbuf;
	source_t *source;

    DEBUG0("Client connected");

	/* make a client */
	client = client_create(con, parser);
	stats_event_inc(NULL, "client_connections");
					
	/* there are several types of HTTP GET clients
	** media clients, which are looking for a source (eg, URI = /stream.ogg)
	** stats clients, which are looking for /stats.xml
	** and director server authorizers, which are looking for /GUID-xxxxxxxx 
    ** (where xxxxxx is the GUID in question) - this isn't implemented yet.
	** we need to handle the latter two before the former, as the latter two
	** aren't subject to the limits.
	*/
	/* TODO: add GUID-xxxxxx */
	if (strcmp(uri, "/stats.xml") == 0) {
	    if (!_check_source_pass(parser)) {
		    INFO0("Request for stats.xml with incorrect or no password");
            client_send_401(client);
            return;
    	}
        DEBUG0("Stats request, sending xml stats");
		stats_sendxml(client);
        client_destroy(client);
        return;
    }

	/* Here we are parsing the URI request to see
	** if the extension is .xsl, if so, then process
	** this request as an XSLT request
	*/
    fullpath = util_get_path_from_normalised_uri(uri);
    if (util_check_valid_extension(fullpath) == XSLT_CONTENT) {
    	/* If the file exists, then transform it, otherwise, write a 404 */
    	if (stat(fullpath, &statbuf) == 0) {
            DEBUG0("Stats request, sending XSL transformed stats");
            client->respcode = 200;
		    bytes = sock_write(client->con->sock, 
                    "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
            if(bytes > 0) client->con->sent_bytes = bytes;
            stats_transform_xslt(client, fullpath);
		    client_destroy(client);
    	}
	    else {
            client_send_404(client, "The file you requested could not be found");
    	}
        free(fullpath);
        return;
	}
    else if(config_get_config()->fileserve && 
            stat(fullpath, &statbuf) == 0) {
        fserve_client_create(client, fullpath);
        free(fullpath);
        return;
    }
    free(fullpath);

    if(strcmp(util_get_extension(uri), "m3u") == 0) {
        char *sourceuri = strdup(uri);
        char *dot = strrchr(sourceuri, '.');
        *dot = 0;
	    avl_tree_rlock(global.source_tree);
    	source = source_find_mount(sourceuri);
	   	if (source) {
            client->respcode = 200;
            bytes = sock_write(client->con->sock,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "http://%s:%d%s", 
                    config_get_config()->hostname, 
                    config_get_config()->port,
                    sourceuri
                    );
            if(bytes > 0) client->con->sent_bytes = bytes;
    	    client_destroy(client);
        }
        else {
            client_send_404(client, "The file you requested could not be found");
        }
		avl_tree_unlock(global.source_tree);
        free(sourceuri);
        return;
    }

	if (strcmp(uri, "/allstreams.txt") == 0) {
		if (!_check_relay_pass(parser)) {
			INFO0("Client attempted to fetch allstreams.txt with bad password");
            client_send_401(client);
		} else {
            client->respcode = 200;
            bytes = sock_write(client->con->sock,
                    "HTTP/1.0 200 OK\r\n\r\n");
            if(bytes > 0) client->con->sent_bytes = bytes;

			avl_node *node;
			source_t *s;
			avl_tree_rlock(global.source_tree);
			node = avl_get_first(global.source_tree);
			while (node) {
				s = (source_t *)node->key;
				bytes = sock_write(client->con->sock, "%s\r\n", s->mount);
                if(bytes > 0) client->con->sent_bytes += bytes;
                else break;

				node = avl_get_next(node);
			}
			avl_tree_unlock(global.source_tree);
		    client_destroy(client);
		}
        return;
	}
				
	global_lock();
	if (global.clients >= config_get_config()->client_limit) {
		client->respcode = 504;
		bytes = sock_write(client->con->sock, 
                "HTTP/1.0 504 Server Full\r\n"
                "Content-Type: text/html\r\n\r\n"
    		    "<b>The server is already full.  Try again later.</b>\r\n");
    	if (bytes > 0) client->con->sent_bytes = bytes;
		client_destroy(client);
		global_unlock();
        return;
	}
	global_unlock();
					
	avl_tree_rlock(global.source_tree);
	source = source_find_mount(uri);
	if (source) {
        DEBUG0("Source found for client");
						
		global_lock();
		if (global.clients >= config_get_config()->client_limit) {
			client->respcode = 504;
			bytes = sock_write(client->con->sock, 
                    "HTTP/1.0 504 Server Full\r\n"
                    "Content-Type: text/html\r\n\r\n"
				    "<b>The server is already full.  Try again later.</b>\r\n");
			if (bytes > 0) client->con->sent_bytes = bytes;
			client_destroy(client);
			global_unlock();
            avl_tree_unlock(global.source_tree);
            return;
		}
		global.clients++;
		global_unlock();
						
		client->respcode = 200;
		bytes = sock_write(client->con->sock, 
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: %s\r\n", 
                format_get_mimetype(source->format->type));
        if(bytes > 0) client->con->sent_bytes += bytes;
		/* iterate through source http headers and send to client */
		avl_tree_rlock(source->parser->vars);
		node = avl_get_first(source->parser->vars);
		while (node) {
			var = (http_var_t *)node->key;
			if (strcasecmp(var->name, "ice-password") && 
                    !strncasecmp("ice-", var->name, 4)) {
				bytes = sock_write(client->con->sock, 
                        "%s: %s\r\n", var->name, var->value);
                if(bytes > 0) client->con->sent_bytes += bytes;
			}
			node = avl_get_next(node);
		}
		avl_tree_unlock(source->parser->vars);
						
		bytes = sock_write(client->con->sock, "\r\n");
        if(bytes > 0) client->con->sent_bytes += bytes;
							
    	sock_set_blocking(client->con->sock, SOCK_NONBLOCK);
						
		avl_tree_wlock(source->pending_tree);
		avl_insert(source->pending_tree, (void *)client);
		avl_tree_unlock(source->pending_tree);
	}
					
	avl_tree_unlock(global.source_tree);
					
	if (!source) {
        DEBUG0("Source not found for client");
        client_send_404(client, "The source you requested could not be found.");
	}
}

static void *_handle_connection(void *arg)
{
	char header[4096];
	connection_t *con;
	http_parser_t *parser;
    char *rawuri, *uri;
	client_t *client;

	while (global.running == ICE_RUNNING) {
		memset(header, 0, 4096);

		thread_cond_wait(&_pool_cond);
		if (global.running != ICE_RUNNING) break;

		/* grab a connection and set the socket to blocking */
		while ((con = _get_connection())) {
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
				
				if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
                    strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
                    ERROR0("Bad HTTP protocol detected");
					connection_close(con);
					httpp_destroy(parser);
					continue;
				}

                rawuri = httpp_getvar(parser, HTTPP_VAR_URI);
                uri = util_normalise_uri(rawuri);

                if(!uri) {
					client = client_create(con, parser);
                    client_send_404(client, "The path you requested was invalid");
                    continue;
                }

				if (parser->req_type == httpp_req_source) {
                    _handle_source_request(con, parser, uri);
                }
                else if (parser->req_type == httpp_req_stats) {
                    _handle_stats_request(con, parser, uri);
                }
                else if (parser->req_type == httpp_req_get) {
                    _handle_get_request(con, parser, uri);
                }
                else {
                    ERROR0("Wrong request type from client");
					connection_close(con);
					httpp_destroy(parser);
                }

                free(uri);
			} else {
                ERROR0("HTTP request parsing failed");
				connection_close(con);
				httpp_destroy(parser);
				continue;
			}
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
