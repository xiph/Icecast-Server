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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "net/sock.h"

#include "connection.h"
#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "format.h"
#include "logging.h"
#include "cfgfile.h"
#include "util.h"

#include "fserve.h"

#undef CATMODULE
#define CATMODULE "fserve"

#define BUFSIZE 4096

#ifdef _WIN32
#define MIMETYPESFILE ".\\mime.types"
#else
#define MIMETYPESFILE "/etc/mime.types"
#endif

static avl_tree *client_tree;
static avl_tree *pending_tree;
static avl_tree *mimetypes = NULL;

static cond_t fserv_cond;
static thread_type *fserv_thread;
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

typedef struct {
    char *ext;
    char *type;
} mime_type;

/* avl tree helper */
static int _compare_clients(void *compare_arg, void *a, void *b);
static int _remove_client(void *key);
static int _free_client(void *key);
static int _delete_mapping(void *mapping);
static void *fserv_thread_function(void *arg);
static void create_mime_mappings(char *fn);

void fserve_initialize(void)
{
    ice_config_t *config = config_get_config();
    int serve = config->fileserve;

    config_release_config();

    if(!serve)
        return;

    create_mime_mappings(MIMETYPESFILE);

    client_tree = avl_tree_new(_compare_clients, NULL);
    pending_tree = avl_tree_new(_compare_clients, NULL);
    thread_cond_create(&fserv_cond);

    run_fserv = 1;

    fserv_thread = thread_create("File Serving Thread", 
            fserv_thread_function, NULL, THREAD_ATTACHED);
}

void fserve_shutdown(void)
{
    ice_config_t *config = config_get_config();
    int serve = config->fileserve;

    config_release_config();

    if(!serve)
        return;

    if(!run_fserv)
        return;

    avl_tree_free(mimetypes, _delete_mapping);

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
        fd_set realfds;
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

        memcpy(&realfds, &fds, sizeof(fd_set));
        if(select(fd_max+1, NULL, &realfds, NULL, &tv) > 0)
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

static void *fserv_thread_function(void *arg)
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
            sbytes = client_send_bytes (client->client, 
                    &client->buf[client->offset], 
                    client->datasize - client->offset);

            /* TODO: remove clients if they take too long. */
            if(sbytes > 0) {
                client->offset += sbytes;
            }

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
    mime_type exttype = {ext, NULL};
    void *result;

    if (!avl_get_by_key (mimetypes, &exttype, &result))
    {
        mime_type *mime = result;
        return mime->type;
    }
    else {
        /* Fallbacks for a few basic ones */
        if(!strcmp(ext, "ogg"))
            return "application/ogg";
        else if(!strcmp(ext, "mp3"))
            return "audio/mpeg";
        else if(!strcmp(ext, "html"))
            return "text/html";
        else if(!strcmp(ext, "txt"))
            return "text/plain";
        else
            return "application/octet-stream";
    }
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
    int client_limit;
    ice_config_t *config = config_get_config();

    client_limit = config->client_limit;
    config_release_config();

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
    if(global.clients >= client_limit) {
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
    sock_set_nodelay(client->client->con->sock);

    avl_tree_wlock(pending_tree);
    avl_insert(pending_tree, client);
    avl_tree_unlock(pending_tree);

    thread_cond_signal(&fserv_cond);

    return 0;
}

static int _compare_clients(void *compare_arg, void *a, void *b)
{
    fserve_t *clienta = (fserve_t *)a;
    fserve_t *clientb = (fserve_t *)b;

    connection_t *cona = clienta->client->con;
    connection_t *conb = clientb->client->con;

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

static int _delete_mapping(void *mapping) {
    mime_type *map = mapping;
    free(map->ext);
    free(map->type);
    free(map);

    return 1;
}

static int _compare_mappings(void *arg, void *a, void *b)
{
    return strcmp(
            ((mime_type *)a)->ext,
            ((mime_type *)b)->ext);
}

static void create_mime_mappings(char *fn) {
    FILE *mimefile = fopen(fn, "r");
    char line[4096];
    char *type, *ext, *cur;
    mime_type *mapping;

    mimetypes = avl_tree_new(_compare_mappings, NULL);

    if(!mimefile)
        return;

    while(fgets(line, 4096, mimefile))
    {
        line[4095] = 0;

        if(*line == 0 || *line == '#')
            continue;

        type = line;

        cur = line;

        while(*cur != ' ' && *cur != '\t' && *cur)
            cur++;
        if(*cur == 0)
            continue;

        *cur++ = 0;

        while(1) {
            while(*cur == ' ' || *cur == '\t')
                cur++;
            if(*cur == 0)
                break;

            ext = cur;
            while(*cur != ' ' && *cur != '\t' && *cur != '\n' && *cur)
                cur++;
            *cur++ = 0;
            if(*ext)
            {
                void *tmp;
                /* Add a new extension->type mapping */
                mapping = malloc(sizeof(mime_type));
                mapping->ext = strdup(ext);
                mapping->type = strdup(type);
                if(!avl_get_by_key(mimetypes, mapping, &tmp))
                    avl_delete(mimetypes, mapping, _delete_mapping);
                avl_insert(mimetypes, mapping);
            }
        }
    }

    fclose(mimefile);
}

