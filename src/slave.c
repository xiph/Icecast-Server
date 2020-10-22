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
 * Copyright 2012-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#include "compat.h"

#include <libxml/uri.h>
#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/net/sock.h"
#include "common/httpp/httpp.h"

#include "slave.h"
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
#include "prng.h"

#define CATMODULE "slave"

struct relay_tag {
    relay_config_t *config;
    source_t *source;
    int running;
    int cleanup;
    time_t start;
    thread_type *thread;
    relay_t *next;
};

static void *_slave_thread(void *arg);
static thread_type *_slave_thread_id;
static int slave_running = 0;
static volatile int update_settings = 0;
static volatile int update_all_mounts = 0;
static volatile unsigned int max_interval = 0;
static mutex_t _slave_mutex; // protects slave_running, update_settings, update_all_mounts, max_interval

static inline void relay_config_upstream_free (relay_config_upstream_t *upstream)
{
    if (upstream->server)
        xmlFree(upstream->server);
    if (upstream->mount)
        xmlFree(upstream->mount);
    if (upstream->username)
        xmlFree(upstream->username);
    if (upstream->password)
        xmlFree(upstream->password);
}

void relay_config_free (relay_config_t *relay)
{
    size_t i;

    ICECAST_LOG_DEBUG("freeing relay config for %s", relay->localmount);

    for (i = 0; i < relay->upstreams; i++) {
        relay_config_upstream_free(&(relay->upstream[i]));
    }

    relay_config_upstream_free(&(relay->upstream_default));

    xmlFree(relay->localmount);
    free(relay->upstream);
    free(relay);
}

relay_t *relay_free (relay_t *relay)
{
    relay_t *next = relay->next;

    ICECAST_LOG_DEBUG("freeing relay %s", relay->config->localmount);

    if (relay->source)
       source_free_source (relay->source);

    relay_config_free(relay->config);

    free(relay);
    return next;
}


static inline void relay_config_upstream_copy(relay_config_upstream_t *dst, const relay_config_upstream_t *src)
{
    dst->server = (char *)xmlCharStrdup(src->server);
    dst->mount = (char *)xmlCharStrdup(src->mount);

    if (src->username)
        dst->username = (char *)xmlCharStrdup(src->username);
    if (src->password)
        dst->password = (char *)xmlCharStrdup(src->password);

    dst->port = src->port;

    dst->mp3metadata = src->mp3metadata;
}

static inline relay_config_t *relay_config_copy (relay_config_t *r)
{
    relay_config_t *copy = calloc (1, sizeof (relay_config_t));
    relay_config_upstream_t *u = NULL;
    size_t i;

    if (r->upstreams) {
        u = calloc(r->upstreams, sizeof(relay_config_upstream_t));
        if (!u) {
            free(copy);
            return NULL;
        }
    }

    if (!copy) {
        free(u);
        return NULL;
    }

    copy->upstream = u;
    copy->upstreams = r->upstreams;

    copy->localmount = (char *)xmlCharStrdup(r->localmount);
    copy->on_demand = r->on_demand;

    relay_config_upstream_copy(&(copy->upstream_default), &(r->upstream_default));

    for (i = 0; i < r->upstreams; i++)
        relay_config_upstream_copy(&(copy->upstream[i]), &(r->upstream[i]));


    return copy;
}

static inline relay_t *relay_new(relay_config_t *config)
{
    relay_t *r = calloc(1, sizeof(*r));

    if (!r)
        return NULL;

    r->config = relay_config_copy(config);
    if (!r->config) {
        free(r);
        return NULL;
    }

    return r;
}

/* force a recheck of the relays. This will recheck the master server if
 * this is a slave and rebuild all mountpoints in the stats tree
 */
void slave_update_all_mounts(void)
{
    thread_mutex_lock(&_slave_mutex);
    max_interval = 0;
    update_all_mounts = 1;
    update_settings = 1;
    thread_mutex_unlock(&_slave_mutex);
}


/* Request slave thread to check the relay list for changes and to
 * update the stats for the current streams.
 */
void slave_rebuild_mounts(void)
{
    thread_mutex_lock(&_slave_mutex);
    update_settings = 1;
    thread_mutex_unlock(&_slave_mutex);
}


void slave_initialize(void)
{
    if (slave_running)
        return;

    slave_running = 1;
    max_interval = 0;
    thread_mutex_create (&_slave_mutex);
    _slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}


void slave_shutdown(void)
{
    thread_mutex_lock(&_slave_mutex);
    if (!slave_running) {
        thread_mutex_unlock(&_slave_mutex);
        return;
    }
    slave_running = 0;
    thread_mutex_unlock(&_slave_mutex);

    ICECAST_LOG_DEBUG("waiting for slave thread");
    thread_join (_slave_thread_id);
}


/* Actually open the connection and do some http parsing, handle any 302
 * responses within here.
 */
#define _GET_UPSTREAM_SETTING(n) ((upstream && upstream->n) ? upstream->n : relay->config->upstream_default.n)
static client_t *open_relay_connection (relay_t *relay, relay_config_upstream_t *upstream)
{
    int redirects = 0;
    char *server_id = NULL;
    ice_config_t *config;
    http_parser_t *parser = NULL;
    connection_t *con=NULL;
    char *server = strdup (_GET_UPSTREAM_SETTING(server));
    char *mount = strdup (_GET_UPSTREAM_SETTING(mount));
    int port = _GET_UPSTREAM_SETTING(port);
    char *auth_header;
    char header[4096];

    config = config_get_config ();
    server_id = strdup (config->server_id);
    config_release_config ();

    /* build any authentication header before connecting */
    if (_GET_UPSTREAM_SETTING(username) && _GET_UPSTREAM_SETTING(password))
    {
        char *esc_authorisation;
        unsigned len = strlen(_GET_UPSTREAM_SETTING(username)) + strlen(_GET_UPSTREAM_SETTING(password)) + 2;

        auth_header = malloc (len);
        snprintf (auth_header, len, "%s:%s", _GET_UPSTREAM_SETTING(username), _GET_UPSTREAM_SETTING(password));
        esc_authorisation = util_base64_encode(auth_header, len);
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

        ICECAST_LOG_INFO("connecting to %s:%d", server, port);

        streamsock = sock_connect_wto_bind (server, port, _GET_UPSTREAM_SETTING(bind), 10);
        if (streamsock == SOCK_ERROR)
        {
            ICECAST_LOG_WARN("Failed to connect to %s:%d", server, port);
            break;
        }
        con = connection_create(streamsock, NULL, NULL, strdup(server));

        /* At this point we may not know if we are relaying an mp3 or vorbis
         * stream, but only send the icy-metadata header if the relay details
         * state so (the typical case).  It's harmless in the vorbis case. If
         * we don't send in this header then relay will not have mp3 metadata.
         */
        sock_write(streamsock, "GET %s HTTP/1.0\r\n"
                "User-Agent: %s\r\n"
                "Host: %s\r\n"
                "%s"
                "%s"
                "\r\n",
                mount,
                server_id,
                server,
                _GET_UPSTREAM_SETTING(mp3metadata) ? "Icy-MetaData: 1\r\n" : "",
                auth_header);
        memset (header, 0, sizeof(header));
        if (util_read_header (con->sock, header, 4096, READ_ENTIRE_HEADER) == 0)
        {
            ICECAST_LOG_ERROR("Header read failed for %s (%s:%d%s)", relay->config->localmount, server, port, mount);
            break;
        }
        prng_write(header, strlen(header));
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        if (! httpp_parse_response (parser, header, strlen(header), relay->config->localmount))
        {
            ICECAST_LOG_ERROR("Error parsing relay request for %s (%s:%d%s)", relay->config->localmount,
                    server, port, mount);
            break;
        }
        if (strcmp (httpp_getvar (parser, HTTPP_VAR_ERROR_CODE), "302") == 0)
        {
            /* better retry the connection again but with different details */
            const char *uri, *mountpoint;
            int len;

            uri = httpp_getvar (parser, "location");
            ICECAST_LOG_INFO("redirect received %s", uri);
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
            client_t *client = NULL;

            if (httpp_getvar (parser, HTTPP_VAR_ERROR_MESSAGE))
            {
                ICECAST_LOG_ERROR("Error from relay request: %s (%s)", relay->config->localmount,
                        httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
                break;
            }
            global_lock ();
            if (client_create (&client, con, parser) < 0)
            {
                global_unlock ();
                /* make sure only the client_destory frees these */
                con = NULL;
                parser = NULL;
                client_destroy (client);
                break;
            }
            global_unlock ();
            sock_set_blocking (streamsock, 0);
            client_set_queue (client, NULL);
            client_complete(client);
            free (server);
            free (mount);
            free (server_id);
            free (auth_header);

            return client;
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
    return NULL;
}


/* This does the actual connection for a relay. A thread is
 * started off if a connection can be acquired
 */
static void *start_relay_stream (void *arg)
{
    relay_t *relay = arg;
    source_t *src = relay->source;
    client_t *client;

    ICECAST_LOG_INFO("Starting relayed source at mountpoint \"%s\"", relay->config->localmount);
    do
    {
        size_t i;

        for (i = 0; i < relay->config->upstreams; i++) {
            ICECAST_LOG_DEBUG("For relay on mount \"%s\", trying upstream #%zu", relay->config->localmount, i);
            client = open_relay_connection(relay, &(relay->config->upstream[i]));
            if (client)
                break;
        }

        /* if we have no upstreams defined, use the default upstream */
        if (!relay->config->upstreams) {
            ICECAST_LOG_DEBUG("For relay on mount \"%s\" with no upstreams trying upstream default", relay->config->localmount);
            client = open_relay_connection(relay, NULL);
        }

        if (client == NULL)
            continue;

        src->client = client;
        src->parser = client->parser;
        src->con = client->con;

        if (connection_complete_source (src, 0) < 0)
        {
            ICECAST_LOG_INFO("Failed to complete source initialisation");
            client_destroy (client);
            src->client = NULL;
            continue;
        }
        stats_event_inc(NULL, "source_relay_connections");
        stats_event (relay->config->localmount, "source_ip", client->con->ip);

        source_main (relay->source);

        if (relay->config->on_demand == 0)
        {
            /* only keep refreshing YP entries for inactive on-demand relays */
            yp_remove (relay->config->localmount);
            relay->source->yp_public = -1;
            relay->start = time(NULL) + 10; /* prevent busy looping if failing */
            slave_update_all_mounts();
        }

        /* we've finished, now get cleaned up */
        relay->cleanup = 1;
        slave_rebuild_mounts();

        return NULL;
    } while (0); /* TODO allow looping through multiple servers */

    if (relay->source->fallback_mount)
    {
        source_t *fallback_source;

        ICECAST_LOG_DEBUG("failed relay, fallback to %s", relay->source->fallback_mount);
        avl_tree_rlock(global.source_tree);
        fallback_source = source_find_mount(relay->source->fallback_mount);

        if (fallback_source != NULL)
            source_move_clients(relay->source, fallback_source, NULL);

        avl_tree_unlock(global.source_tree);
    }

    source_clear_source(relay->source);

    /* cleanup relay, but prevent this relay from starting up again too soon */
    thread_mutex_lock(&_slave_mutex);
    thread_mutex_lock(&(config_locks()->relay_lock));
    relay->source->on_demand = 0;
    relay->start = time(NULL) + max_interval;
    relay->cleanup = 1;
    thread_mutex_unlock(&(config_locks()->relay_lock));
    thread_mutex_unlock(&_slave_mutex);

    return NULL;
}


/* wrapper for starting the provided relay stream */
static void check_relay_stream (relay_t *relay)
{
    if (relay->source == NULL)
    {
        if (relay->config->localmount[0] != '/')
        {
            ICECAST_LOG_WARN("relay mountpoint \"%s\" does not start with /, skipping",
                    relay->config->localmount);
            return;
        }
        /* new relay, reserve the name */
        relay->source = source_reserve (relay->config->localmount);
        if (relay->source)
        {
            ICECAST_LOG_DEBUG("Adding relay source at mountpoint \"%s\"", relay->config->localmount);
            if (relay->config->on_demand)
            {
                ice_config_t *config = config_get_config ();
                mount_proxy *mountinfo = config_find_mount (config, relay->config->localmount, MOUNT_TYPE_NORMAL);
                relay->source->on_demand = relay->config->on_demand;
                if (mountinfo == NULL)
                    source_update_settings (config, relay->source, mountinfo);
                config_release_config ();
                stats_event (relay->config->localmount, "listeners", "0");
                slave_update_all_mounts();
            }
        }
        else
        {
            if (relay->start == 0)
            {
                ICECAST_LOG_WARN("new relay but source \"%s\" already exists", relay->config->localmount);
                relay->start = 1;
            }
            return;
        }
    }
    do
    {
        source_t *source = relay->source;
        /* skip relay if active, not configured or just not time yet */
        if (relay->source == NULL || relay->running || relay->start > time(NULL))
            break;
        /* check if an inactive on-demand relay has a fallback that has listeners */
        if (relay->config->on_demand && source->on_demand_req == 0)
        {
            relay->source->on_demand = relay->config->on_demand;

            if (source->fallback_mount && source->fallback_override)
            {
                source_t *fallback;
                avl_tree_rlock (global.source_tree);
                fallback = source_find_mount (source->fallback_mount);
                if (fallback && fallback->running && fallback->listeners)
                {
                   ICECAST_LOG_DEBUG("fallback running %d with %lu listeners", fallback->running, fallback->listeners);
                   source->on_demand_req = 1;
                }
                avl_tree_unlock (global.source_tree);
            }
            if (source->on_demand_req == 0)
                break;
        }

        relay->start = time(NULL) + 5;
        relay->running = 1;
        relay->thread = thread_create ("Relay Thread", start_relay_stream,
                relay, THREAD_ATTACHED);
        return;

    } while (0);
    /* the relay thread may of shut down itself */
    if (relay->cleanup)
    {
        if (relay->thread)
        {
            ICECAST_LOG_DEBUG("waiting for relay thread for \"%s\"", relay->config->localmount);
            thread_join (relay->thread);
            relay->thread = NULL;
        }
        relay->cleanup = 0;
        relay->running = 0;

        if (relay->config->on_demand && relay->source)
        {
            ice_config_t *config = config_get_config ();
            mount_proxy *mountinfo = config_find_mount (config, relay->config->localmount, MOUNT_TYPE_NORMAL);
            source_update_settings (config, relay->source, mountinfo);
            config_release_config ();
            stats_event (relay->config->localmount, "listeners", "0");
        }
    }
}


/* compare the 2 relays to see if there are any changes, return 1 if
 * the relay needs to be restarted, 0 otherwise
 */
#define _EQ_STR(a,b) (((a) == (b)) || ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0))
#define _EQ_ATTR(x) (_EQ_STR((new->x), (old->x)))
static int relay_has_changed_upstream(const relay_config_upstream_t *new, const relay_config_upstream_t *old)
{
    if (new->mp3metadata != old->mp3metadata)
        return 1;

    if (!_EQ_ATTR(server) || new->port != old->port)
        return 1;

    if (!_EQ_ATTR(mount))
        return 1;

/* NOTE: We currently do not consider this a relevant change. Why?
    if (!_EQ_ATTR(username) || !_EQ_ATTR(password))
        return 1;

    if (!_EQ_ATTR(bind))
        return 1;
*/

    return 0;
}

static int relay_has_changed (const relay_config_t *new, relay_config_t *old)
{
    size_t i;

    /* This is not fully true: If more upstreams has been added there is no reason
     * to restart the relay. However for now we ignore this case. TODO: Change this.
     */
    if (new->upstreams != old->upstreams)
        return 1;

    for (i = 0; i < new->upstreams; i++) {
        if (relay_has_changed_upstream(&(new->upstream[i]), &(old->upstream[i])))
            return 1;
    }

    if (relay_has_changed_upstream(&(new->upstream_default), &(old->upstream_default)))
        return 1;

    /* Why do we do this here? */
    old->on_demand = new->on_demand;

    return 0;
}


/* go through updated looking for relays that are different configured. The
 * returned list contains relays that should be kept running, current contains
 * the list of relays to shutdown
 */
static relay_t *
update_relay_set(relay_t **current, relay_config_t **updated, size_t updated_length)
{
    relay_config_t *relay;
    relay_t *existing_relay, **existing_p;
    relay_t *new_list = NULL;
    size_t i;

    for (i = 0; i < updated_length; i++) {
        relay = updated[i];

        existing_relay = *current;
        existing_p = current;

        while (existing_relay)
        {
            /* break out if keeping relay */
            if (strcmp(relay->localmount, existing_relay->config->localmount) == 0)
                if (relay_has_changed(relay, existing_relay->config) == 0)
                    break;
            existing_p = &existing_relay->next;

            existing_relay = existing_relay->next;
        }


        if (existing_relay == NULL)
        {
            /* new one, copy and insert */
            existing_relay = relay_new(relay);
        }
        else
        {
            *existing_p = existing_relay->next;
        }
        existing_relay->next = new_list;
        new_list = existing_relay;
    }

    return new_list;
}


/* update the relay_list with entries from new_relay_list. Any new relays
 * are added to the list, and any not listed in the provided new_relay_list
 * are separated and returned in a separate list
 */
static relay_t *
update_relays (relay_t **relay_list, relay_config_t **new_relay_list, size_t new_relay_list_length)
{
    relay_t *active_relays, *cleanup_relays;

    active_relays = update_relay_set(relay_list, new_relay_list, new_relay_list_length);

    cleanup_relays = *relay_list;
    /* re-assign new set */
    *relay_list = active_relays;

    return cleanup_relays;
}


static void relay_check_streams (relay_t *to_start,
        relay_t *to_free, int skip_timer)
{
    relay_t *relay;

    while (to_free)
    {
        if (to_free->source)
        {
            if (to_free->running)
            {
                /* relay has been removed from xml, shut down active relay */
                ICECAST_LOG_DEBUG("source shutdown request on \"%s\"", to_free->config->localmount);
                to_free->running = 0;
                to_free->source->running = 0;
                thread_join (to_free->thread);
            }
            else
                stats_event (to_free->config->localmount, NULL, NULL);
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


static int update_from_master(ice_config_t *config)
{
    char *master = NULL, *password = NULL, *username= NULL;
    int port;
    sock_t mastersock;
    int ret = 0;
    char buf[256];
    do
    {
        char *authheader, *data;
        relay_t *cleanup_relays;
        relay_config_t **new_relays = NULL;
        size_t new_relays_length = 0;
        int len, count = 1;
        int on_demand;
        size_t i;

        username = strdup(config->master_username);
        if (config->master_password)
            password = strdup(config->master_password);

        if (config->master_server)
            master = strdup(config->master_server);

        port = config->master_server_port;

        if (password == NULL || master == NULL || port == 0)
            break;
        on_demand = config->on_demand;
        ret = 1;
        config_release_config();
        mastersock = sock_connect_wto(master, port, 10);

        if (mastersock == SOCK_ERROR)
        {
            ICECAST_LOG_WARN("Relay slave failed to contact master server to fetch stream list");
            break;
        }

        len = strlen(username) + strlen(password) + 2;
        authheader = malloc(len);
        snprintf (authheader, len, "%s:%s", username, password);
        data = util_base64_encode(authheader, len);
        sock_write (mastersock,
                "GET /admin/streamlist.txt HTTP/1.0\r\n"
                "Authorization: Basic %s\r\n"
                "\r\n", data);
        free(authheader);
        free(data);

        if (sock_read_line(mastersock, buf, sizeof(buf)) == 0 ||
                ((strncmp (buf, "HTTP/1.0 200", 12) != 0) && (strncmp (buf, "HTTP/1.1 200", 12) != 0)))
        {
            sock_close (mastersock);
            ICECAST_LOG_WARN("Master rejected streamlist request");
            break;
        } else {
            ICECAST_LOG_INFO("Master accepted streamlist request");
        }

        while (sock_read_line(mastersock, buf, sizeof(buf))) {
            size_t len = strlen(buf);
            if (!len)
                break;
            prng_write(buf, len);
        }
        while (sock_read_line(mastersock, buf, sizeof(buf))) {
            size_t len = strlen(buf);
            relay_config_t *c = NULL;
            relay_config_t **n;

            if (!len)
                continue;
            prng_write(buf, len);

            ICECAST_LOG_DEBUG("read %d from master \"%s\"", count++, buf);
            xmlURIPtr parsed_uri = xmlParseURI(buf);
            if (parsed_uri == NULL) {
                ICECAST_LOG_DEBUG("Error while parsing line from master. Ignoring line.");
                continue;
            }

            n = realloc(new_relays, sizeof(*new_relays)*(new_relays_length + 1));
            if (n) {
                new_relays = n;

                c = calloc(1, sizeof(*c));
                new_relays[new_relays_length++] = c;
            }

            if (c) {
                if (parsed_uri->server != NULL) {
                    c->upstream_default.server = (char *)xmlCharStrdup(parsed_uri->server);
                    if (parsed_uri->port == 0) {
                        c->upstream_default.port = 80;
                    } else {
                        c->upstream_default.port = parsed_uri->port;
                    }
                } else {
                    c->upstream_default.server = (char *)xmlCharStrdup(master);
                    c->upstream_default.port = port;
                }

                c->upstream_default.mount = (char *)xmlCharStrdup(parsed_uri->path);
                c->localmount = (char *)xmlCharStrdup(parsed_uri->path);
                c->upstream_default.mp3metadata = 1;
                c->on_demand = on_demand;
                ICECAST_LOG_DEBUG("Added relay host=\"%s\", port=%d, mount=\"%s\"", c->upstream_default.server, c->upstream_default.port, c->upstream_default.mount);
            }
            xmlFreeURI(parsed_uri);
        }
        sock_close (mastersock);

        thread_mutex_lock (&(config_locks()->relay_lock));
        cleanup_relays = update_relays (&global.master_relays, new_relays, new_relays_length);

        relay_check_streams (global.master_relays, cleanup_relays, 0);

        for (i = 0; i < new_relays_length; i++) {
            relay_config_free(new_relays[i]);
        }
        free(new_relays);

        thread_mutex_unlock (&(config_locks()->relay_lock));

    } while(0);

    if (master)
        free (master);
    if (username)
        free (username);
    if (password)
        free (password);

    return ret;
}


static void *_slave_thread(void *arg)
{
    ice_config_t *config;
    unsigned int interval = 0;

    (void)arg;

    thread_mutex_lock(&_slave_mutex);
    update_settings = 0;
    update_all_mounts = 0;
    thread_mutex_unlock(&_slave_mutex);

    config = config_get_config();
    stats_global(config);
    config_release_config();
    source_recheck_mounts(1);

    while (1)
    {
        relay_t *cleanup_relays = NULL;
        int skip_timer = 0;

        /* re-read xml file if requested */
        global_lock();
        if (global.schedule_config_reread) {
            config_reread_config();
            global.schedule_config_reread = 0;
        }
        global_unlock();

        thread_sleep(1000000);
        prng_auto_reseed();
        thread_mutex_lock(&_slave_mutex);
        if (slave_running == 0) {
            thread_mutex_unlock(&_slave_mutex);
            break;
        }
        thread_mutex_unlock(&_slave_mutex);

        ++interval;

        /* only update relays lists when required */
        thread_mutex_lock(&_slave_mutex);
        if (max_interval <= interval)
        {
            ICECAST_LOG_DEBUG("checking master stream list");
            config = config_get_config();

            if (max_interval == 0)
                skip_timer = 1;
            interval = 0;
            max_interval = config->master_update_interval;
            thread_mutex_unlock(&_slave_mutex);

            /* the connection could take some time, so the lock can drop */
            if (update_from_master (config))
                config = config_get_config();

            thread_mutex_lock (&(config_locks()->relay_lock));

            cleanup_relays = update_relays(&global.relays, config->relay, config->relay_length);

            config_release_config();
        }
        else
        {
            thread_mutex_unlock(&_slave_mutex);
            thread_mutex_lock (&(config_locks()->relay_lock));
        }

        relay_check_streams (global.relays, cleanup_relays, skip_timer);
        relay_check_streams (global.master_relays, NULL, skip_timer);
        thread_mutex_unlock (&(config_locks()->relay_lock));

        thread_mutex_lock(&_slave_mutex);
        if (update_settings)
        {
            source_recheck_mounts (update_all_mounts);
            update_settings = 0;
            update_all_mounts = 0;
        }
        thread_mutex_unlock(&_slave_mutex);
    }
    ICECAST_LOG_INFO("shutting down current relays");
    relay_check_streams (NULL, global.relays, 0);
    relay_check_streams (NULL, global.master_relays, 0);

    ICECAST_LOG_INFO("Slave thread shutdown complete");

    return NULL;
}

