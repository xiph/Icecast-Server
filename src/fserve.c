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
#define SCN_OFF_T SCNdMAX
#define PRI_OFF_T PRIdMAX
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
#include "admin.h"
#include "compat.h"

#include "fserve.h"

#undef CATMODULE
#define CATMODULE "fserve"

#define BUFSIZE 4096

static spin_t pending_lock;
static avl_tree *mimetypes = NULL;
static avl_tree *fh_cache = NULL;

typedef struct {
    char *ext;
    char *type;
} mime_type;

typedef struct {
    fbinfo finfo;
    mutex_t lock;
    int refcount;
    FILE *fp;
} fh_node;

int fserve_running;

static int _delete_mapping(void *mapping);
static int prefile_send (client_t *client);
static int file_send (client_t *client);
static int _compare_fh(void *arg, void *a, void *b);
static int _delete_fh (void *mapping);

void fserve_initialize(void)
{
    ice_config_t *config = config_get_config();

    mimetypes = NULL;
    thread_spin_create (&pending_lock);
    fh_cache = avl_tree_new (_compare_fh, NULL);

    fserve_recheck_mime_types (config);
    config_release_config();

    stats_event (NULL, "file_connections", "0");
    fserve_running = 1;
    INFO0("file serving started");
}

void fserve_shutdown(void)
{
    fserve_running = 0;
    if (mimetypes)
        avl_tree_free (mimetypes, _delete_mapping);
    if (fh_cache)
    {
        int count = 100;
        while (fh_cache->length && count)
        {
            DEBUG1 ("waiting for %u entries to clear", fh_cache->length);
            thread_sleep (20000);
            count--;
        }
        avl_tree_free (fh_cache, _delete_fh);
    }

    thread_spin_destroy (&pending_lock);
    INFO0("file serving stopped");
}


/* string returned needs to be free'd */
char *fserve_content_type (const char *path)
{
    char *ext = util_get_extension(path);
    mime_type exttype = { NULL, NULL };
    void *result;
    char *type;

    if (ext == NULL)
        return strdup ("text/html");
    exttype.ext = strdup (ext);

    thread_spin_lock (&pending_lock);
    if (mimetypes && !avl_get_by_key (mimetypes, &exttype, &result))
    {
        mime_type *mime = result;
        free (exttype.ext);
        type = strdup (mime->type);
    }
    else {
        free (exttype.ext);
        /* Fallbacks for a few basic ones */
        if(!strcmp(ext, "ogg"))
            type = strdup ("application/ogg");
        else if(!strcmp(ext, "mp3"))
            type = strdup ("audio/mpeg");
        else if(!strcmp(ext, "html"))
            type = strdup ("text/html");
        else if(!strcmp(ext, "css"))
            type = strdup ("text/css");
        else if(!strcmp(ext, "txt"))
            type = strdup ("text/plain");
        else if(!strcmp(ext, "jpg"))
            type = strdup ("image/jpeg");
        else if(!strcmp(ext, "png"))
            type = strdup ("image/png");
        else if(!strcmp(ext, "m3u"))
            type = strdup ("audio/x-mpegurl");
        else if(!strcmp(ext, "aac"))
            type = strdup ("audio/aac");
        else
            type = strdup ("application/octet-stream");
    }
    thread_spin_unlock (&pending_lock);
    return type;
}

static int _compare_fh(void *arg, void *a, void *b)
{
    fh_node *x = a, *y = b;
    int r = strcmp (x->finfo.mount, y->finfo.mount);

    if (r) return r;
    r = (int)x->finfo.flags - y->finfo.flags;
    if (r) return r;
    if (x->finfo.flags & FS_JINGLE)
        return strcmp (x->finfo.fallback, y->finfo.fallback);
    return 0;
}

static int _delete_fh (void *mapping)
{
    fh_node *fh = mapping;
    if (fh->refcount)
        WARN2 ("handle for %s has refcount %d", fh->finfo.mount, fh->refcount);
    thread_mutex_destroy (&fh->lock);
    if (fh->fp)
        fclose (fh->fp);
    free (fh->finfo.mount);
    free (fh->finfo.fallback);
    free (fh);

    return 1;
}

/* find/create handle and return it with the structure in a locked state */
static fh_node *open_fh (fbinfo *finfo)
{
    fh_node *fh, *result;

    if (finfo->mount == NULL)
        finfo->mount = "";
    fh = calloc (1, sizeof (fh_node));
    memcpy (&fh->finfo, finfo, sizeof (fbinfo));
    avl_tree_wlock (fh_cache);
    if (avl_get_by_key (fh_cache, fh, (void**)&result) == 0)
    {
        free (fh);
        thread_mutex_lock (&result->lock);
        result->refcount++;
        DEBUG2 ("refcount now %d for %s", result->refcount, result->finfo.mount);
        avl_tree_unlock (fh_cache);
        return result;
    }

    // insert new one
    if (fh->finfo.mount[0])
    {
        char *fullpath= util_get_path_from_normalised_uri (fh->finfo.mount, fh->finfo.flags&FS_USE_ADMIN);
        DEBUG1 ("lookup of \"%s\"", finfo->mount);
        fh->fp = fopen (fullpath, "rb");
        if (fh->fp == NULL)
            WARN1 ("Failed to open \"%s\"", fullpath);
        free (fullpath);
    }
    thread_mutex_create (&fh->lock);
    thread_mutex_lock (&fh->lock);
    fh->refcount = 1;
    fh->finfo.mount = strdup (finfo->mount);
    if (finfo->fallback)
        fh->finfo.fallback = strdup (finfo->fallback);
    avl_insert (fh_cache, fh);
    avl_tree_unlock (fh_cache);
    return fh;
}


/* client has requested a file, so check for it and send the file.  Do not
 * refer to the client_t afterwards.  return 0 for success, -1 on error.
 */
int fserve_client_create (client_t *httpclient, const char *path)
{
    struct stat file_buf;
    const char *range = NULL;
    off_t new_content_len = 0;
    off_t rangenumber = 0, content_length;
    int ret = 0;
    char *fullpath;
    int m3u_requested = 0, m3u_file_available = 1;
    int xspf_requested = 0, xspf_file_available = 1;
    ice_config_t *config;
    fh_node *fh;
    fbinfo finfo;

    fullpath = util_get_path_from_normalised_uri (path, 0);
    INFO2 ("checking for file %s (%s)", path, fullpath);

    if (strcmp (util_get_extension (fullpath), "m3u") == 0)
        m3u_requested = 1;

    if (strcmp (util_get_extension (fullpath), "xspf") == 0)
        xspf_requested = 1;

    /* check for the actual file */
    if (stat (fullpath, &file_buf) != 0)
    {
        /* the m3u can be generated, but send an m3u file if available */
        if (m3u_requested == 0 && xspf_requested == 0)
        {
            WARN2 ("req for file \"%s\" %s", fullpath, strerror (errno));
            client_send_404 (httpclient, "The file you requested could not be found");
            free (fullpath);
            return -1;
        }
        m3u_file_available = 0;
        xspf_file_available = 0;
    }

    httpclient->refbuf->len = PER_CLIENT_REFBUF_SIZE;

    if (m3u_requested && m3u_file_available == 0)
    {
        const char *host = httpp_getvar (httpclient->parser, "host");
        char *sourceuri = strdup (path);
        char *dot = strrchr (sourceuri, '.');
        char *protocol = "http";
        const char *agent = httpp_getvar (httpclient->parser, "user-agent");

        if (agent)
        {
            if (strstr (agent, "QTS") || strstr (agent, "QuickTime"))
                protocol = "icy";
        }
        /* at least a couple of players (fb2k/winamp) are reported to send a 
         * host header but without the port number. So if we are missing the
         * port then lets treat it as if no host line was sent */
        if (host && strchr (host, ':') == NULL)
            host = NULL;

        *dot = 0;
        httpclient->respcode = 200;
        if (host == NULL)
        {
            config = config_get_config();
            snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "%s://%s:%d%s\r\n", 
                    protocol,
                    config->hostname, config->port,
                    sourceuri
                    );
            config_release_config();
        }
        else
        {
            snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "%s://%s%s\r\n", 
                    protocol,
                    host, 
                    sourceuri
                    );
        }
        httpclient->refbuf->len = strlen (httpclient->refbuf->data);
        fserve_setup_client_fb (httpclient, NULL);
        free (sourceuri);
        free (fullpath);
        return 0;
    }
    if (xspf_requested && xspf_file_available == 0)
    {
        xmlDocPtr doc;
        char *reference = strdup (path);
        char *eol = strrchr (reference, '.');
        if (eol)
            *eol = '\0';
        doc = stats_get_xml (0, reference);
        free (reference);
        admin_send_response (doc, httpclient, XSLT, "xspf.xsl");
        xmlFreeDoc(doc);
        return 0;
    }

    /* on demand file serving check */
    config = config_get_config();
    if (config->fileserve == 0)
    {
        DEBUG1 ("on demand file \"%s\" refused", fullpath);
        client_send_404 (httpclient, "The file you requested could not be found");
        config_release_config();
        free (fullpath);
        return -1;
    }
    config_release_config();

    if (S_ISREG (file_buf.st_mode) == 0)
    {
        client_send_404 (httpclient, "The file you requested could not be found");
        WARN1 ("found requested file but there is no handler for it: %s", fullpath);
        free (fullpath);
        return -1;
    }

    finfo.flags = FS_NORMAL;
    finfo.mount = (char *)path;
    finfo.fallback = NULL;
    finfo.limit = 0;

    fh = open_fh (&finfo);
    if (fh == NULL)
    {
        WARN1 ("Problem accessing file \"%s\"", fullpath);
        client_send_404 (httpclient, "File not readable");
        free (fullpath);
        return -1;
    }
    free (fullpath);

    content_length = file_buf.st_size;
    range = httpp_getvar (httpclient->parser, "range");

    do
    {
        int bytes;

        httpclient->intro_offset = 0;
        /* full http range handling is currently not done but we deal with the common case */
        if (range)
        {
            ret = 0;
            if (strncasecmp (range, "bytes=", 6) == 0)
                ret = sscanf (range+6, "%" SCN_OFF_T "-", &rangenumber);

            if (ret == 1 && rangenumber>=0 && rangenumber < content_length)
            {
                /* Date: is required on all HTTP1.1 responses */
                char currenttime[50];
                time_t now;
                int strflen;
                struct tm result;
                off_t endpos;
                char *type;

                ret = fseeko (fh->fp, rangenumber, SEEK_SET);
                if (ret == -1)
                    break;

                httpclient->intro_offset = rangenumber;
                new_content_len = content_length - rangenumber;
                endpos = rangenumber + new_content_len - 1;
                if (endpos < 0)
                    endpos = 0;
                
                time(&now);
                strflen = strftime(currenttime, 50, "%a, %d-%b-%Y %X GMT",
                                   gmtime_r (&now, &result));
                httpclient->respcode = 206;
                type = fserve_content_type (path);
                bytes = snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.1 206 Partial Content\r\n"
                    "Date: %s\r\n"
                    "Accept-Ranges: bytes\r\n"
                    "Content-Length: %" PRI_OFF_T "\r\n"
                    "Content-Range: bytes %" PRI_OFF_T
                    "-%" PRI_OFF_T 
                    "/%" PRI_OFF_T "\r\n"
                    "Content-Type: %s\r\n\r\n",
                    currenttime,
                    new_content_len,
                    rangenumber,
                    endpos,
                    content_length,
                    type);
                free (type);
            }
            else
                break;
        }
        else
        {
            char *type = fserve_content_type (path);
            httpclient->respcode = 200;
            if (httpclient->flags & CLIENT_NO_CONTENT_LENGTH)
                bytes = snprintf (httpclient->refbuf->data, BUFSIZE,
                        "HTTP/1.0 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "\r\n",
                        type);
            else
                bytes = snprintf (httpclient->refbuf->data, BUFSIZE,
                        "HTTP/1.0 200 OK\r\n"
                        "Accept-Ranges: bytes\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %" PRI_OFF_T "\r\n"
                        "\r\n",
                        type,
                        content_length);
            free (type);
        }
        httpclient->refbuf->len = bytes;
        httpclient->pos = 0;
        httpclient->shared_data = fh;

        stats_event_inc (NULL, "file_connections");
        thread_mutex_unlock (&fh->lock);
        fserve_setup_client_fb (httpclient, NULL);
        return 0;
    } while (0);
    thread_mutex_unlock (&fh->lock);
    /* If we run into any issues with the ranges
       we fallback to a normal/non-range request */
    client_send_416 (httpclient);
    return -1;
}

// fh must be locked before calling this
static void fh_release (fh_node *fh)
{
    fh->refcount--;
    if (fh->finfo.mount[0])
        DEBUG2 ("refcount now %d on %s", fh->refcount, fh->finfo.mount);
    if (fh->refcount)
    {
        thread_mutex_unlock (&fh->lock);
        return;
    }
    /* now we will probably remove the fh, but to prevent a deadlock with
     * open_fh, we drop the node lock and acquire the tree and node locks
     * in that order and only remove if the refcount is still 0 */
    thread_mutex_unlock (&fh->lock);
    avl_tree_wlock (fh_cache);
    thread_mutex_lock (&fh->lock);
    if (fh->refcount)
        thread_mutex_unlock (&fh->lock);
    else
        avl_delete (fh_cache, fh, _delete_fh);
    avl_tree_unlock (fh_cache);
}

static void file_release (client_t *client)
{
    fh_node *fh = client->shared_data;
    const char *mount = httpp_getvar (client->parser, HTTPP_VAR_URI);

    rate_free (client->out_bitrate);
    if (client->respcode == 200)
    {
        ice_config_t *config = config_get_config ();
        mount_proxy *mountinfo = config_find_mount (config, mount);
        auth_release_listener (client, mount, mountinfo);
        config_release_config();
    }
    else
    {
        client->flags &= ~CLIENT_AUTHENTICATED;
        client_destroy (client);
    }
    global_reduce_bitrate_sampling (global.out_bitrate);
    if (fh)
    {
        thread_mutex_lock (&fh->lock);
        if (fh->finfo.flags & (FS_FALLBACK|FS_JINGLE))
            stats_event_dec (NULL, "listeners");
        fh_release (fh);
    }
}


struct _client_functions file_content_ops =
{
    file_send,
    file_release
};

struct _client_functions buffer_content_ops =
{
    prefile_send,
    file_release
};


static void fserve_move_listener (client_t *client)
{
    fh_node *fh = client->shared_data;
    fbinfo f;

    refbuf_release (client->refbuf);
    client->refbuf = NULL;
    rate_free (client->out_bitrate);
    client->out_bitrate = NULL;
    client->shared_data = NULL;
    thread_mutex_lock (&fh->lock);
    f.flags = fh->finfo.flags;
    f.limit = fh->finfo.limit;
    f.mount = fh->finfo.fallback;
    f.fallback = NULL;
    client->intro_offset = -1;
    move_listener (client, &f);
    fh_release (fh);
}


static int prefile_send (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int loop = 3, bytes;

    while (loop)
    {
        fh_node *fh = client->shared_data;
        loop--;
        if (fserve_running == 0 || client->con->error)
            return -1;
        if (refbuf == NULL || client->pos == refbuf->len)
        {
            if (fh && fh->finfo.fallback)
            {
                fserve_move_listener (client);
                return 0;
            }
            if (refbuf == NULL || refbuf->next == NULL)
            {
                if (fh)
                {
                    if (fh->fp) // is there a file to read from
                    {
                        int len = fh->finfo.limit ? 1400 : 4096;
                        refbuf_t *r = refbuf_new (len);
                        refbuf_release (client->refbuf);
                        client->refbuf = r;
                        client->pos = len;
                        client->ops = &file_content_ops;
                        return 1;
                    }
                }
                if (client->respcode)
                    return -1;
                client_send_404 (client, NULL);
                thread_mutex_lock (&fh->lock);
                fh_release (fh);
                return 0;
            }
            else
            {
                refbuf = refbuf->next;
                client->refbuf->next = NULL;
                refbuf_release (client->refbuf);
                client->refbuf = refbuf;
            }
            client->pos = 0;
        }
        bytes = format_generic_write_to_client (client);
        if (bytes < 0)
        {
            client->schedule_ms = client->worker->time_ms + 150;
            return 0;
        }
        global_add_bitrates (global.out_bitrate, bytes, client->worker->time_ms);
        if (bytes < 4096)
            break;
    }
    client->schedule_ms = client->worker->time_ms + (loop ? 50 : 15);
    return 0;
}


static int file_send (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int loop = 5, bytes;
    fh_node *fh = client->shared_data;

    if (client->con->discon_time && client->worker->current_time.tv_sec >= client->con->discon_time)
        return -1;
    while (loop)
    {
        loop--;
        if (fserve_running == 0 || client->con->error)
            return -1;
        if (fh->finfo.limit)
        {
            long rate = rate_avg (client->out_bitrate);
            if (rate == 0) loop = 0;
            if (fh->finfo.limit < rate)
            {
                rate_add (client->out_bitrate, 0, client->worker->time_ms);
                client->schedule_ms = client->worker->time_ms + 150;
                return 0;
            }
        }
        if (client->pos == refbuf->len)
        {
            bytes = 0;
            if (fh->finfo.fallback)
            {
                fserve_move_listener (client);
                return 0;
            }
            if (fh->fp)
            {
                thread_mutex_lock (&fh->lock);
                if (fseeko (fh->fp, client->intro_offset, SEEK_SET) == 0 &&
                        (bytes = fread (refbuf->data, 1, refbuf->len, fh->fp)) > 0)
                {
                    refbuf->len = bytes;
                    client->intro_offset += bytes;
                }
                thread_mutex_unlock (&fh->lock);
            }
            if (bytes == 0)
                return -1;
            client->pos = 0;
        }
        bytes = client->check_buffer (client);
        if (bytes < 0)
            return 0;
        global_add_bitrates (global.out_bitrate, bytes, client->worker->time_ms);
        if (fh->finfo.limit)
            rate_add (client->out_bitrate, bytes, client->worker->time_ms);
    }
    client->schedule_ms = client->worker->time_ms + 10;
    return 1;
}


void fserve_setup_client_fb (client_t *client, fbinfo *finfo)
{
    client->ops = &buffer_content_ops;
    if (finfo)
    {
        fh_node *fh = open_fh (finfo);
        client->shared_data = fh;
        if (fh)
        {
            if (fh->finfo.limit)
                client->out_bitrate = rate_setup (50,1000);
            thread_mutex_unlock (&fh->lock);
        }
    }
    else
    {
        client->check_buffer = format_generic_write_to_client;
    }
    client->intro_offset = 0;
    client->flags |= CLIENT_ACTIVE;
}


void fserve_setup_client (client_t *client, const char *mount)
{
    fbinfo finfo;
    finfo.flags = FS_NORMAL;
    finfo.mount = (char *)mount;
    finfo.fallback = NULL;
    finfo.limit = 0;
    client->check_buffer = format_generic_write_to_client;
    fserve_setup_client_fb (client, &finfo);
}


void fserve_set_override (const char *mount, const char *dest)
{
    fh_node fh, *result;

    fh.finfo.flags = FS_FALLBACK;
    fh.finfo.mount = (char *)mount;
    fh.finfo.fallback = NULL;

    avl_tree_rlock (fh_cache);
    if (avl_get_by_key (fh_cache, &fh, (void**)&result) == 0)
    {
        char *tmp = result->finfo.fallback;
        result->finfo.fallback = strdup (dest);
        free (tmp);
    }
    avl_tree_unlock (fh_cache);
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

void fserve_recheck_mime_types (ice_config_t *config)
{
    FILE *mimefile;
    char line[4096];
    char *type, *ext, *cur;
    mime_type *mapping;
    avl_tree *new_mimetypes;

    if (config->mimetypes_fn == NULL)
        return;
    mimefile = fopen (config->mimetypes_fn, "r");
    if (mimefile == NULL)
    {
        WARN1 ("Cannot open mime types file %s", config->mimetypes_fn);
        return;
    }

    new_mimetypes = avl_tree_new(_compare_mappings, NULL);

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
                if (!avl_get_by_key (new_mimetypes, mapping, &tmp))
                    avl_delete (new_mimetypes, mapping, _delete_mapping);
                if (avl_insert (new_mimetypes, mapping) != 0)
                    _delete_mapping (mapping);
            }
        }
    }
    fclose(mimefile);

    thread_spin_lock (&pending_lock);
    if (mimetypes)
        avl_tree_free (mimetypes, _delete_mapping);
    mimetypes = new_mimetypes;
    thread_spin_unlock (&pending_lock);
}


#if 0
/* generate xml list containing listener details for clients attached via the file
 * serving engine.
 */
xmlDocPtr command_show_listeners(client_t *client, source_t *source,
        int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;
    unsigned long id = -1;
    char *ID_str = NULL;
    char buf[22];

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    xmlDocSetRootElement(doc, node);

    {
        client_t *listener;
        thread_mutex_lock (&source->lock);

        listener = source->active_clients;
        while (listener)
        {
            add_listener_node (srcnode, listener);
            listener = listener->next;
        }
        thread_mutex_unlock (&source->lock);
    }

    admin_send_response(doc, client, response, "listclients.xsl");
    xmlFreeDoc(doc);
}

#endif
