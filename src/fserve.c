#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

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

#include "fserve.h"

#undef CATMODULE
#define CATMODULE "fserve"

#define BUFSIZE 4096

static avl_tree *client_tree;
static avl_tree *pending_tree;

static cond_t fserv_cond;
static thread_t *fserv_thread;
static int run_fserv;

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _remove_client(void *key);
static int _free_client(void *key);
void *fserv_thread_function(void *arg);

void fserve_initialize(void)
{
    if(!config_get_config()->fileserve)
        return;

	client_tree = avl_tree_new(_compare_clients, NULL);
	pending_tree = avl_tree_new(_compare_clients, NULL);
    thread_cond_create(&fserv_cond);

    run_fserv = 1;

    fserv_thread = thread_create("File Serving Thread", 
            fserv_thread_function, NULL, THREAD_ATTACHED);
}

void fserve_shutdown(void)
{
    if(!config_get_config()->fileserve)
        return;

    run_fserv = 0;
    thread_cond_signal(&fserv_cond);
    thread_join(fserv_thread);

    thread_cond_destroy(&fserv_cond);
    avl_tree_free(client_tree, _free_client);
    avl_tree_free(pending_tree, _free_client);
}

void *fserv_thread_function(void *arg)
{
    avl_node *client_node, *pending_node;
    fserve_t *client;
    int sbytes, bytes;

    while (run_fserv) {
        avl_tree_rlock(client_tree);

        client_node = avl_get_first(client_tree);
        if(!client_node) {
            pending_node = avl_get_first(pending_tree);
            if(!pending_node) {
                /* There are no current clients. Wait until there are... */
                avl_tree_unlock(client_tree);
                thread_cond_wait(&fserv_cond);
                continue;
            }
        }

        while(client_node) {
            avl_node_wlock(client_node);

            client = (fserve_t *)client_node->key;

            if(client->offset >= client->datasize) {
                /* Grab a new chunk */
                bytes = fread(client->buf, 1, BUFSIZE, client->file);
                if(bytes <= 0) {
                    client->client->con->error = 1;
                    avl_node_unlock(client_node);
                    client_node = avl_get_next(client_node);
                    continue;
                }
                client->offset = 0;
                client->datasize = bytes;
            }

            /* Now try and send current chunk. */
            sbytes = sock_write_bytes(client->client->con->sock, 
                    &client->buf[client->offset], 
                    client->datasize - client->offset);

            // TODO: remove clients if they take too long.
            if(sbytes >= 0) {
                client->offset += sbytes;
                client->client->con->sent_bytes += sbytes;
            }
            else if(!sock_recoverable(sock_error())) {
                DEBUG0("Fileserving client had fatal error, disconnecting");
                client->client->con->error = 1;
            }
            else
                DEBUG0("Fileserving client had recoverable error");

            avl_node_unlock(client_node);
            client_node = avl_get_next(client_node);
        }

        avl_tree_unlock(client_tree);

        /* Now we need a write lock instead, to delete done clients. */
        avl_tree_wlock(client_tree);

        client_node = avl_get_first(client_tree);
        while(client_node) {
            client = (fserve_t *)client_node->key;
            if(client->client->con->error) {
                client_node = avl_get_next(client_node);
                avl_delete(client_tree, (void *)client, _free_client);
                continue;
            }
            client_node = avl_get_next(client_node);
        }

        avl_tree_wlock(pending_tree);

        /* And now insert new clients. */
        client_node = avl_get_first(pending_tree);
        while(client_node) {
            client = (fserve_t *)client_node->key;
            avl_insert(client_tree, client);
            client_node = avl_get_next(client_node);

        }

        /* clear pending */
        while(avl_get_first(pending_tree)) {
            avl_delete(pending_tree, avl_get_first(pending_tree)->key, 
                    _remove_client);
        }

        avl_tree_unlock(pending_tree);
        avl_tree_unlock(client_tree);
    }

    /* Shutdown path */

    avl_tree_wlock(pending_tree);
    while(avl_get_first(pending_tree))
        avl_delete(pending_tree, avl_get_first(pending_tree)->key, 
                _free_client);
    avl_tree_unlock(pending_tree);

    avl_tree_wlock(client_tree);
    while(avl_get_first(client_tree))
        avl_delete(client_tree, avl_get_first(client_tree)->key, 
                _free_client);
    avl_tree_unlock(client_tree);

    thread_exit(0);
    return NULL;
}

char *fserve_content_type(char *path)
{
    char *ext = util_get_extension(path);

    if(!strcmp(ext, "ogg"))
        return "application/x-ogg";
    else if(!strcmp(ext, "mp3"))
        return "audio/mpeg";
    else if(!strcmp(ext, "html"))
        return "text/html";
    else if(!strcmp(ext, "txt"))
        return "text/plain";
    else
        return "application/octet-stream";
    /* TODO Add more types */
}

static void fserve_client_destroy(fserve_t *client)
{
    if(client) {
        if(client->buf)
            free(client->buf);
        if(client->file)
            fclose(client->file);

        if(client->client)
            client_destroy(client->client);
        free(client);
    }
}

int fserve_client_create(client_t *httpclient, char *path)
{
    fserve_t *client = calloc(1, sizeof(fserve_t));

    client->client = httpclient;
    client->file = fopen(path, "rb");
    if(!client->file) {
        fserve_client_destroy(client);
        return -1;
    }
    client->offset = 0;
    client->datasize = 0;
    client->buf = malloc(BUFSIZE);

    avl_tree_wlock(pending_tree);
    avl_insert(pending_tree, client);
    avl_tree_unlock(pending_tree);

    thread_cond_signal(&fserv_cond);

    return 0;
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
	fserve_t *client = (fserve_t *)key;

	fserve_client_destroy(client);
	
	return 1;
}

