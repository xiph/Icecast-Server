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
#include "format_mp3.h"

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
    int peak;
    int max;
    FILE *fp;
    time_t stats_update;
    format_plugin_t *format;
    avl_tree *clients;
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
        int count = 20;
        while (fh_cache->length && count)
        {
            DEBUG1 ("waiting for %u entries to clear", fh_cache->length);
            thread_sleep (100000);
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
    return 0;
}

static int _delete_fh (void *mapping)
{
    fh_node *fh = mapping;
    fh->refcount--;
    if (fh->refcount)
        WARN2 ("handle for %s has refcount %d", fh->finfo.mount, fh->refcount);
    else
    {
        thread_mutex_unlock (&fh->lock);
        thread_mutex_destroy (&fh->lock);
    }
    if (fh->fp)
        fclose (fh->fp);
    if (fh->format)
    {
        format_plugin_clear (fh->format, NULL);
        free (fh->format);
    }
    avl_tree_free (fh->clients, NULL);
    free (fh->finfo.mount);
    free (fh->finfo.fallback);
    free (fh);

    return 1;
}


static void remove_from_fh (fh_node *fh, client_t *client)
{
    avl_delete (fh->clients, client, NULL);
}


/* find/create handle and return it with the structure in a locked state */
static fh_node *open_fh (fbinfo *finfo, client_t *client)
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
        avl_tree_unlock (fh_cache);
        result->refcount++;
        if (client)
        {
            if (finfo->mount && (finfo->flags & FS_FALLBACK))
            {
                stats_event_args (result->finfo.mount, "listeners", "%ld", result->refcount);
                if (result->refcount > result->peak)
                {
                    result->peak = result->refcount;
                    stats_event_args (result->finfo.mount, "listener_peak", "%ld", result->peak);
                }
            }
            avl_insert (result->clients, client);
            if (result->format)
            {
                if (result->format->create_client_data && client->format_data == NULL)
                {
                    result->format->create_client_data (result->format, client);
                    client->check_buffer = result->format->write_buf_to_client;
                }
            }
        }
        DEBUG2 ("refcount now %d for %s", result->refcount, result->finfo.mount);
        return result;
    }

    // insert new one
    if (fh->finfo.mount[0])
    {
        char *fullpath= util_get_path_from_normalised_uri (fh->finfo.mount, fh->finfo.flags&FS_USE_ADMIN);
        if (client)
        {
            if (finfo->flags & FS_FALLBACK)
                DEBUG1 ("lookup of fallback file \"%s\"", finfo->mount);
            else
                DEBUG1 ("lookup of \"%s\"", finfo->mount);
        }
        fh->fp = fopen (fullpath, "rb");
        if (fh->fp == NULL)
        {
            if (client)
                WARN1 ("Failed to open \"%s\"", fullpath);
            if (finfo->flags & FS_FALLBACK)
            {
                avl_tree_unlock (fh_cache);
                free (fullpath);
                free (fh);
                return NULL;
            }
        }
        if (client && finfo->flags & FS_FALLBACK)
        {
            char *contenttype = fserve_content_type (fullpath);

            stats_event (finfo->mount, "fallback", "file");
            fh->format = calloc (1, sizeof (format_plugin_t));
            fh->format->type = format_get_type (contenttype);
            free (contenttype);
            fh->format->mount = fh->finfo.mount;
            if (format_get_plugin (fh->format, NULL) < 0)
            {
                avl_tree_unlock (fh_cache);
                free (fullpath);
                free (fh);
                return NULL;
            }
            if (fh->format->create_client_data && client->format_data == NULL)
            {
                fh->format->create_client_data (fh->format, client);
                client->check_buffer = fh->format->write_buf_to_client;
            }
        }
        free (fullpath);
    }
    thread_mutex_create (&fh->lock);
    thread_mutex_lock (&fh->lock);
    fh->clients = avl_tree_new (client_compare, NULL);
    fh->refcount = 1;
    fh->peak = 1;
    if (client)
    {
        if (finfo->mount && (finfo->flags & FS_FALLBACK))
        {
            stats_event_flags (fh->finfo.mount, "listeners", "1", STATS_HIDDEN);
            stats_event_flags (fh->finfo.mount, "listener_peak", "1", STATS_HIDDEN);
        }
        avl_insert (fh->clients, client);
    }
    fh->finfo.mount = strdup (finfo->mount);
    if (finfo->fallback)
        fh->finfo.fallback = strdup (finfo->fallback);
    avl_insert (fh_cache, fh);
    avl_tree_unlock (fh_cache);
    return fh;
}


static int fill_http_headers (client_t *client, const char *path, struct stat *file_buf)
{
    char *type;
    off_t content_length = 0;
    const char *range = httpp_getvar (client->parser, "range");
    refbuf_t *ref = client->refbuf;


    if (file_buf)
        content_length = file_buf->st_size;
    /* full http range handling is currently not done but we deal with the common case */
    if (range)
    {
        off_t new_content_len = 0, rangenumber = 0;
        int ret = 0;

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
            fh_node * fh = client->shared_data;

            ret = fseeko (fh->fp, rangenumber, SEEK_SET);
            if (ret == -1)
                return -1;

            client->intro_offset = rangenumber;
            new_content_len = content_length - rangenumber;
            endpos = rangenumber + new_content_len - 1;
            if (endpos < 0)
                endpos = 0;

            now = client->worker->current_time.tv_sec;
            strflen = strftime(currenttime, 50, "%a, %d-%b-%Y %X GMT",
                    gmtime_r (&now, &result));
            client->respcode = 206;
            type = fserve_content_type (path);
            snprintf (ref->data, BUFSIZE,
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
        }
        else
            return -1;
    }
    else
    {
        type = fserve_content_type (path);
        client->respcode = 200;
        if (content_length)
            snprintf (ref->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Accept-Ranges: bytes\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %" PRI_OFF_T "\r\n"
                    "\r\n",
                    type,
                    content_length);
        else
            snprintf (ref->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: %s\r\n"
                    "\r\n",
                    type);
    }
    free (type);
    client->refbuf->len = strlen (ref->data);
    client->pos = 0;
    ref->flags |= WRITE_BLOCK_GENERIC;
    return 0;
}


/* client has requested a file, so check for it and send the file.  Do not
 * refer to the client_t afterwards.  return 0 for success, -1 on error.
 */
int fserve_client_create (client_t *httpclient, const char *path)
{
    struct stat file_buf;
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

    finfo.flags = 0;
    finfo.mount = (char *)path;
    finfo.fallback = NULL;
    finfo.limit = 0;

    fh = open_fh (&finfo, httpclient);
    if (fh == NULL)
    {
        WARN1 ("Problem accessing file \"%s\"", fullpath);
        client_send_404 (httpclient, "File not readable");
        free (fullpath);
        return -1;
    }
    free (fullpath);

    httpclient->intro_offset = 0;
    httpclient->shared_data = fh;
    if (fill_http_headers (httpclient, path, &file_buf) < 0)
    {
        thread_mutex_unlock (&fh->lock);
        client_send_416 (httpclient);
        return -1;
    }

    stats_event_inc (NULL, "file_connections");
    thread_mutex_unlock (&fh->lock);
    fserve_setup_client_fb (httpclient, NULL);
    return 0;
}

// fh must be locked before calling this
static void fh_release (fh_node *fh)
{
    if (fh->finfo.mount[0])
        DEBUG2 ("refcount now %d on %s", fh->refcount, fh->finfo.mount);
    if (fh->refcount > 1)
    {
        fh->refcount--;
        thread_mutex_unlock (&fh->lock);
        return;
    }
    /* now we will probably remove the fh, but to prevent a deadlock with
     * open_fh, we drop the node lock and acquire the tree and node locks
     * in that order and only remove if the refcount is still 0 */
    thread_mutex_unlock (&fh->lock);
    avl_tree_wlock (fh_cache);
    thread_mutex_lock (&fh->lock);
    if (fh->refcount > 1)
        thread_mutex_unlock (&fh->lock);
    else
        avl_delete (fh_cache, fh, _delete_fh);
    avl_tree_unlock (fh_cache);
}


static void file_release (client_t *client)
{
    fh_node *fh = client->shared_data;

    if (fh)
    {
        thread_mutex_lock (&fh->lock);
        if (fh->finfo.flags & FS_FALLBACK)
            stats_event_dec (NULL, "listeners");
        remove_from_fh (fh, client);
        if (fh->refcount == 1)
            stats_event (fh->finfo.mount, NULL, NULL);
        fh_release (fh);
    }
    if (client->respcode == 200)
    {
        const char *mount = httpp_getvar (client->parser, HTTPP_VAR_URI);
        ice_config_t *config = config_get_config ();
        mount_proxy *mountinfo = config_find_mount (config, mount);
        if (mountinfo && mountinfo->access_log.name)
            logging_access_id (&mountinfo->access_log, client);
        auth_release_listener (client, mount, mountinfo);
        config_release_config();
    }
    else
    {
        client->flags &= ~CLIENT_AUTHENTICATED;
        client_destroy (client);
    }
    global_reduce_bitrate_sampling (global.out_bitrate);
}


struct _client_functions buffer_content_ops =
{
    prefile_send,
    file_release
};


struct _client_functions file_content_ops =
{
    file_send,
    file_release
};


static void fserve_move_listener (client_t *client)
{
    fh_node *fh = client->shared_data;
    fbinfo f;

    refbuf_release (client->refbuf);
    client->refbuf = NULL;
    client->shared_data = NULL;
    thread_mutex_lock (&fh->lock);
    remove_from_fh (fh, client);
    if (fh->refcount == 1)
        stats_event (fh->finfo.mount, NULL, NULL);
    f.flags = fh->finfo.flags;
    f.limit = fh->finfo.limit;
    f.mount = fh->finfo.fallback;
    f.fallback = NULL;
    client->intro_offset = -1;
    move_listener (client, &f);
    fh_release (fh);
}

struct _client_functions throttled_file_content_ops;

static int prefile_send (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int loop = 6, bytes, written = 0;
    worker_t *worker = client->worker;

    while (loop)
    {
        fh_node *fh = client->shared_data;
        loop--;
        if (fserve_running == 0 || client->connection.error)
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
                        int len = 8192;
                        if (fh->finfo.flags & FS_FALLBACK)
                        {
                            len = 1400;
                            client->ops = &throttled_file_content_ops;
                        }
                        else
                            client->ops = &file_content_ops;
                        refbuf_release (client->refbuf);
                        client->refbuf = refbuf_new(len);
                        client->pos = len;
                        return 0;
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
        if (refbuf->flags & WRITE_BLOCK_GENERIC)
            bytes = format_generic_write_to_client (client);
        else 
            bytes = client->check_buffer (client);
        if (bytes < 0)
        {
            client->schedule_ms = worker->time_ms + 300;
            return 0;
        }
        written += bytes;
        global_add_bitrates (global.out_bitrate, bytes, worker->time_ms);
        if (written > 30000)
            break;
    }
    client->schedule_ms = worker->time_ms + 100;
    return 0;
}

static int read_file (client_t *client, int blksize)
{
    refbuf_t *refbuf = client->refbuf;
    fh_node *fh = client->shared_data;
    int bytes = 0;

    switch (fseeko (fh->fp, client->intro_offset, SEEK_SET))
    {
        case -1:
            client->connection.error = 1;
            break;
        default:
            bytes = fread (refbuf->data, 1, blksize, fh->fp);
            if (bytes > 0)
            {
                refbuf->len = bytes;
                client->intro_offset += bytes;
            }
    }
    return bytes;
}


/* fast send routine */
static int file_send (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int loop = 6, bytes, written = 0, ret = 0;
    fh_node *fh = client->shared_data;
    worker_t *worker = client->worker;
    time_t now;

    client->schedule_ms = worker->time_ms;
    now = worker->current_time.tv_sec;
    /* slowdown if max bandwidth is exceeded, but allow for short-lived connections to avoid 
     * this, eg admin requests */
    if (throttle_sends > 1 && now - client->connection.con_time > 1)
    {
        client->schedule_ms += 500;
        loop = 1; 
    }
    while (loop && written < 30000)
    {
        loop--;
        if (fserve_running == 0 || client->connection.error)
            return -1;
        if (client->connection.discon_time && now >= client->connection.discon_time)
            return -1;
        if (client->pos == refbuf->len)
        {
            thread_mutex_lock (&fh->lock);
            ret = read_file (client, 8192);
            thread_mutex_unlock (&fh->lock);
            if (ret == 0)
                return -1;
            client->pos = 0;
        }
        bytes = client->check_buffer (client);
        if (bytes < 0)
            break;
        written += bytes;
        client->schedule_ms += 3;
    }
    return 0;
}


/* send routine for files sent at a target bitrate, eg fallback files. */
static int throttled_file_send (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int  bytes;
    fh_node *fh = client->shared_data;
    time_t now;
    worker_t *worker = client->worker;
    unsigned long secs; 
    unsigned int  rate = 0;
    unsigned int limit = fh->finfo.limit;

    if (fserve_running == 0 || client->connection.error)
        return -1;
    now = worker->current_time.tv_sec;
    secs = now - client->timer_start; 
    client->schedule_ms = worker->time_ms;
    if (client->connection.discon_time && now >= client->connection.discon_time)
        return -1;
    if (fh->finfo.fallback)
    {
        fserve_move_listener (client);
        return 0;
    }
    if (client->flags & CLIENT_WANTS_FLV) /* increase limit for flv clients as wrapping takes more space */
        limit = (unsigned long)(limit * 1.02);
    if (secs)
        rate = (client->counter+1400)/secs;
    thread_mutex_lock (&fh->lock);
    if (rate > limit)
    {
        client->schedule_ms += (1000*(rate - limit))/limit;
        rate_add (fh->format->out_bitrate, 0, worker->time_ms);
        thread_mutex_unlock (&fh->lock);
        global_add_bitrates (global.out_bitrate, 0, worker->time_ms);
        return 0;
    }
    if (fh->stats_update <= now)
    {
        stats_event_args (fh->finfo.mount, "outgoing_kbitrate", "%ld",
                (long)((8 * rate_avg (fh->format->out_bitrate))/1024));
        fh->stats_update = now + 5;
    }
    if (client->pos == refbuf->len)
    {
        if (read_file (client, 1400) == 0)
        {
            /* loop fallback file  */
            thread_mutex_unlock (&fh->lock);
            client->intro_offset = 0;
            client->pos = refbuf->len = 0;
            client->schedule_ms += 150;
            return 0;
        }
        client->pos = 0;
    }
    bytes = client->check_buffer (client);
    if (bytes < 0)
        bytes = 0;
    rate_add (fh->format->out_bitrate, bytes, worker->time_ms);
    thread_mutex_unlock (&fh->lock);
    global_add_bitrates (global.out_bitrate, bytes, worker->time_ms);
    client->counter += bytes;
    client->schedule_ms += (1000/(limit/1400*2));

    /* progessive slowdown if max bandwidth is exceeded. */
    if (throttle_sends > 1)
        client->schedule_ms += 300;
    return 0;
}


struct _client_functions throttled_file_content_ops =
{
    throttled_file_send,
    file_release
};


/* return 0 for success, 1 for fallback invalid */
int fserve_setup_client_fb (client_t *client, fbinfo *finfo)
{
    if (finfo)
    {
        fh_node *fh;
        if (finfo->flags & FS_FALLBACK && finfo->limit == 0)
            return 1;
        fh = open_fh (finfo, client);
        if (fh == NULL)
            return 1;
        client->shared_data = fh;

        if (fh->finfo.limit)
        {
            client->timer_start = client->worker->current_time.tv_sec;
            if (client->connection.sent_bytes == 0)
                client->timer_start -= 2;
            client->counter = 0;
            fh->stats_update = client->timer_start + 5;
            fh->format->out_bitrate = rate_setup (10000, 1000);
            global_reduce_bitrate_sampling (global.out_bitrate);
        }
        thread_mutex_unlock (&fh->lock);
        if (client->respcode == 0)
            fill_http_headers (client, finfo->mount, NULL);
    }
    else
        client->check_buffer = format_generic_write_to_client;

    client->ops = &buffer_content_ops;
    client->flags &= ~CLIENT_HAS_INTRO_CONTENT;
    client->intro_offset = 0;
    if (client->flags & CLIENT_ACTIVE)
        client->schedule_ms = client->worker->time_ms;
    else
    {
        worker_t *worker = client->worker;
        client->flags |= CLIENT_ACTIVE;
        worker_wakeup (worker); /* worker may of already processed client but make sure */
    }
    return 0;
}


void fserve_setup_client (client_t *client, const char *mount)
{
    client->check_buffer = format_generic_write_to_client;
    fserve_setup_client_fb (client, NULL);
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
        INFO2 ("move clients from %s to %s", mount, dest);
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


void fserve_kill_client (client_t *client, const char *mount, int response)
{
    int c = 2, id;
    fbinfo finfo;
    xmlDocPtr doc;
    xmlNodePtr node;
    const char *idtext, *v = "0";
    char buf[50];

    finfo.flags = 0;
    finfo.mount = (char*)mount;
    finfo.limit = 0;
    finfo.fallback = NULL;

    idtext = httpp_get_query_param (client->parser, "id");
    if (idtext == NULL)
    {
        client_send_400 (client, "missing parameter id");
        return;
    }
    id = atoi(idtext);

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);
    snprintf (buf, sizeof(buf), "Client %d not found", id);

    while (c)
    {
        fh_node *fh = open_fh (&finfo, NULL);
        if (fh)
        {
            avl_node *node = avl_get_first (fh->clients);

            while (node)
            {
                client_t *listener = (client_t *)node->key;
                if (listener->connection.id == id)
                {
                    listener->connection.error = 1;
                    snprintf (buf, sizeof(buf), "Client %d removed", id);
                    v = "1";
                    break;
                }
                node = avl_get_next (node);
            }
            fh_release (fh);
            break;
        }
        c--;
        finfo.flags = FS_FALLBACK;
    }
    xmlNewChild (node, NULL, XMLSTR("message"), XMLSTR(buf));
    xmlNewChild (node, NULL, XMLSTR("return"), XMLSTR(v));
    admin_send_response (doc, client, response, "response.xsl");
    xmlFreeDoc(doc);
}


int fserve_list_clients_xml (xmlNodePtr srcnode, fbinfo *finfo)
{
    int ret = 0;
    fh_node *fh = open_fh (finfo, NULL);

    if (fh)
    {
        avl_node *anode = avl_get_first (fh->clients);

        while (anode)
        {
            client_t *listener = (client_t *)anode->key;
            char buf [100];

            xmlNodePtr node = xmlNewChild (srcnode, NULL, XMLSTR("listener"), NULL);
            const char *useragent;
            snprintf (buf, sizeof (buf), "%lu", listener->connection.id);
            xmlSetProp (node, XMLSTR("id"), XMLSTR(buf));

            xmlNewChild (node, NULL, XMLSTR("ip"), XMLSTR(listener->connection.ip));
            useragent = httpp_getvar (listener->parser, "user-agent");
            if (useragent)
            {
                xmlChar *str = xmlEncodeEntitiesReentrant (srcnode->doc, XMLSTR(useragent));
                xmlNewChild (node, NULL, XMLSTR("useragent"), str);
                xmlFree (str);
            }
            xmlNewChild (node, NULL, XMLSTR("lag"), XMLSTR( "0"));
            snprintf (buf, sizeof (buf), "%lu",
                    (unsigned long)(listener->worker->current_time.tv_sec - listener->connection.con_time));
            xmlNewChild (node, NULL, XMLSTR("connected"), XMLSTR(buf));
            if (listener->username)
            {
                xmlChar *str = xmlEncodeEntitiesReentrant (srcnode->doc, XMLSTR(listener->username));
                xmlNewChild (node, NULL, XMLSTR("username"), str);
                xmlFree (str);
            }

            ret++;
            anode = avl_get_next (anode);
        }
        fh_release (fh);
    }
    return ret;
}


void fserve_list_clients (client_t *client, const char *mount, int response, int show_listeners)
{
    int ret;
    fbinfo finfo;
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;

    finfo.flags = FS_FALLBACK;
    finfo.mount = (char*)mount;
    finfo.limit = 0;
    finfo.fallback = NULL;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    xmlDocSetRootElement(doc, node);
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(mount));

    ret = fserve_list_clients_xml (srcnode, &finfo);
    if (ret == 0)
    {
        finfo.flags = 0;
        ret = fserve_list_clients_xml (srcnode, &finfo);
    }
    if (ret)
    {
        char buf[20];
        snprintf (buf, sizeof(buf), "%u", ret);
        xmlNewChild (srcnode, NULL, XMLSTR("listeners"), XMLSTR(buf));
        admin_send_response (doc, client, response, "listclients.xsl");
    }
    else
        client_send_400 (client, "mount does not exist");
    xmlFreeDoc(doc);
}
