#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

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
static int fserve_clients;
static int client_tree_changed=0;

#ifdef HAVE_POLL
static struct pollfd *ufds = NULL;
static int ufdssize = 0;
#else
static fd_set fds;
static int fd_max = 0;
#endif

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

static void wait_for_fds() {
    avl_node *client_node;
    fserve_t *client;
    int i;

    while(run_fserv) {
#ifdef HAVE_POLL
        if(client_tree_changed) {
            client_tree_changed = 0;
            i = 0;
            ufdssize = fserve_clients;
            ufds = realloc(ufds, ufdssize * sizeof(struct pollfd));
            avl_tree_rlock(client_tree);
            client_node = avl_get_first(client_tree);
            while(client_node) {
                client = client_node->key;
                ufds[i].fd = client->client->con->sock;
                ufds[i].events = POLLOUT;
                client_node = avl_get_next(client_node);
            }
            avl_tree_unlock(client_tree);
        }

        if(poll(ufds, ufdssize, 200) > 0)
            return;
#else
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if(client_tree_changed) {
            client_tree_changed = 0;
            i=0;
            FD_ZERO(&fds);
            fd_max = 0;
            avl_tree_rlock(client_tree);
            client_node = avl_get_first(client_tree);
            while(client_node) {
                client = client_node->key;
                FD_SET(client->client->con->sock, &fds);
                if(client->client->con->sock > fd_max)
                    fd_max = client->client->con->sock;
                client_node = avl_get_next(client_node);
            }
            avl_tree_unlock(client_tree);
        }

        if(select(fd_max+1, NULL, &fds, NULL, &tv) > 0)
            return;
#endif
        else {
            avl_tree_rlock(pending_tree);
            client_node = avl_get_first(pending_tree);
            avl_tree_unlock(pending_tree);
            if(client_node)
                return;
        }
    }
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
            avl_tree_rlock(pending_tree);
            pending_node = avl_get_first(pending_tree);
            if(!pending_node) {
                /* There are no current clients. Wait until there are... */
                avl_tree_unlock(pending_tree);
                avl_tree_unlock(client_tree);
                thread_cond_wait(&fserv_cond);
                continue;
            }
            avl_tree_unlock(pending_tree);
        }

        /* This isn't hugely efficient, but it'll do for now */
        avl_tree_unlock(client_tree);
        wait_for_fds();

        avl_tree_rlock(client_tree);
        client_node = avl_get_first(client_tree);

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
                fserve_clients--;
                client_node = avl_get_next(client_node);
                avl_delete(client_tree, (void *)client, _free_client);
                client_tree_changed = 1;
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
            client_tree_changed = 1;
            fserve_clients++;
            stats_event_inc(NULL, "clients");
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

static char *fserve_content_type(char *path)
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
    int bytes;

    client->file = fopen(path, "rb");
    if(!client->file) {
        client_send_404(httpclient, "File not readable");
        return -1;
    }

    client->client = httpclient;
    client->offset = 0;
    client->datasize = 0;
    client->buf = malloc(BUFSIZE);

    global_lock();
    if(global.clients >= config_get_config()->client_limit) {
        httpclient->respcode = 504;
        bytes = sock_write(httpclient->con->sock,
                "HTTP/1.0 504 Server Full\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<b>Server is full, try again later.</b>\r\n");
        if(bytes > 0) httpclient->con->sent_bytes = bytes;
        fserve_client_destroy(client);
        global_unlock();
        return -1;
    }
    global.clients++;
    global_unlock();

    httpclient->respcode = 200;
    bytes = sock_write(httpclient->con->sock,
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n\r\n",
            fserve_content_type(path));
    if(bytes > 0) httpclient->con->sent_bytes = bytes;

    sock_set_blocking(client->client->con->sock, SOCK_NONBLOCK);

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
    global_lock();
    global.clients--;
    global_unlock();
    stats_event_dec(NULL, "clients");

	
	return 1;
}

