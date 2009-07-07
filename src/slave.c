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
/* slave.c
 * by Ciaran Anscomb <ciaran.anscomb@6809.org.uk>
 *
 * Periodically requests a list of streams from a master server
 * and creates source threads for any it doesn't already have.
 * */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#endif
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif
#ifdef HAVE_GETRLIMIT
#include <sys/resource.h>
#endif

#include "compat.h"

#include "timing/timing.h"
#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "httpp/httpp.h"

#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "source.h"
#include "format.h"
#include "event.h"

#define CATMODULE "slave"

typedef struct _redirect_host
{
    struct _redirect_host *next;
    time_t next_update;
    char *server;
    int port;
} redirect_host;


static void _slave_thread(void);
static void redirector_add (const char *server, int port, int interval);
static redirect_host *find_slave_host (const char *server, int port);
static void redirector_clearall (void);
static int  relay_startup (client_t *client);
static int  relay_read (client_t *client);

int slave_running = 0;
int worker_count;
int relays_connecting;

static volatile int update_settings = 0;
static volatile int update_all_mounts = 0;
static volatile int restart_connection_thread = 0;
static time_t streamlist_check = 0;
static rwlock_t slaves_lock;
static spin_t relay_start_lock;

redirect_host *redirectors;
worker_t *workers;
rwlock_t workers_lock;

struct _client_functions relay_client_ops =
{
    relay_read,
    client_destroy
};

struct _client_functions relay_startup_ops =
{
    relay_startup,
    client_destroy
};


relay_server *relay_free (relay_server *relay)
{
    relay_server *next = relay->next;

    DEBUG1("freeing relay %s", relay->localmount);
    if (relay->source)
       source_free_source (relay->source);
    while (relay->masters)
    {
        relay_server_master *master = relay->masters;
        relay->masters = master->next;
        xmlFree (master->ip);
        xmlFree (master->mount);
        xmlFree (master->bind);
        free (master);
    }
    xmlFree (relay->localmount);
    if (relay->username) xmlFree (relay->username);
    if (relay->password) xmlFree (relay->password);
    free (relay);
    return next;
}


relay_server *relay_copy (relay_server *r)
{
    relay_server *copy = calloc (1, sizeof (relay_server));

    if (copy)
    {
        relay_server_master *from = r->masters, **insert = &copy->masters;

        while (from)
        {
            relay_server_master *to = calloc (1, sizeof (relay_server_master));
            to->ip = (char *)xmlCharStrdup (from->ip);
            to->mount = (char *)xmlCharStrdup (from->mount);
            if (from->bind)
                to->bind = (char *)xmlCharStrdup (from->bind);
            to->port = from->port;
            *insert = to;
            from = from->next;
            insert = &to->next;
        }

        copy->localmount = (char *)xmlStrdup (XMLSTR(r->localmount));
        if (r->username)
            copy->username = (char *)xmlStrdup (XMLSTR(r->username));
        if (r->password)
            copy->password = (char *)xmlStrdup (XMLSTR(r->password));
        copy->mp3metadata = r->mp3metadata;
        copy->on_demand = r->on_demand;
        copy->interval = r->interval;
        copy->enable = r->enable;
        copy->source = r->source;
        r->source = NULL;
    }
    return copy;
}


/* force a recheck of the relays. This will recheck the master server if
 * this is a slave and rebuild all mountpoints in the stats tree
 */
void slave_update_all_mounts (void)
{
    update_all_mounts = 1;
    update_settings = 1;
}


/* called on reload, so drop all redirection and trigger a relay checkup and
 * rebuild all stat mountpoints
 */
void slave_restart (void)
{
    restart_connection_thread = 1;
    redirector_clearall();
    slave_update_all_mounts ();
}

/* Request slave thread to check the relay list for changes and to
 * update the stats for the current streams.
 */
void slave_rebuild_mounts (void)
{
    update_settings = 1;
}


void slave_initialize(void)
{
    if (slave_running)
        return;

    thread_rwlock_create (&slaves_lock);
    slave_running = 1;
    streamlist_check = 0;
    update_settings = 0;
    update_all_mounts = 0;
    restart_connection_thread = 0;
    redirectors = NULL;
    workers = NULL;
    worker_count = 0;
    relays_connecting = 0;
    thread_spin_create (&relay_start_lock);
    thread_rwlock_create (&workers_lock);
#ifndef HAVE_CURL
    ERROR0 ("streamlist request disabled, rebuild with libcurl if required");
#endif
    _slave_thread ();
    slave_running = 0;
}


void slave_shutdown(void)
{
    workers_adjust (0);
    thread_rwlock_destroy (&slaves_lock);
    thread_rwlock_destroy (&workers_lock);
    thread_spin_destroy (&relay_start_lock);
    slave_running = 0;
}


int redirect_client (const char *mountpoint, client_t *client)
{
    int ret = 0, which;
    redirect_host *checking, **trail;

    thread_rwlock_rlock (&slaves_lock);
    /* select slave entry */
    if (global.redirect_count == 0)
    {
        thread_rwlock_unlock (&slaves_lock);
        return 0;
    }
    which=(int) (((float)global.redirect_count)*rand()/(RAND_MAX+1.0)) + 1;
    checking = redirectors;
    trail = &redirectors;

    DEBUG2 ("random selection %d (out of %d)", which, global.redirect_count);
    while (checking)
    {
        DEBUG2 ("...%s:%d", checking->server, checking->port);
        if (checking->next_update && checking->next_update+10 < time(NULL))
        {
            /* no streamist request, expire slave for now */
            *trail = checking->next;
            global.redirect_count--;
            /* free slave details */
            INFO2 ("dropping redirector for %s:%d", checking->server, checking->port);
            free (checking->server);
            free (checking);
            checking = *trail;
            if (which > 0)
                which--; /* we are 1 less now */
            continue;
        }
        if (--which == 0)
        {
            char *location;
            /* add enough for "http://" the port ':' and nul */
            int len = strlen (mountpoint) + strlen (checking->server) + 13;

            INFO2 ("redirecting listener to slave server "
                    "at %s:%d", checking->server, checking->port);
            location = malloc (len);
            snprintf (location, len, "http://%s:%d%s", checking->server,
                    checking->port, mountpoint);
            client_send_302 (client, location);
            free (location);
            ret = 1;
        }
        trail = &checking->next;
        checking = checking->next;
    }
    thread_rwlock_unlock (&slaves_lock);
    return ret;
}


/* Actually open the connection and do some http parsing, handle any 302
 * responses within here.
 */
static int open_relay_connection (client_t *client, relay_server *relay, relay_server_master *master)
{
    int redirects = 0;
    char *server_id = NULL;
    ice_config_t *config;
    http_parser_t *parser = NULL;
    connection_t *con=NULL;
    char *server = strdup (master->ip);
    char *mount = strdup (master->mount);
    int port = master->port;
    char *bind = NULL;
    char *auth_header;
    char header[4096];

    config = config_get_config ();
    server_id = strdup (config->server_id);
    config_release_config ();

    if (relay->username && relay->password)
    {
        char *esc_authorisation;
        unsigned len = strlen(relay->username) + strlen(relay->password) + 2;

        auth_header = malloc (len);
        snprintf (auth_header, len, "%s:%s", relay->username, relay->password);
        esc_authorisation = util_base64_encode(auth_header);
        free(auth_header);
        len = strlen (esc_authorisation) + 24;
        auth_header = malloc (len);
        snprintf (auth_header, len,
                "Authorization: Basic %s\r\n", esc_authorisation);
        free(esc_authorisation);
    }
    else
        auth_header = strdup ("");

    while (redirects < 10)
    {
        sock_t streamsock;

        /* policy decision, we assume a source bind even after redirect, possible option */
        if (master->bind)
            bind = master->bind;

        if (bind)
            INFO4 ("connecting to %s:%d for %s, bound to %s", server, port, relay->localmount, bind);
        else
            INFO3 ("connecting to %s:%d for %s", server, port, relay->localmount);

        streamsock = sock_connect_wto_bind (server, port, bind, 10);
        if (streamsock == SOCK_ERROR)
        {
            WARN3 ("Failed to connect to %s:%d for %s", server, port, relay->localmount);
            break;
        }
        con = connection_create (streamsock, SOCK_ERROR, strdup (server));

        /* At this point we may not know if we are relaying an mp3 or vorbis
         * stream, but only send the icy-metadata header if the relay details
         * state so (the typical case).  It's harmless in the vorbis case. If
         * we don't send in this header then relay will not have mp3 metadata.
         */
        sock_write(streamsock, "GET %s HTTP/1.0\r\n"
                "User-Agent: %s\r\n"
                "%s"
                "%s"
                "\r\n",
                mount,
                server_id,
                relay->mp3metadata ? "Icy-MetaData: 1\r\n" : "",
                auth_header);
        memset (header, 0, sizeof(header));
        if (util_read_header (con->sock, header, 4096, READ_ENTIRE_HEADER) == 0)
        {
            ERROR4 ("Header read failed for %s (%s:%d%s)", relay->localmount, server, port, mount);
            break;
        }
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        if (! httpp_parse_response (parser, header, strlen(header), relay->localmount))
        {
            ERROR4("Error parsing relay request for %s (%s:%d%s)", relay->localmount,
                    server, port, mount);
            break;
        }
        if (strcmp (httpp_getvar (parser, HTTPP_VAR_ERROR_CODE), "302") == 0)
        {
            /* better retry the connection again but with different details */
            const char *uri, *mountpoint;
            int len;

            uri = httpp_getvar (parser, "location");
            INFO1 ("redirect received %s", uri);
            if (strncmp (uri, "http://", 7) != 0)
                break;
            uri += 7;
            mountpoint = strchr (uri, '/');
            free (mount);
            if (mountpoint)
                mount = strdup (mountpoint);
            else
                mount = strdup ("/");

            len = strcspn (uri, ":/");
            port = 80;
            if (uri [len] == ':')
                port = atoi (uri+len+1);
            free (server);
            server = calloc (1, len+1);
            strncpy (server, uri, len);
            connection_close (con);
            httpp_destroy (parser);
            con = NULL;
            parser = NULL;
        }
        else
        {
            if (httpp_getvar (parser, HTTPP_VAR_ERROR_MESSAGE))
            {
                ERROR2("Error from relay request: %s (%s)", relay->localmount,
                        httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
                break;
            }
            sock_set_blocking (streamsock, 0);
            client->con = con;
            client->parser = parser;
            client_set_queue (client, NULL);
            free (server);
            free (mount);
            free (server_id);
            free (auth_header);

            return 0;
        }
        redirects++;
    }
    /* failed, better clean up */
    free (server);
    free (mount);
    free (server_id);
    free (auth_header);
    if (con)
        connection_close (con);
    if (parser)
        httpp_destroy (parser);
    return -1;
}


/* This does the actual connection for a relay. A thread is
 * started off if a connection can be acquired
 */
static void *start_relay_stream (void *arg)
{
    relay_server *relay = arg;
    source_t *src = relay->source;
    relay_server_master *master = relay->masters;
    client_t *client = src->client;
    mount_proxy *mountinfo;
    ice_config_t *config;

    INFO1("Starting relayed source at mountpoint \"%s\"", relay->localmount);
    thread_rwlock_rlock (&global.shutdown_lock);
    thread_mutex_lock (&src->lock);
    do
    {
        int ret;

        thread_mutex_unlock (&src->lock);
        ret = open_relay_connection (client, relay, master);
        thread_mutex_lock (&src->lock);

        if (ret < 0)
            continue;
        src->parser = client->parser;

        if (connection_complete_source (src, 0) < 0)
        {
            INFO0("Failed to complete source initialisation");
            continue;
        }
        stats_event_inc (NULL, "source_relay_connections");

        source_init (src);
        relay->running = 1;
        client->ops = &relay_client_ops;
        client->flags |= CLIENT_ACTIVE;

        thread_spin_lock (&relay_start_lock);
        relays_connecting--;
        thread_spin_unlock (&relay_start_lock);
        thread_cond_signal (&client->worker->cond);

        return NULL;
    } while ((master = master->next));

    /* failed to start, better clean up and reset */
    if (relay->on_demand)
        src->flags &= ~SOURCE_ON_DEMAND;  // disable for a short time
    else
        yp_remove (relay->localmount);

    if (client->con)
        connection_close (client->con);
    client->con = NULL;
    if (client->parser)
        httpp_destroy (client->parser);
    client->parser = NULL;

    config = config_get_config();
    mountinfo = config_find_mount (config, src->mount);
    if (mountinfo && mountinfo->fallback_mount && strcmp (src->mount, mountinfo->fallback_mount) != 0)
    {
        int left_on = 0;
        client_t *leave_on = NULL, *check_clients, *last = NULL;
        INFO1 ("failed relay, fallback to %s", mountinfo->fallback_mount);
        check_clients = src->client_list;
        src->client_list = NULL;
        src->listeners = 0;
        while (check_clients)
        {
            client_t *to_move = check_clients;
            check_clients = to_move->next;
            if (to_move->flags & CLIENT_IS_SLAVE)
            {
                if (leave_on == NULL) last = to_move;
                to_move->next = leave_on;
                leave_on = to_move;
                left_on++;
            }
            else
            {
                fbinfo f;
                f.mount = mountinfo->fallback_mount;
                f.flags = FS_NORMAL;
                f.fallback = NULL;
                if (src->format)
                    f.limit = rate_avg (src->format->in_bitrate);
                else
                    f.limit = src->limit_rate;
                thread_mutex_unlock (&src->lock);
                move_listener (to_move, &f);
                thread_mutex_lock (&src->lock);
            }
        }
        /* it is possible that listeners could of been moved here by now */
        src->listeners += left_on;
        if (last)
        {
            last->next = src->client_list;
            src->client_list = leave_on;
        }
    }
    config_release_config();

    INFO2 ("listener count still on %s is %d", src->mount, src->listeners);
    source_clear_source (src);
    relay->start = client->worker->current_time.tv_sec + relay->interval;
    client->schedule_ms = timing_get_time() + 1000;
    client->flags |= CLIENT_ACTIVE;

    thread_spin_lock (&relay_start_lock);
    relays_connecting--;
    thread_spin_unlock (&relay_start_lock);
    thread_cond_signal (&client->worker->cond);
    thread_rwlock_unlock (&global.shutdown_lock);
    return NULL;
}


static void check_relay_stream (relay_server *relay)
{
    source_t *source = relay->source;
    if (source && source->client)
        return;
    if (source == NULL)
    {
        if (relay->localmount[0] != '/')
        {
            WARN1 ("relay mountpoint \"%s\" does not start with /, skipping", relay->localmount);
            return;
        }
        /* new relay, reserve the name */
        source = source_reserve (relay->localmount);
        if (source == NULL)
        {
            if (relay->start == 0)
            {
                WARN1 ("new relay but source \"%s\" already exists", relay->localmount);
                relay->start = 1;
            }
            return;
        }
        relay->source = source;
        INFO1("Adding new relay at mountpoint \"%s\"", relay->localmount);
        stats_event_hidden (source->mount, "listener_connections", "0", STATS_COUNTERS);
    }
    if (source->client == NULL)
    {
        client_t *client;
        global_lock();
        client = client_create (NULL, NULL);
        global_unlock();
        source->client = client;
        client->shared_data = relay;
        client->ops = &relay_startup_ops;
        if (relay->on_demand)
        {
            ice_config_t *config = config_get_config();
            mount_proxy *mountinfo = config_find_mount (config, source->mount);
            thread_mutex_lock (&source->lock);
            source->flags |= SOURCE_ON_DEMAND;
            source_update_settings (config, source, mountinfo);
            thread_mutex_unlock (&source->lock);
            config_release_config();
        }
        client->flags |= CLIENT_ACTIVE;
        DEBUG1 ("adding relay client for %s", relay->localmount);
        client_add_worker (client);
    }
}


/* compare the 2 relays to see if there are any changes, return 1 if
 * the relay needs to be restarted, 0 otherwise
 */
static int relay_has_changed (relay_server *new, relay_server *old)
{
    do
    {
        relay_server_master *oldmaster = old->masters, *newmaster = new->masters;

        while (oldmaster && newmaster)
        {
            if (strcmp (newmaster->mount, oldmaster->mount) != 0)
                break;
            if (strcmp (newmaster->ip, oldmaster->ip) != 0)
                break;
            if (newmaster->port != oldmaster->port)
                break;
            oldmaster = oldmaster->next;
            newmaster = newmaster->next;
        }
        if (oldmaster || newmaster)
            break;
        if (new->mp3metadata != old->mp3metadata)
            break;
        if (new->on_demand != old->on_demand)
            old->on_demand = new->on_demand;
        return 0;
    } while (0);
    new->source = old->source;
    return 1;
}


/* go through updated looking for relays that are different configured. The
 * returned list contains relays that should be kept running, current contains
 * the list of relays to shutdown
 */
static relay_server *
update_relay_set (relay_server **current, relay_server *updated)
{
    relay_server *relay = updated;
    relay_server *existing_relay, **existing_p;
    relay_server *new_list = NULL;

    while (relay)
    {
        existing_relay = *current;
        existing_p = current;

        while (existing_relay)
        {
            /* break out if keeping relay */
            if (strcmp (relay->localmount, existing_relay->localmount) == 0)
                if (relay_has_changed (relay, existing_relay) == 0)
                    break;
            existing_p = &existing_relay->next;
            existing_relay = existing_relay->next;
        }
        if (existing_relay == NULL)
        {
            /* new one, copy and insert */
            existing_relay = relay_copy (relay);
        }
        else
        {
            *existing_p = existing_relay->next;
        }
        existing_relay->next = new_list;
        new_list = existing_relay;
        relay = relay->next;
    }
    return new_list;
}


/* update the relay_list with entries from new_relay_list. Any new relays
 * are added to the list, and any not listed in the provided new_relay_list
 * are separated and returned in a separate list
 */
static relay_server *
update_relays (relay_server **relay_list, relay_server *new_relay_list)
{
    relay_server *active_relays, *cleanup_relays;

    active_relays = update_relay_set (relay_list, new_relay_list);

    cleanup_relays = *relay_list;
    /* re-assign new set */
    *relay_list = active_relays;

    return cleanup_relays;
}


static void relay_check_streams (relay_server *to_start,
        relay_server *to_free, int skip_timer)
{
    relay_server *relay;

    while (to_free)
    {
        relay_server *release = to_free;
        to_free = release->next;

        if (release->source && release->source->client)
        {
            if (release->running)
            {
                /* relay has been removed from xml, shut down active relay */
                DEBUG1 ("source shutdown request on \"%s\"", release->localmount);
                release->running = 0;
                release->source->flags &= ~SOURCE_RUNNING;
            }
            else
                stats_event (release->localmount, NULL, NULL);
            continue;
        }
        relay_free (release);
    }

    relay = to_start;
    while (relay)
    {
        if (skip_timer)
            relay->start = 0;
        check_relay_stream (relay);
        relay = relay->next;
    }
}


#ifdef HAVE_CURL
struct master_conn_details
{
    char *server;
    int port;
    int ssl_port;
    int send_auth;
    int on_demand;
    int previous;
    int ok;
    int max_interval;
    char *buffer;
    char *username;
    char *password;
    char *bind;
    char *server_id;
    char *args;
    relay_server *new_relays;
};


/* process a single HTTP header from streamlist response */
static size_t streamlist_header (void *ptr, size_t size, size_t nmemb, void *stream)
{
    size_t passed_len = size*nmemb;
    char *eol = memchr (ptr, '\r', passed_len);
    struct master_conn_details *master = stream;

    /* drop EOL chars if any */
    if (eol)
        *eol = '\0';
    else
    {
        eol = memchr (ptr, '\n', passed_len);
        if (eol)
            *eol = '\0';
        else
            return -1;
    }
    if (strncmp (ptr, "HTTP", 4) == 0)
    {
        int respcode;
        if (sscanf (ptr, "HTTP%*s %d OK", &respcode) == 1 && respcode == 200)
        {
            master->ok = 1;
        }
        else
        {
            WARN1 ("Failed response from master \"%s\"", (char*)ptr);
            return -1;
        }
    }
    return passed_len;
}


/* process mountpoint list from master server. This may be called multiple
 * times so watch for the last line in this block as it may be incomplete
 */
static size_t streamlist_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
    struct master_conn_details *master = stream;
    size_t passed_len = size*nmemb;
    size_t len = passed_len + master->previous + 1;
    char *buffer, *buf;

    /* append newly read data to the end of any previous unprocess data */
    buffer = realloc (master->buffer, len);
    memcpy (buffer + master->previous, ptr, passed_len);
    buffer [len-1] = '\0';

    buf = buffer;
    while (len)
    {
        int offset;
        char *eol = strchr (buf, '\n');
        if (eol)
        {
            offset = (eol - buf) + 1;
            *eol = '\0';
            eol = strchr (buf, '\r');
            if (eol) *eol = '\0';
        }
        else
        {
            /* incomplete line, the rest may be in the next read */
            unsigned rest = strlen (buf);
            memmove (buffer, buf, rest);
            master->previous = rest;
            break;
        }

        DEBUG1 ("read from master \"%s\"", buf);
        if (strlen (buf))
        {
            relay_server *r = calloc (1, sizeof (relay_server));
            relay_server_master *m = calloc (1, sizeof (relay_server_master));

            m->ip = (char *)xmlStrdup (XMLSTR(master->server));
            m->port = master->port;
            if (master->bind)
                m->bind = (char *)xmlStrdup (XMLSTR(master->bind));
            m->mount = (char *)xmlStrdup (XMLSTR(buf));
            r->masters = m;
            if (strncmp (buf, "/admin/streams?mount=/", 22) == 0)
                r->localmount = (char *)xmlStrdup (XMLSTR(buf+21));
            else
                r->localmount = (char *)xmlStrdup (XMLSTR(buf));
            r->mp3metadata = 1;
            r->on_demand = master->on_demand;
            r->interval = master->max_interval;
            r->enable = 1;
            if (master->send_auth)
            {
                r->username = (char *)xmlStrdup (XMLSTR(master->username));
                r->password = (char *)xmlStrdup (XMLSTR(master->password));
            }
            r->next = master->new_relays;
            master->new_relays = r;
        }
        buf += offset;
        len -= offset;
    }
    master->buffer = buffer;
    return passed_len;
}


/* retrieve streamlist from master server. The streamlist can be retrieved
 * from an SSL port if curl is capable and the config is aware of the port
 * to use
 */
static void *streamlist_thread (void *arg)
{
    struct master_conn_details *master = arg;
    CURL *handle;
    const char *protocol = "http";
    int port = master->port;
    char error [CURL_ERROR_SIZE];
    char url [1024], auth [100];

    DEBUG0 ("checking master stream list");
    if (master->ssl_port)
    {
        protocol = "https";
        port = master->ssl_port;
    }
    snprintf (auth, sizeof (auth), "%s:%s", master->username, master->password);
    snprintf (url, sizeof (url), "%s://%s:%d/admin/streams%s",
            protocol, master->server, port, master->args);
    handle = curl_easy_init ();
    curl_easy_setopt (handle, CURLOPT_USERAGENT, master->server_id);
    curl_easy_setopt (handle, CURLOPT_URL, url);
    curl_easy_setopt (handle, CURLOPT_HEADERFUNCTION, streamlist_header);
    curl_easy_setopt (handle, CURLOPT_HEADERDATA, master);
    curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, streamlist_data);
    curl_easy_setopt (handle, CURLOPT_WRITEDATA, master);
    curl_easy_setopt (handle, CURLOPT_USERPWD, auth);
    curl_easy_setopt (handle, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt (handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt (handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt (handle, CURLOPT_TIMEOUT, 10L);
    if (master->bind)
        curl_easy_setopt (handle, CURLOPT_INTERFACE, master->bind);

    if (curl_easy_perform (handle) != 0)
    {
        /* fall back to traditional request */
        snprintf (url, sizeof (url), "%s://%s:%d/admin/streamlist.txt%s",
                protocol, master->server, port, master->args);
        curl_easy_setopt (handle, CURLOPT_URL, url);
        if (curl_easy_perform (handle) != 0)
            WARN2 ("Failed URL access \"%s\" (%s)", url, error);
    }
    if (master->ok)
    {
        /* process retrieved relays */
        relay_server *cleanup_relays;

        thread_mutex_lock (&(config_locks()->relay_lock));
        cleanup_relays = update_relays (&global.master_relays, master->new_relays);

        relay_check_streams (global.master_relays, cleanup_relays, 0);
        relay_check_streams (NULL, master->new_relays, 0);

        thread_mutex_unlock (&(config_locks()->relay_lock));
    }

    curl_easy_cleanup (handle);
    free (master->server);
    free (master->username);
    free (master->password);
    free (master->buffer);
    free (master->server_id);
    free (master->args);
    free (master);
    return NULL;
}
#endif


static void update_from_master (ice_config_t *config)
{
#ifdef HAVE_CURL
    struct master_conn_details *details;

    if (config->master_password == NULL || config->master_server == NULL ||
            config->master_server_port == 0)
        return;
    details = calloc (1, sizeof (*details));
    details->server = strdup (config->master_server);
    details->port = config->master_server_port; 
    details->ssl_port = config->master_ssl_port; 
    details->username = strdup (config->master_username);
    details->password = strdup (config->master_password);
    details->send_auth = config->master_relay_auth;
    details->bind = (config->master_bind) ? strdup (config->master_bind) : NULL;
    details->on_demand = config->on_demand;
    details->server_id = strdup (config->server_id);
    details->max_interval = config->master_update_interval;
    if (config->master_redirect)
    {
        details->args = malloc (4096);
        snprintf (details->args, 4096, "?rserver=%s&rport=%d&interval=%d",
                config->hostname, config->port, config->master_update_interval);
    }
    else
        details->args = strdup ("");

    thread_create ("streamlist", streamlist_thread, details, THREAD_DETACHED);
#endif
}


static void update_master_as_slave (ice_config_t *config)
{
    redirect_host *redirect;

    if (config->master_server == NULL || config->master_redirect == 0 || config->max_redirects == 0)
         return;

    thread_rwlock_wlock (&slaves_lock);
    redirect = find_slave_host (config->master_server, config->master_server_port);
    if (redirect == NULL)
    {
        INFO2 ("adding master %s:%d", config->master_server, config->master_server_port);
        redirector_add (config->master_server, config->master_server_port, 0);
    }
    else
        redirect->next_update += config->master_update_interval;
    thread_rwlock_unlock (&slaves_lock);
}

static void slave_startup (void)
{
    ice_config_t *config;

#ifdef HAVE_GETRLIMIT
    struct rlimit rlimit;
    if (getrlimit (RLIMIT_NOFILE, &rlimit) == 0)
    {
        INFO2 ("max file descriptors %ld (hard limit %ld)",
                (long)rlimit.rlim_cur, (long)rlimit.rlim_max);
    }
#endif

    update_settings = 0;
    update_all_mounts = 0;

    config = config_get_config();
    update_master_as_slave (config);
    stats_global (config);
    workers_adjust (config->workers_count);
    config_release_config();

    source_recheck_mounts (1);
    connection_thread_startup();
}

static void _slave_thread(void)
{
    slave_startup();

    while (1)
    {
        relay_server *cleanup_relays = NULL;
        int skip_timer = 0;
        struct timespec current;

        thread_get_timespec (&current);
        /* re-read xml file if requested */
        if (global . schedule_config_reread)
        {
            event_config_read ();
            global . schedule_config_reread = 0;
        }

        global_add_bitrates (global.out_bitrate, 0L, THREAD_TIME_MS(&current));

        if (global.running != ICE_RUNNING)
            break;

        if (streamlist_check <= current.tv_sec)
        {
            ice_config_t *config;

            if (streamlist_check == 0)
                skip_timer = 1;

            thread_mutex_lock (&(config_locks()->relay_lock));
            config = config_get_config();

            streamlist_check = current.tv_sec + config->master_update_interval;
            update_master_as_slave (config);

            update_from_master (config);

            cleanup_relays = update_relays (&global.relays, config->relay);

            config_release_config();
        }
        else
            thread_mutex_lock (&(config_locks()->relay_lock));

        relay_check_streams (global.relays, cleanup_relays, skip_timer);
        relay_check_streams (global.master_relays, NULL, skip_timer);
        thread_mutex_unlock (&(config_locks()->relay_lock));

        if (update_settings)
        {
            source_recheck_mounts (update_all_mounts);
            update_settings = 0;
            update_all_mounts = 0;
            if (restart_connection_thread)
            {
                connection_thread_startup();
                restart_connection_thread = 0;
            }
        }
        /* trigger any YP processing */
        yp_thread_startup();
        stats_global_calc();
        thread_sleep (1000000);
    }
    connection_thread_shutdown();
    INFO0 ("shutting down current relays");
    relay_check_streams (NULL, global.relays, 0);
    relay_check_streams (NULL, global.master_relays, 0);
    global.relays = NULL;
    global.master_relays = NULL;
    redirector_clearall();
    /* send any removals to the YP servers */
    yp_thread_startup();

    thread_rwlock_wlock (&global.shutdown_lock);
    thread_rwlock_unlock (&global.shutdown_lock);
    INFO0 ("Slave thread shutdown complete");
}


relay_server *slave_find_relay (relay_server *relays, const char *mount)
{
    while (relays)
    {
        if (strcmp (relays->localmount, mount) == 0)
            break;
        relays = relays->next;
    }
    return relays;
}


/* drop all redirection details.
 */
static void redirector_clearall (void)
{
    thread_rwlock_wlock (&slaves_lock);
    while (redirectors)
    {
        redirect_host *current = redirectors;
        redirectors = current->next;
        INFO2 ("removing %s:%d", current->server, current->port);
        free (current->server);
        free (current);
    }
    global.redirect_count = 0;
    thread_rwlock_unlock (&slaves_lock);
}

/* Add new redirectors or update any existing ones
 */
void redirector_update (client_t *client)
{
    redirect_host *redirect;
    const char *rserver = httpp_get_query_param (client->parser, "rserver");
    const char *value;
    int rport = 0, interval = 0;

    if (rserver==NULL) return;
    value = httpp_get_query_param (client->parser, "rport");
    if (value == NULL) return;
    rport = atoi (value);
    if (rport <= 0) return;
    value = httpp_get_query_param (client->parser, "interval");
    if (value == NULL) return;
    interval = atoi (value);
    if (interval < 5) return;

    thread_rwlock_wlock (&slaves_lock);
    redirect = find_slave_host (rserver, rport);
    if (redirect == NULL)
    {
        redirector_add (rserver, rport, interval);
    }
    else
    {
        DEBUG2 ("touch update on %s:%d", redirect->server, redirect->port);
        redirect->next_update = time(NULL) + interval;
    }
    thread_rwlock_unlock (&slaves_lock);
}



/* search list of redirectors for a matching entry, lock must be held before
 * invoking this function
 */
static redirect_host *find_slave_host (const char *server, int port)
{
    redirect_host *redirect = redirectors;
    while (redirect)
    {
        if (strcmp (redirect->server, server) == 0 && redirect->port == port)
            break;
        redirect = redirect->next;
    }
    return redirect;
}


static void redirector_add (const char *server, int port, int interval)
{
    ice_config_t *config = config_get_config();
    unsigned int allowed = config->max_redirects;
    redirect_host *redirect;

    config_release_config();

    if (global.redirect_count >= allowed)
    {
        INFO2 ("redirect to slave limit reached (%d, %d)", global.redirect_count, allowed);
        return;
    }
    redirect = calloc (1, sizeof (redirect_host));
    if (redirect == NULL)
        abort();
    redirect->server = strdup (server);
    redirect->port = port;
    if (interval == 0)
        redirect->next_update = (time_t)0;
    else
        redirect->next_update = time(NULL) + interval;
    redirect->next = redirectors;
    redirectors = redirect;
    global.redirect_count++;
    INFO3 ("slave (%d) at %s:%d added", global.redirect_count,
            redirect->server, redirect->port);
}


static int relay_read (client_t *client)
{
    relay_server *relay = client->shared_data;
    source_t *source = relay->source;

    thread_mutex_lock (&source->lock);
    if (source_running (source))
    {
        if (relay->enable == 0)
            source->flags &= ~SOURCE_RUNNING;
        if (relay->on_demand && source->listeners == 0)
            source->flags &= ~SOURCE_RUNNING;
        source_read (source);
        return 0;
    }
    DEBUG2 ("counts are %d and %d", source->termination_count, source->listeners);
    if (relay->running)
    {
        client_t *listener;

        INFO1 ("standing by to restart relay on %s", relay->localmount);
        source_shutdown (source, 0);

        /* the relay itself closed, but may want restarting */
        listener = source->client_list;
        while (listener)
        {
            client_set_queue (listener, NULL);
            listener = listener->next;
        }
        client->ops = &relay_startup_ops;
        relay->running = 0;
        global_reduce_bitrate_sampling (global.out_bitrate);
        thread_mutex_unlock (&source->lock);
        thread_rwlock_unlock (&global.shutdown_lock);
        return 0;
    }
    if ((source->flags & SOURCE_TERMINATING) == 0)
    {
        source_shutdown (source, 1);
        source->flags |= SOURCE_TERMINATING;
    }
    if (source->termination_count)
    {
        thread_mutex_unlock (&source->lock);
        return 0;
    }
    INFO1 ("shutting down relay %s", relay->localmount);
    thread_mutex_unlock (&source->lock);
    stats_event (relay->localmount, NULL, NULL);
    global_reduce_bitrate_sampling (global.out_bitrate);
    thread_rwlock_unlock (&global.shutdown_lock);
    relay_free (relay);
    return -1;
}


static int relay_startup (client_t *client)
{
    relay_server *relay = client->shared_data;

    if (global.running != ICE_RUNNING)
        return -1;
    if (relay->enable == 0 || relay->start > client->worker->current_time.tv_sec)
    {
        client->schedule_ms = client->worker->time_ms + 1000;
        return 0;
    }

    if (relay->on_demand)
    {
        source_t *src = relay->source;
        src->flags |= SOURCE_ON_DEMAND;
        if (src->listeners == 0)
        {
            client->schedule_ms = client->worker->time_ms + 1000;
            return 0;
        }
        if (client->worker->current_time.tv_sec % 10 == 0)
        {
            mount_proxy * mountinfo = config_find_mount (config_get_config(), src->mount);
            if (mountinfo && mountinfo->fallback_mount)
                source_set_override (mountinfo->fallback_mount, src->mount);
            config_release_config();
        }
        DEBUG1 ("Detected listeners on relay %s", relay->localmount);
    }

    /* limit the number of relays starting up at the same time */
    thread_spin_lock (&relay_start_lock);
    if (relays_connecting > 3)
    {
        thread_spin_unlock (&relay_start_lock);
        client->schedule_ms = client->worker->time_ms + 2000;
        return 0;
    }
    relays_connecting++;
    thread_spin_unlock (&relay_start_lock);

    client->flags &= ~CLIENT_ACTIVE;
    thread_create ("Relay Thread", start_relay_stream, relay, THREAD_DETACHED);
    return 0;
}

