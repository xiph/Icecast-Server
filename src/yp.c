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

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

#include <thread/thread.h>

#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "format.h"
#include "source.h"
#include "cfgfile.h"
#include "stats.h"

#ifdef WIN32
#define snprintf _snprintf
#endif

#define CATMODULE "yp" 

struct yp_server
{
    char        *url;
    unsigned    url_timeout;
    unsigned    touch_interval;
    int         remove;

    CURL *curl;
    struct ypdata_tag *mounts, *pending_mounts;
    struct yp_server *next;
    char curl_error[CURL_ERROR_SIZE];
};



typedef struct ypdata_tag
{
    int remove;
    int release;
    int cmd_ok;

    char *sid;
    char *mount;
    char *url;
    char *listen_url;
    char *server_name;
    char *server_desc;
    char *server_genre;
    char *cluster_password;
    char *bitrate;
    char *audio_info;
    char *server_type;
    char *current_song;
    char *subtype;

    struct yp_server *server;
    time_t      next_update;
    unsigned    touch_interval;
    char        *error_msg;
    unsigned    (*process)(struct ypdata_tag *yp, char *s, unsigned len);

    struct ypdata_tag *next;
} ypdata_t;


static rwlock_t yp_lock;
static mutex_t yp_pending_lock;

volatile static struct yp_server *active_yps = NULL, *pending_yps = NULL;
static volatile int yp_update = 0;
static int yp_running;
static time_t now;
static thread_type *yp_thread;

static void *yp_update_thread(void *arg);
static void add_yp_info (ypdata_t *yp, char *stat_name, void *info, int type);
static unsigned do_yp_remove (ypdata_t *yp, char *s, unsigned len);
static unsigned do_yp_add (ypdata_t *yp, char *s, unsigned len);
static unsigned do_yp_touch (ypdata_t *yp, char *s, unsigned len);
static void yp_destroy_ypdata(ypdata_t *ypdata);


/* curl callback used to parse headers coming back from the YP server */
static int handle_returned_header (void *ptr, size_t size, size_t nmemb, void *stream)
{
    ypdata_t *yp = stream;
    unsigned bytes = size * nmemb;

    /* DEBUG2 ("header from YP is \"%.*s\"", bytes, ptr); */
    if (strncmp (ptr, "YPResponse: 1", 13) == 0)
        yp->cmd_ok = 1;

    if (strncmp (ptr, "YPMessage: ", 11) == 0)
    {
        unsigned len = bytes - 11;
        free (yp->error_msg);
        yp->error_msg = calloc (1, len);
        if (yp->error_msg)
            sscanf (ptr, "YPMessage: %[^\r\n]", yp->error_msg);
    }

    if (yp->process == do_yp_add)
    {
        if (strncmp (ptr, "SID: ", 5) == 0)
        {
            unsigned len = bytes - 5;
            free (yp->sid);
            yp->sid = calloc (1, len);
            if (yp->sid)
                sscanf (ptr, "SID: %[^\r\n]", yp->sid);
        }
        if (strncmp (ptr, "TouchFreq: ", 11) == 0)
        {
            unsigned secs;
            sscanf (ptr, "TouchFreq: %u", &secs);
            if (secs < 30)
                secs = 30;
            DEBUG1 ("server touch interval is %u", secs);
            yp->touch_interval = secs;
        }
    }
    return (int)bytes;
}


/* capture returned data, but don't do anything with it, shouldn't be any */
static int handle_returned_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
    return (int)(size*nmemb);
}


/* search the active and pending YP server lists */
static struct yp_server *find_yp_server (const char *url)
{
    struct yp_server *server;

    server = (struct yp_server *)active_yps;
    while (server)
    {
        if (strcmp (server->url, url) == 0)
            return server;
        server = server->next;
    }
    server = (struct yp_server *)pending_yps;
    while (server)
    {
        if (strcmp (server->url, url) == 0)
            break;
        server = server->next;
    }
    return server;
}


static void destroy_yp_server (struct yp_server *server)
{
    if (server == NULL)
        return;
    DEBUG1 ("Removing YP server entry for %s", server->url);
    if (server->curl)
        curl_easy_cleanup (server->curl);
    if (server->mounts) WARN0 ("active ypdata not freed up");
    if (server->pending_mounts) WARN0 ("pending ypdata not freed up");
    free (server->url);
    free (server);
}



/* search for a ypdata entry corresponding to a specific mountpoint */
static ypdata_t *find_yp_mount (ypdata_t *mounts, const char *mount)
{
    ypdata_t *yp = mounts;
    while (yp)
    {
        if (strcmp (yp->mount, mount) == 0)
            break;
        yp = yp->next;
    }
    return yp;
}


void yp_recheck_config (ice_config_t *config)
{
    int i;
    struct yp_server *server;

    DEBUG0("Updating YP configuration");
    thread_rwlock_rlock (&yp_lock);

    server = (struct yp_server *)active_yps;
    while (server)
    {
        server->remove = 1;
        server = server->next;
    }
    /* for each yp url in config, check to see if one exists 
       if not, then add it. */
    for (i=0 ; i < config->num_yp_directories; i++)
    {
        server = find_yp_server (config->yp_url[i]);
        if (server == NULL)
        {
            server = calloc (1, sizeof (struct yp_server));

            if (server == NULL)
            {
                destroy_yp_server (server);
                break;
            }
            server->url = strdup (config->yp_url[i]);
            server->url_timeout = config->yp_url_timeout[i];
            server->touch_interval = config->yp_touch_interval[i];
            server->curl = curl_easy_init();
            if (server->curl == NULL)
            {
                destroy_yp_server (server);
                break;
            }
            if (server->touch_interval < 30)
                server->touch_interval = 30;
            curl_easy_setopt (server->curl, CURLOPT_URL, server->url);
            curl_easy_setopt (server->curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
            curl_easy_setopt (server->curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
            curl_easy_setopt (server->curl, CURLOPT_WRITEDATA, server->curl);
            curl_easy_setopt (server->curl, CURLOPT_TIMEOUT, server->url_timeout);
            curl_easy_setopt (server->curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt (server->curl, CURLOPT_ERRORBUFFER, &(server->curl_error[0]));
            server->next = (struct yp_server *)pending_yps;
            pending_yps = server;
            INFO3 ("Adding new YP server \"%s\" (timeout %ds, default interval %ds)",
                    server->url, server->url_timeout, server->touch_interval);
        }
        else
        {
            server->remove = 0;
        }
    }
    thread_rwlock_unlock (&yp_lock);
    yp_update = 1;
}


void yp_initialize()
{
    ice_config_t *config = config_get_config();
    thread_rwlock_create (&yp_lock);
    thread_mutex_create (&yp_pending_lock);
    yp_recheck_config (config);
    config_release_config ();
    yp_thread = thread_create("YP Touch Thread", yp_update_thread,
                            (void *)NULL, THREAD_ATTACHED);
}



/* handler for curl, checks if successful handling occurred */
static int send_to_yp (const char *cmd, ypdata_t *yp, char *post)
{
    int curlcode;
    struct yp_server *server = yp->server;

    /* DEBUG2 ("send YP (%s):%s", cmd, post); */
    yp->cmd_ok = 0;
    curl_easy_setopt (server->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (server->curl, CURLOPT_WRITEHEADER, yp);
    curlcode = curl_easy_perform (server->curl);
    if (curlcode)
    {
        yp->process = do_yp_add;
        yp->next_update += 300;
        ERROR2 ("connection to %s failed with \"%s\"", server->url, server->curl_error);
        return -1;
    }
    if (yp->cmd_ok == 0)
    {
        if (yp->error_msg == NULL)
            yp->error_msg = strdup ("no response from server");
        yp->process = do_yp_add;
        yp->next_update += 300;
        ERROR3 ("YP %s on %s failed: %s", cmd, server->url, yp->error_msg);
        return -1;
    }
    DEBUG2 ("YP %s at %s succeeded", cmd, server->url);
    return 0;
}


/* routines for building and issues requests to the YP server */
static unsigned do_yp_remove (ypdata_t *yp, char *s, unsigned len)
{
    if (yp->sid)
    {
        int ret = snprintf (s, len, "action=remove&sid=%s", yp->sid);
        if (ret >= (signed)len)
            return ret+1;

        INFO1 ("clearing up YP entry for %s", yp->mount);
        send_to_yp ("remove", yp, s);
        free (yp->sid);
        yp->sid = NULL;
    }
    yp_update = 1;
    yp->remove = 1;
    yp->process = do_yp_add;

    return 0;
}


static unsigned do_yp_add (ypdata_t *yp, char *s, unsigned len)
{
    int ret;
    char *value;

    value = stats_get_value (yp->mount, "subtype");
    if (value)
    {
        add_yp_info (yp, "subtype", value, YP_SUBTYPE);
        free (value);
    }

    ret = snprintf (s, len, "action=add&sn=%s&genre=%s&cpswd=%s&desc="
                    "%s&url=%s&listenurl=%s&type=%s&stype=%s&b=%s&%s\r\n",
                    yp->server_name, yp->server_genre, yp->cluster_password,
                    yp->server_desc, yp->url, yp->listen_url,
                    yp->server_type, yp->subtype, yp->bitrate, yp->audio_info);
    if (ret >= (signed)len)
        return ret+1;
    if (send_to_yp ("add", yp, s) == 0)
    {
        yp->process = do_yp_touch;
        /* force first touch in 5 secs */
        yp->next_update = time(NULL) + 5;
    }

    return 0;
}


static unsigned do_yp_touch (ypdata_t *yp, char *s, unsigned len)
{
    unsigned listeners = 0;
    char *val, *artist, *title;
    int ret;
    char *max_listeners;

    artist = (char *)stats_get_value (yp->mount, "artist");
    title = (char *)stats_get_value (yp->mount, "title");
    if (artist || title)
    {
         char *song;
         char *separator = " - ";
         if (artist == NULL)
         {
             artist = strdup("");
             separator = "";
         }
         if (title == NULL) title = strdup("");
         song = malloc (strlen (artist) + strlen (title) + strlen (separator) +1);
         if (song)
         {
             sprintf (song, "%s%s%s", artist, separator, title);
             add_yp_info(yp, "yp_currently_playing", song, YP_CURRENT_SONG);
             free (song);
         }
    }
    free (artist);
    free (title);

    val = (char *)stats_get_value (yp->mount, "listeners");
    if (val)
    {
        listeners = atoi (val);
        free (val);
    }
    max_listeners = stats_get_value (yp->mount, "max_listeners");
    if (max_listeners == NULL || strcmp (max_listeners, "unlimited") == 0)
    {
        free (max_listeners);
        max_listeners = (char *)stats_get_value (NULL, "client_limit");
    }

    ret = snprintf (s, len, "action=touch&sid=%s&st=%s"
            "&listeners=%u&max_listeners=%s\r\n",
            yp->sid, yp->current_song, listeners, max_listeners);

    if (ret >= (signed)len)
        return ret+1; /* space required for above text and nul*/

    send_to_yp ("touch", yp, s);
    return 0;
}



static void process_ypdata (struct yp_server *server, ypdata_t *yp)
{
    unsigned len = 512;
    char *s = NULL, *tmp;

    if (now < yp->next_update)
        return;
    yp->next_update = now + yp->touch_interval;

    /* loop just in case the memory area isn't big enough */
    while (1)
    {
        unsigned ret;
        if ((tmp = realloc (s, len)) == NULL)
            return;
        s = tmp;

        if (yp->release)
        {
            yp->process = do_yp_remove;
            yp->next_update = 0;
        }

        ret = yp->process (yp, s, len);
        if (ret == 0)
        {
           free (s);
           return;
        }
        len = ret;
    }
}


static void yp_process_server (struct yp_server *server)
{
    ypdata_t *yp;

    /* DEBUG1("processing yp server %s", server->url); */
    yp = server->mounts;
    while (yp)
    {
        now = time (NULL);
        process_ypdata (server, yp);
        yp = yp->next;
    }
}



static ypdata_t *create_yp_entry (source_t *source)
{
    ypdata_t *yp;
    char *s;

    if (source->running == 0 || source->yp_public == 0)
        return NULL;
    yp = calloc (1, sizeof (ypdata_t));
    do
    {
        unsigned len = 512;
        int ret;
        char *url;
        mount_proxy *mountproxy = NULL;
        ice_config_t *config;

        if (yp == NULL)
            break;
        yp->mount = strdup (source->mount);
        yp->server_name = strdup ("");
        yp->server_desc = strdup ("");
        yp->server_genre = strdup ("");
        yp->bitrate = strdup ("");
        yp->server_type = strdup ("");
        yp->cluster_password = strdup ("");
        yp->url = strdup ("");
        yp->current_song = strdup ("");
        yp->audio_info = strdup ("");
        yp->subtype = strdup ("");
        yp->process = do_yp_add;

        url = malloc (len);
        if (url == NULL)
            break;
        config = config_get_config();
        ret = snprintf (url, len, "http://%s:%d%s", config->hostname, config->port, source->mount);
        if (ret >= (signed)len)
        {
            s = realloc (url, ++ret);
            if (s) url = s;
            snprintf (url, ret, "http://%s:%d%s", config->hostname, config->port, source->mount);
        }

        mountproxy = config->mounts;
        while (mountproxy) {
            if (strcmp (mountproxy->mountname, source->mount) == 0) {
                if (mountproxy->cluster_password) {
                   add_yp_info (yp, "cluster_password", 
                       mountproxy->cluster_password, YP_CLUSTER_PASSWORD);
                }
                break;
            }
            mountproxy = mountproxy->next;
        }
        config_release_config();
        yp->listen_url = util_url_escape (url);
        free (url);
        if (yp->listen_url == NULL)
            break;

        /* ice-* is icecast, icy-* is shoutcast */
        add_yp_info (yp, "server_type", source->format->contenttype, YP_SERVER_TYPE);
        if ((s = httpp_getvar(source->parser, "ice-name"))) {
            add_yp_info (yp, "server_name", s, YP_SERVER_NAME);
        }
        if ((s = httpp_getvar(source->parser, "icy-name"))) {
            add_yp_info (yp, "server_name", s, YP_SERVER_NAME);
        }
        if ((s = httpp_getvar(source->parser, "ice-url"))) {
            add_yp_info(yp, "server_url", s, YP_SERVER_URL);
        }
        if ((s = httpp_getvar(source->parser, "icy-url"))) {
            add_yp_info(yp, "server_url", s, YP_SERVER_URL);
        }
        if ((s = httpp_getvar(source->parser, "ice-genre"))) {
            add_yp_info(yp, "genre", s, YP_SERVER_GENRE);
        }
        if ((s = httpp_getvar(source->parser, "icy-genre"))) {
            add_yp_info(yp, "genre", s, YP_SERVER_GENRE);
        }
        if ((s = httpp_getvar(source->parser, "ice-bitrate"))) {
            add_yp_info(yp, "bitrate", s, YP_BITRATE);
        }
        if ((s = httpp_getvar(source->parser, "icy-br"))) {
            add_yp_info(yp, "bitrate", s, YP_BITRATE);
        }
        if ((s = httpp_getvar(source->parser, "ice-description"))) {
            add_yp_info(yp, "server_description", s, YP_SERVER_DESC);
        }
        s = util_dict_urlencode (source->audio_info, '&');
        if (s)
            add_yp_info (yp, "audio_info", s, YP_AUDIO_INFO);
        free(s);
        return yp;
    } while (0);

    yp_destroy_ypdata (yp);
    return NULL;
}


/* Check for changes in the YP servers configured */
static void check_servers ()
{
    struct yp_server *server = (struct yp_server *)active_yps,
                     **server_p = (struct yp_server **)&active_yps;

    while (server)
    {
        if (server->remove)
        {
            struct yp_server *to_go = server;
            DEBUG1 ("YP server \"%s\"removed", server->url);
            *server_p = server->next;
            server = server->next;
            destroy_yp_server (to_go);
            continue;
        }
        server_p = &server->next;
        server = server->next;
    }
    /* add new server entries */
    while (pending_yps)
    {
        avl_node *node;

        server = (struct yp_server *)pending_yps;
        pending_yps = server->next;

        DEBUG1("Add pending yps %s", server->url);
        server->next = (struct yp_server *)active_yps;
        active_yps = server;

        /* new YP server configured, need to populate with existing sources */
        avl_tree_rlock (global.source_tree);
        node = avl_get_first (global.source_tree);
        while (node)
        {
            ypdata_t *yp;

            source_t *source = node->key;
            if ((yp = create_yp_entry (source)) != NULL)
            {
                DEBUG1 ("Adding existing mount %s", source->mount);
                yp->server = server;
                yp->touch_interval = server->touch_interval;
                yp->next = server->mounts;
                server->mounts = yp;
            }
            node = avl_get_next (node);
        }
        avl_tree_unlock (global.source_tree);
    }
}


static void add_pending_yp (struct yp_server *server)
{
    ypdata_t *current, *yp;
    unsigned count = 0;

    if (server->pending_mounts == NULL)
        return;
    current = server->mounts;
    server->mounts = server->pending_mounts;
    server->pending_mounts = NULL;
    yp = server->mounts;
    while (1)
    {
        count++;
        if (yp->next == NULL)
            break;
        yp = yp->next;
    }
    yp->next = current;
    DEBUG2 ("%u YP entries added to %s", count, server->url);
}


static void delete_marked_yp (struct yp_server *server)
{
    ypdata_t *yp = server->mounts, **prev = &server->mounts;

    while (yp)
    {
        if (yp->remove)
        {
            ypdata_t *to_go = yp;
            DEBUG2 ("removed %s from YP server %s", yp->mount, server->url);
            *prev = yp->next;
            yp = yp->next;
            yp_destroy_ypdata (to_go);
            continue;
        }
        prev = &yp->next;
        yp = yp->next;
    }
}


static void *yp_update_thread(void *arg)
{
    INFO0("YP update thread started");

    yp_running = 1;
    while (yp_running)
    {
        struct yp_server *server;

        thread_sleep (200000);

        /* do the YP communication */
        thread_rwlock_rlock (&yp_lock);
        server = (struct yp_server *)active_yps;
        while (server)
        {
            /* DEBUG1 ("trying %s", server->url); */
            yp_process_server (server);
            server = server->next;
        }
        thread_rwlock_unlock (&yp_lock);

        /* update the local YP structure */
        if (yp_update)
        {
            thread_rwlock_wlock (&yp_lock);
            check_servers ();
            server = (struct yp_server *)active_yps;
            while (server)
            {
                /* DEBUG1 ("Checking yps %s", server->url); */
                add_pending_yp (server);
                delete_marked_yp (server);
                server = server->next;
            }
            yp_update = 0;
            thread_rwlock_unlock (&yp_lock);
        }
    }
    thread_rwlock_destroy (&yp_lock);
    thread_mutex_destroy (&yp_pending_lock);
    /* free server and ypdata left */
    while (active_yps)
    {
        struct yp_server *server = (struct yp_server *)active_yps;
        active_yps = server->next;
        destroy_yp_server (server);
    }

    return NULL;
}



static void yp_destroy_ypdata(ypdata_t *ypdata)
{
    if (ypdata) {
        if (ypdata->mount) {
            free (ypdata->mount);
        }
        if (ypdata->url) {
            free (ypdata->url);
        }
        if (ypdata->sid) {
            free(ypdata->sid);
        }
        if (ypdata->server_name) {
            free(ypdata->server_name);
        }
        if (ypdata->server_desc) {
            free(ypdata->server_desc);
        }
        if (ypdata->server_genre) {
            free(ypdata->server_genre);
        }
        if (ypdata->cluster_password) {
            free(ypdata->cluster_password);
        }
        if (ypdata->listen_url) {
            free(ypdata->listen_url);
        }
        if (ypdata->current_song) {
            free(ypdata->current_song);
        }
        if (ypdata->bitrate) {
            free(ypdata->bitrate);
        }
        if (ypdata->server_type) {
            free(ypdata->server_type);
        }
        if (ypdata->audio_info) {
            free(ypdata->audio_info);
        }
        free (ypdata->subtype);
        free (ypdata->error_msg);
        free (ypdata);
    }
}

static void add_yp_info (ypdata_t *yp, char *stat_name, void *info, int type)
{
    char *escaped;

    if (!info)
        return;

    switch (type)
    {
        case YP_SERVER_NAME:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->server_name)
                    free (yp->server_name);
                yp->server_name = escaped;
                stats_event (yp->mount, stat_name, (char *)info);
            }
            break;
        case YP_SERVER_DESC:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->server_desc)
                    free (yp->server_desc);
                yp->server_desc = escaped;
                stats_event(yp->mount, stat_name, (char *)info);
            }
            break;
        case YP_SERVER_GENRE:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->server_genre)
                    free (yp->server_genre);
                yp->server_genre = escaped;
                stats_event (yp->mount, stat_name, (char *)info);
            }
            break;
        case YP_SERVER_URL:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->url)
                    free (yp->url);
                yp->url = escaped;
                stats_event (yp->mount, stat_name, (char *)info);
            }
            break;
        case YP_BITRATE:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->bitrate)
                    free (yp->bitrate);
                yp->bitrate = escaped;
                stats_event (yp->mount, stat_name, (char *)info);
            }
            break;
        case YP_AUDIO_INFO:
            if (yp->audio_info)
                free (yp->audio_info);
            yp->audio_info = strdup (info);
            break;
        case YP_SERVER_TYPE:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->server_type)
                    free (yp->server_type);
                yp->server_type = escaped;
            }
            break;
        case YP_CURRENT_SONG:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->current_song)
                    free (yp->current_song);
                yp->current_song = escaped;
                stats_event (yp->mount, "yp_currently_playing", (char *)info);
            }
            break;
        case YP_CLUSTER_PASSWORD:
            escaped = util_url_escape(info);
            if (escaped)
            {
                if (yp->cluster_password)
                    free (yp->cluster_password);
                yp->cluster_password = escaped;
            }
            break;
        case YP_SUBTYPE:
            escaped = util_url_escape(info);
            if (escaped)
            {
                free (yp->subtype);
                yp->subtype = escaped;
            }
            break;
    }
}


/* Add YP entries to active servers */
void yp_add (source_t *source)
{
    struct yp_server *server;

    /* make sure YP thread is not modifying the lists */
    thread_rwlock_rlock (&yp_lock);

    /* make sure we don't race against another yp_add */
    thread_mutex_lock (&yp_pending_lock);
    server = (struct yp_server *)active_yps;
    while (server)
    {
        ypdata_t *yp;
        /* add new ypdata to each servers pending yp */
        if ((yp = create_yp_entry (source)) != NULL)
        {
            DEBUG2 ("Adding %s to %s", source->mount, server->url);
            yp->server = server;
            yp->touch_interval = server->touch_interval;
            yp->next = server->pending_mounts;
            server->pending_mounts = yp;
            yp_update = 1;
        }
        server = server->next;
    }
    thread_mutex_unlock (&yp_pending_lock);
    thread_rwlock_unlock (&yp_lock);
    /* DEBUG1 ("Added %s to YP ", source->mount); */
}



/* Mark an existing entry in the YP list as to be marked for deletion */
void yp_remove (const char *mount)
{
    struct yp_server *server = (struct yp_server *)active_yps;

    thread_rwlock_rlock (&yp_lock);
    while (server)
    {
        ypdata_t *yp = find_yp_mount (server->mounts, mount);
        if (yp)
        {
            DEBUG2 ("release %s on YP %s", mount, server->url);
            yp->release = 1;
            yp->next_update = 0;
        }
        server = server->next;
    }
    thread_rwlock_unlock (&yp_lock);
}


/* This is similar to yp_remove, but we force a touch
 * attempt */
void yp_touch (const char *mount)
{
    struct yp_server *server = (struct yp_server *)active_yps;
    time_t trigger;
    ypdata_t *search_list = NULL;

    thread_rwlock_rlock (&yp_lock);
    /* do update in 3 secs, give stats chance to update */
    trigger = time(NULL) + 3;
    if (server)
        search_list = server->mounts;

    while (server)
    {
        ypdata_t *yp = find_yp_mount (search_list, mount);
        if (yp)
        {
            /* we may of found old entries not purged yet, so skip them */
            if (yp->release != 0 || yp->remove != 0)
            {
                search_list = yp->next;
                continue;
            }
            /* only force if touch */
            if (yp->process == do_yp_touch)
                yp->next_update = trigger;
        }
        server = server->next;
        if (server)
            search_list = server->mounts;
    }
    thread_rwlock_unlock (&yp_lock);
}


void yp_shutdown ()
{
    yp_running = 0;
    yp_update = 1;
    if (yp_thread)
        thread_join (yp_thread);
    curl_global_cleanup();
    INFO0 ("YP thread down");
}

