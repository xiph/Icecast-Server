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
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#include "compat.h"

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

static void *_slave_thread(void *arg);
static void redirector_add (const char *server, int port, int interval);
static redirect_host *find_slave_host (const char *server, int port);
static void redirector_clearall (void);

static thread_type *_slave_thread_id;
static int slave_running = 0;
static int update_settings = 0;
static volatile unsigned int max_interval = 0;
static rwlock_t slaves_lock;

relay_server *relay_free (relay_server *relay)
{
    relay_server *next = relay->next;
    DEBUG1("freeing relay %s", relay->localmount);
    if (relay->source)
       source_free_source (relay->source);
    xmlFree (relay->server);
    xmlFree (relay->mount);
    xmlFree (relay->localmount);
    if (relay->username)
        xmlFree (relay->username);
    if (relay->password)
        xmlFree (relay->password);
    free (relay);
    return next;
}


relay_server *relay_copy (relay_server *r)
{
    relay_server *copy = calloc (1, sizeof (relay_server));

    if (copy)
    {
        copy->server = xmlCharStrdup (r->server);
        copy->mount = xmlCharStrdup (r->mount);
        copy->localmount = xmlStrdup (r->localmount);
        if (r->username)
            copy->username = xmlStrdup (r->username);
        if (r->password)
            copy->password = xmlStrdup (r->password);
        copy->port = r->port;
        copy->mp3metadata = r->mp3metadata;
        copy->on_demand = r->on_demand;
        copy->enable = r->enable;
        copy->source = r->source;
        r->source = NULL;
    }
    return copy;
}


/* force a recheck of the relays. This will recheck the master server if
 * a this is a slave.
 */
void slave_recheck_mounts (void)
{
    max_interval = 0;
    update_settings = 1;
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
    max_interval = 0;
#ifndef HAVE_CURL
    WARN0 ("streamlist request disabled, rebuild with libcurl if required");
#endif
    _slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}


void slave_shutdown(void)
{
    if (!slave_running)
        return;
    slave_running = 0;
    DEBUG0 ("waiting for slave thread");
    thread_join (_slave_thread_id);
    thread_rwlock_destroy (&slaves_lock);
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
    checking = global.redirectors;
    trail = &global.redirectors;

    DEBUG2 ("random selection %d (out of %d)", which, global.redirect_count);
    while (checking)
    {
        DEBUG2 ("...%s:%d", checking->server, checking->port);
        if (checking->next_update && checking->next_update+10 < global.time)
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

            INFO2 ("redirecting client to slave server "
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


/* This does the actual connection for a relay. A thread is
 * started off if a connection can be acquired
 */
static void *start_relay_stream (void *arg)
{
    relay_server *relay = arg;
    sock_t streamsock = SOCK_ERROR;
    source_t *src = relay->source;
    http_parser_t *parser = NULL;
    connection_t *con=NULL;
    char header[4096];

    relay->running = 1;
    INFO1("Starting relayed source at mountpoint \"%s\"", relay->localmount);
    do
    {
        char *auth_header;
        char *server_id;
        ice_config_t *config;

        streamsock = sock_connect_wto (relay->server, relay->port, 10);
        if (streamsock == SOCK_ERROR)
        {
            WARN3("Failed to relay stream from master server, couldn't connect to http://%s:%d%s",
                    relay->server, relay->port, relay->mount);
            break;
        }
        con = connection_create (streamsock, -1, NULL);

        config = config_get_config ();
        server_id = strdup (config->server_id);
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
        {
            auth_header = strdup ("");
        }
        config_release_config ();

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
                relay->mount,
                server_id,
                relay->mp3metadata?"Icy-MetaData: 1\r\n":"",
                auth_header);
        free (server_id);
        free (auth_header);
        memset (header, 0, sizeof(header));
        if (util_read_header (con->sock, header, 4096, READ_ENTIRE_HEADER) == 0)
        {
            WARN0("Header read failed");
            break;
        }
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        if (! httpp_parse_response (parser, header, strlen(header), relay->localmount))
        {
            ERROR0("Error parsing relay request");
            break;
        }
        if (httpp_getvar (parser, HTTPP_VAR_ERROR_MESSAGE))
        {
            ERROR1("Error from relay request: %s", httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
            break;
        }
        src->parser = parser;

        global_lock ();
        if (client_create (&src->client, con, parser) < 0)
        {
            global_unlock ();
            /* make sure only the client_destory frees these */
            con = NULL;
            parser = NULL;
            break;
        }
        global_unlock ();
        sock_set_blocking (streamsock, SOCK_NONBLOCK);
        con = NULL;
        parser = NULL;
        client_set_queue (src->client, NULL);

        if (connection_complete_source (src, 0) < 0)
        {
            DEBUG0("Failed to complete source initialisation");
            break;
        }
        stats_event_inc(NULL, "source_relay_connections");
        stats_event (relay->localmount, "source_ip", relay->server);

        source_main (relay->source);

        if (relay->on_demand == 0)
        {
            /* only keep refreshing YP entries for inactive on-demand relays */
            yp_remove (relay->localmount);
            relay->source->yp_public = -1;
            relay->start = global.time + 10; /* prevent busy looping if failing */
        }

        /* we've finished, now get cleaned up */
        relay->cleanup = 1;

        return NULL;
    } while (0);

    if (relay->source->fallback_mount)
    {
        source_t *fallback_source;

        DEBUG1 ("failed relay, fallback to %s", relay->source->fallback_mount);
        avl_tree_rlock(global.source_tree);
        fallback_source = source_find_mount (relay->source->fallback_mount);

        if (fallback_source != NULL)
            source_move_clients (relay->source, fallback_source);

        avl_tree_unlock (global.source_tree);
    }

    if (con)
        connection_close (con);
    if (parser)
        httpp_destroy (parser);
    source_clear_source (relay->source);

    /* cleanup relay, but prevent this relay from starting up again too soon */
    relay->start = global.time + max_interval;
    relay->cleanup = 1;

    return NULL;
}


/* wrapper for starting the provided relay stream */
static void check_relay_stream (relay_server *relay)
{
    if (relay->source == NULL)
    {
        if (relay->localmount[0] != '/')
        {
            WARN1 ("relay mountpoint \"%s\" does not start with /, skipping",
                    relay->localmount);
            return;
        }
        /* new relay, reserve the name */
        relay->source = source_reserve (relay->localmount);
        if (relay->source)
        {
            DEBUG1("Adding relay source at mountpoint \"%s\"", relay->localmount);
            slave_rebuild_mounts();
        }
        else
            WARN1 ("new relay but source \"%s\" already exists", relay->localmount);
    }
    do
    {
        source_t *source = relay->source;
        /* skip relay if active, not configured or just not time yet */
        if (relay->source == NULL || relay->running || relay->start > global.time)
            break;
        if (relay->enable == 0)
        {
            stats_event (relay->localmount, NULL, NULL);
            break;
        }
        if (relay->on_demand && source->on_demand_req == 0)
        {
            relay->source->on_demand = relay->on_demand;

            if (source->fallback_mount && source->fallback_override)
            {
                source_t *fallback;
                DEBUG1 ("checking %s for fallback override", source->fallback_mount);
                avl_tree_rlock (global.source_tree);
                fallback = source_find_mount (source->fallback_mount);
                if (fallback && fallback->running && fallback->listeners)
                {
                   DEBUG2 ("fallback running %d with %lu listeners", fallback->running, fallback->listeners);
                   source->on_demand_req = 1;
                }
                avl_tree_unlock (global.source_tree);
            }
            if (source->on_demand_req == 0)
                break;
        }

        relay->start = global.time + 5;
        relay->thread = thread_create ("Relay Thread", start_relay_stream,
                relay, THREAD_ATTACHED);
        return;

    } while (0);
    /* the relay thread may of shut down itself */
    if (relay->cleanup)
    {
        if (relay->thread)
        {
            DEBUG1 ("waiting for relay thread for \"%s\"", relay->localmount);
            thread_join (relay->thread);
            relay->thread = NULL;
        }
        relay->cleanup = 0;
        relay->running = 0;

        if (relay->enable == 0)
        {
            stats_event (relay->localmount, NULL, NULL);
            slave_rebuild_mounts();
            return;
        }
        if (relay->on_demand)
        {
            ice_config_t *config = config_get_config ();
            mount_proxy *mountinfo = config_find_mount (config, relay->localmount);
            source_update_settings (config, relay->source, mountinfo);
            config_release_config ();
            stats_event (relay->localmount, "listeners", "0");
        }
    }
}


/* compare the 2 relays to see if there are any changes, return 1 if
 * the relay needs to be restarted, 0 otherwise
 */
static int relay_has_changed (relay_server *new, relay_server *old)
{
    do
    {
        if (strcmp (new->mount, old->mount) != 0)
            break;
        if (strcmp (new->server, old->server) != 0)
            break;
        if (new->port != old->port)
            break;
        if (new->mp3metadata != old->mp3metadata)
            break;
        if (new->on_demand != old->on_demand)
            old->on_demand = new->on_demand;
        return 0;
    } while (0);
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
        if (to_free->source)
        {
            if (to_free->running)
            {
                /* relay has been removed from xml, shut down active relay */
                DEBUG1 ("source shutdown request on \"%s\"", to_free->localmount);
                to_free->source->running = 0;
                thread_join (to_free->thread);
                slave_rebuild_mounts();
            }
            else
                stats_event (to_free->localmount, NULL, NULL);
        }
        to_free = relay_free (to_free);
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
    char *buffer;
    char *username;
    char *password;
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
            r->server = xmlStrdup (master->server);
            r->port = master->port;
            r->mount = xmlStrdup (buf);
            r->localmount = xmlStrdup (buf);
            r->mp3metadata = 1;
            r->on_demand = master->on_demand;
            r->enable = 1;
            if (master->send_auth)
            {
                r->username = xmlStrdup (master->username);
                r->password = xmlStrdup (master->password);
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

    if (master->ssl_port)
    {
        protocol = "https";
        port = master->ssl_port;
    }
    snprintf (auth, sizeof (auth), "%s:%s", master->username, master->password);
    snprintf (url, sizeof (url), "%s://%s:%d/admin/streamlist.txt%s",
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
    curl_easy_setopt (handle, CURLOPT_TIMEOUT, 15L);

    if (curl_easy_perform (handle) != 0)
        WARN2 ("Failed URL access \"%s\" (%s)", url, error);
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
    details->on_demand = config->on_demand;
    details->server_id = strdup (config->server_id);
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
    {
        redirector_clearall();
        return;
    }

    thread_rwlock_wlock (&slaves_lock);
    redirect = find_slave_host (config->master_server, config->master_server_port);
    if (redirect == NULL)
    {
        INFO2 ("adding master %s:%d", config->master_server, config->master_server_port);
        redirector_add (config->master_server, config->master_server_port, 0);
    }
    else
        redirect->next_update += max_interval;
    thread_rwlock_unlock (&slaves_lock);
}


static void *_slave_thread(void *arg)
{
    ice_config_t *config;
    unsigned int interval = 0;

    config = config_get_config();
    update_master_as_slave (config);
    config_release_config();
    source_recheck_mounts();

    while (1)
    {
        relay_server *cleanup_relays = NULL;
        int skip_timer = 0;

        /* re-read xml file if requested */
        if (global . schedule_config_reread)
        {
            event_config_read (NULL);
            global . schedule_config_reread = 0;
        }

        thread_sleep (1000000);
        if (slave_running == 0)
            break;
        time (&global.time);
        ++interval;

        thread_mutex_lock (&(config_locks()->relay_lock));

        /* only update relays lists when required */
        if (max_interval <= interval)
        {
            DEBUG0 ("checking master stream list");
            config = config_get_config();

            if (max_interval == 0)
                skip_timer = 1;
            interval = 0;
            max_interval = config->master_update_interval;
            update_master_as_slave (config);

            update_from_master (config);

            cleanup_relays = update_relays (&global.relays, config->relay);

            config_release_config();
        }
        relay_check_streams (global.master_relays, NULL, skip_timer);
        relay_check_streams (global.relays, cleanup_relays, skip_timer);
        thread_mutex_unlock (&(config_locks()->relay_lock));

        if (update_settings)
        {
            update_settings = 0;
            source_recheck_mounts();
        }
    }
    INFO0 ("shutting down current relays");
    relay_check_streams (NULL, global.relays, 0);
    relay_check_streams (NULL, global.master_relays, 0);
    redirector_clearall();

    INFO0 ("Slave thread shutdown complete");

    return NULL;
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
    while (global.redirectors)
    {
        redirect_host *current = global.redirectors;
        global.redirectors = current->next;
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
    char *value;
    int rport, interval;

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
        redirect->next_update = global.time + interval;
    }
    thread_rwlock_unlock (&slaves_lock);
}



/* search list of redirectors for a matching entry, lock must be held before
 * invoking this function
 */
static redirect_host *find_slave_host (const char *server, int port)
{
    redirect_host *redirect = global.redirectors;
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
    int allowed = config->max_redirects;
    redirect_host *redirect;

    config_release_config();

    if (global.redirect_count >= allowed)
    {
        INFO1 ("redirect to slave limit reached (%d)", global.redirect_count);
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
        redirect->next_update = global.time + interval;
    redirect->next = global.redirectors;
    global.redirectors = redirect;
    global.redirect_count++;
    INFO3 ("slave (%d) at %s:%d added", global.redirect_count,
            redirect->server, redirect->port);
}

