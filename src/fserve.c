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
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <errno.h>

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
#include "compat.h"

#include "fserve.h"

#undef CATMODULE
#define CATMODULE "fserve"

#define BUFSIZE 4096

#ifdef _WIN32
#define MIMETYPESFILE ".\\mime.types"
#else
#define MIMETYPESFILE "/etc/mime.types"
#endif

static fserve_t *active_list = NULL;
volatile static fserve_t *pending_list = NULL;
static mutex_t pending_lock;
static avl_tree *mimetypes = NULL;

static thread_type *fserv_thread;
static int run_fserv = 0;
static unsigned int fserve_clients;
static int client_tree_changed=0;

#ifdef HAVE_POLL
static struct pollfd *ufds = NULL;
#else
static fd_set fds;
static int fd_max = -1;
#endif

typedef struct {
    char *ext;
    char *type;
} mime_type;

static int _free_client(void *key);
static int _delete_mapping(void *mapping);
static void *fserv_thread_function(void *arg);
static void create_mime_mappings(const char *fn);

void fserve_initialize(void)
{
    ice_config_t *config = config_get_config();
    int serve = config->fileserve;

    config_release_config();

    if(!serve)
        return;

    create_mime_mappings(MIMETYPESFILE);

    thread_mutex_create ("fserve pending", &pending_lock);

    run_fserv = 1;

    fserv_thread = thread_create("File Serving Thread", 
            fserv_thread_function, NULL, THREAD_ATTACHED);
}

void fserve_shutdown(void)
{
    if(!run_fserv)
        return;

    run_fserv = 0;
    thread_join(fserv_thread);
    INFO0("file serving thread stopped");
    avl_tree_free(mimetypes, _delete_mapping);
}

#ifdef HAVE_POLL
int fserve_client_waiting (void)
{
    fserve_t *fclient;
    unsigned int i = 0;

    /* only rebuild ufds if there are clients added/removed */
    if(client_tree_changed) {
        client_tree_changed = 0;
        ufds = realloc(ufds, fserve_clients * sizeof(struct pollfd));
        fclient = active_list;
        while (fclient) {
            ufds[i].fd = fclient->client->con->sock;
            ufds[i].events = POLLOUT;
            ufds[i].revents = 0;
            fclient = fclient->next;
            i++;
        }
    }

    if (!ufds)
        thread_sleep(200000);
    else if (poll(ufds, fserve_clients, 200) > 0)
    {
        /* mark any clients that are ready */
        fclient = active_list;
        for (i=0; i<fserve_clients; i++)
        {
            if (ufds[i].revents & (POLLOUT|POLLHUP|POLLERR))
                fclient->ready = 1;
            fclient = fclient->next;
        }
        return 1;
    }
    return 0;
}
#else
int fserve_client_waiting (void)
{
    fserve_t *fclient;
    fd_set realfds;

    /* only rebuild fds if there are clients added/removed */
    if(client_tree_changed) {
        client_tree_changed = 0;
        FD_ZERO(&fds);
        fd_max = -1;
        fclient = active_list;
        while (fclient) {
            FD_SET (fclient->client->con->sock, &fds);
            if (fclient->client->con->sock > fd_max)
                fd_max = fclient->client->con->sock;
            fclient = fclient->next;
        }
    }
    /* hack for windows, select needs at least 1 descriptor */
    if (fd_max == -1)
        thread_sleep (200000);
    else
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        /* make a duplicate of the set so we do not have to rebuild it
         * each time around */
        memcpy(&realfds, &fds, sizeof(fd_set));
        if(select(fd_max+1, NULL, &realfds, NULL, &tv) > 0)
        {
            /* mark any clients that are ready */
            fclient = active_list;
            while (fclient)
            {
                if (FD_ISSET (fclient->client->con->sock, &realfds))
                    fclient->ready = 1;
                fclient = fclient->next;
            }
            return 1;
        }
    }
    return 0;
}
#endif

static void wait_for_fds() {
    fserve_t *fclient;

    while (run_fserv)
    {
        /* add any new clients here */
        if (pending_list)
        {
            thread_mutex_lock (&pending_lock);

            fclient = (fserve_t*)pending_list;
            while (fclient)
            {
                fserve_t *to_move = fclient;
                fclient = fclient->next;
                to_move->next = active_list;
                active_list = to_move;
                client_tree_changed = 1;
                fserve_clients++;
                stats_event_inc(NULL, "clients");
            }
            pending_list = NULL;
            thread_mutex_unlock (&pending_lock);
        }
        /* drop out of here is someone is ready */
        if (fserve_client_waiting())
           break;
    }
}

static void *fserv_thread_function(void *arg)
{
    fserve_t *fclient, **trail;
    int sbytes, bytes;

    INFO0("file serving thread started");
    while (run_fserv)
    {
        wait_for_fds();

        fclient = active_list;
        trail = &active_list;

        while (fclient)
        {
            /* can we process this client */
            if (fclient->ready)
            {
                client_t *client = fclient->client;
                refbuf_t *refbuf = client->refbuf;
                fclient->ready = 0;
                if (client->pos == refbuf->len)
                {
                    /* Grab a new chunk */
                    bytes = fread (refbuf->data, 1, BUFSIZE, fclient->file);
                    if (bytes == 0)
                    {
                        fserve_t *to_go = fclient;
                        fclient = fclient->next;
                        *trail = fclient;
                        _free_client (to_go);
                        fserve_clients--;
                        client_tree_changed = 1;
                        continue;
                    }
                    refbuf->len = bytes;
                    client->pos = 0;
                }

                /* Now try and send current chunk. */
                sbytes = client->write_to_client (NULL, client);

                if (client->con->error)
                {
                    fserve_t *to_go = fclient;
                    fclient = fclient->next;
                    *trail = fclient;
                    fserve_clients--;
                    _free_client (to_go);
                    client_tree_changed = 1;
                    continue;
                }
            }
            trail = &fclient->next;
            fclient = fclient->next;
        }
    }

    /* Shutdown path */
    thread_mutex_lock (&pending_lock);
    while (pending_list)
    {
        fserve_t *to_go = (fserve_t *)pending_list;
        pending_list = to_go->next;

        /* Argh! _free_client decrements "clients" in stats, but it hasn't been
           incremented if the client is still on the pending list. So, fix that
           up first. Messy. */
        stats_event_inc(NULL, "clients");
        _free_client (to_go);
    }
    thread_mutex_unlock (&pending_lock);

    while (active_list)
    {
        fserve_t *to_go = active_list;
        active_list = to_go->next;
        _free_client (to_go);
    }

    return NULL;
}

const char *fserve_content_type (const char *path)
{
    const char *ext = util_get_extension(path);
    mime_type exttype = {strdup(ext), NULL};
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
        else if(!strcmp(ext, "css"))
            return "text/css";
        else if(!strcmp(ext, "txt"))
            return "text/plain";
        else
            return "application/octet-stream";
    }
}

static void fserve_client_destroy(fserve_t *fclient)
{
    if (fclient)
    {
        if (fclient->file)
            fclose (fclient->file);

        if (fclient->client)
            client_destroy (fclient->client);
        free (fclient);
    }
}


int fserve_client_create(client_t *httpclient, const char *path)
{
    fserve_t *client = calloc(1, sizeof(fserve_t));
    int bytes;
    int client_limit;
    ice_config_t *config = config_get_config();
    struct stat file_buf;
    char *range = NULL;
    int64_t new_content_len = 0;
    int64_t rangenumber = 0;
    int ret = 0;
    char *fullpath;
    int m3u_requested = 0, m3u_file_available = 1;

    // server limit check should be done earlier
    client_limit = config->client_limit;
    config_release_config();

    fullpath = util_get_path_from_normalised_uri (path);
    INFO2 ("handle file %s (%s)", path, fullpath);

    if (strcmp (util_get_extension (fullpath), "m3u") == 0)
        m3u_requested = 1;

    /* check for the actual file */
    if (stat (fullpath, &file_buf) != 0)
    {
        /* the m3u can be generated, but send an m3u file if available */
        if (m3u_requested == 0)
        {
            WARN2 ("req for file \"%s\" %s", fullpath, strerror (errno));
            client_send_404 (httpclient, "The file you requested could not be found");
            free (fullpath);
            return 0;
        }
        m3u_file_available = 0;
    }

    if (m3u_requested && m3u_file_available == 0)
    {
        char *host = httpp_getvar (httpclient->parser, "host");
        char *sourceuri = strdup (path);
        char *dot = strrchr(sourceuri, '.');
        int port = 8002;
        *dot = 0;
        DEBUG1 ("host passed is %s", host);
        httpclient->respcode = 200;
        if (host == NULL)
        {
            config = config_get_config();
            host = strdup (config->hostname);
            port = config->port;
            config_release_config();
            bytes = sock_write (httpclient->con->sock,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "http://%s:%d%s\r\n", 
                    host, port,
                    sourceuri
                    );
            free (host);
        }
        else
        {
            bytes = sock_write (httpclient->con->sock,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "http://%s%s\r\n", 
                    host, 
                    sourceuri
                    );
        }
        if (bytes > 0) httpclient->con->sent_bytes = bytes;
        client_destroy (httpclient);
        free (sourceuri);
        free (fullpath);
        return 0;
    }

    if (util_check_valid_extension (fullpath) == XSLT_CONTENT)
    {
        /* If the file exists, then transform it, otherwise, write a 404 */
        DEBUG0("Stats request, sending XSL transformed stats");
        httpclient->respcode = 200;
        bytes = sock_write(httpclient->con->sock, 
                "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
        if(bytes > 0) httpclient->con->sent_bytes = bytes;
        stats_transform_xslt (httpclient, fullpath);
        client_destroy (httpclient);
        free (fullpath);
        return 0;
    }

    client->file = fopen (fullpath, "rb");
    if (client->file == NULL)
    {
        client_send_404 (httpclient, "File not readable");
        return 0;
    }

    client->client = httpclient;
    client->ready = 0;
    client_set_queue (httpclient, NULL);
    httpclient->refbuf = refbuf_new (BUFSIZE);
    httpclient->pos = BUFSIZE;
    client->content_length = (int64_t)file_buf.st_size;

    global_lock();
    if(global.clients >= client_limit) {
        global_unlock();
        httpclient->respcode = 503;
        bytes = sock_write(httpclient->con->sock,
                "HTTP/1.0 503 Service Unavailable\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<b>Server is full, try again later.</b>\r\n");
        if(bytes > 0) httpclient->con->sent_bytes = bytes;
        fserve_client_destroy(client);
        return 0;
    }
    global.clients++;
    global_unlock();

    range = httpp_getvar (client->client->parser, "range");

    do
    {
        if (range)
        {
            ret = sscanf(range, "bytes=" FORMAT_INT64 "-", &rangenumber);
            if (ret == 1 && rangenumber>=0 && rangenumber < client->content_length)
            {
                /* Date: is required on all HTTP1.1 responses */
                char currenttime[50];
                time_t now;
                int strflen;
                struct tm result;
                int64_t endpos;

                ret = fseek(client->file, rangenumber, SEEK_SET);
                if (ret == -1)
                    break;

                new_content_len = client->content_length - rangenumber;
                endpos = rangenumber + new_content_len - 1;
                if (endpos < 0)
                    endpos = 0;
                
                time(&now);
                strflen = strftime(currenttime, 50, "%a, %d-%b-%Y %X GMT",
                                   gmtime_r (&now, &result));
                httpclient->respcode = 206;
                bytes = sock_write(httpclient->con->sock,
                    "HTTP/1.1 206 Partial Content\r\n"
                    "Date: %s\r\n"
                    "Content-Length: " FORMAT_INT64 "\r\n"
                    "Content-Range: bytes " FORMAT_INT64 \
                    "-" FORMAT_INT64 "/" FORMAT_INT64 "\r\n"
                    "Content-Type: %s\r\n\r\n",
                    currenttime,
                    new_content_len,
                    rangenumber,
                    endpos,
                    client->content_length,
                    fserve_content_type(path));
            }
            else
                break;
        }
        else {
            httpclient->respcode = 200;
            bytes = sock_write (httpclient->con->sock,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Length: " FORMAT_INT64 "\r\n"
                    "Content-Type: %s\r\n\r\n",
                    client->content_length,
                    fserve_content_type(path));
        }
        if(bytes > 0) httpclient->con->sent_bytes = bytes;

        sock_set_blocking(client->client->con->sock, SOCK_NONBLOCK);
        sock_set_nodelay(client->client->con->sock);

        thread_mutex_lock (&pending_lock);
        client->next = (fserve_t *)pending_list;
        pending_list = client;
        thread_mutex_unlock (&pending_lock);

        return 0;
    } while (0);
    /* If we run into any issues with the ranges
       we fallback to a normal/non-range request */
    httpclient->respcode = 416;
    bytes = sock_write(httpclient->con->sock,
            "HTTP/1.0 416 Request Range Not Satisfiable\r\n\r\n");
    if(bytes > 0) httpclient->con->sent_bytes = bytes;
    fserve_client_destroy (client);
    return 0;
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

static void create_mime_mappings(const char *fn) {
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

