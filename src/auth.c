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

/** 
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "auth.h"
#include "auth_htpasswd.h"
#include "auth_cmd.h"
#include "auth_url.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "stats.h"
#include "httpp/httpp.h"
#include "fserve.h"
#include "admin.h"

#include "logging.h"
#define CATMODULE "auth"


static spin_t auth_lock;
static volatile int thread_id;

static void *auth_run_thread (void *arg);
static int auth_postprocess_listener (auth_client *auth_user);


void auth_check_http (client_t *client)
{
    const char *header;
    char *username, *password;

    /* process any auth headers if any available */
    header = httpp_getvar (client->parser, "authorization");
    if (header == NULL)
        return;

    if (strncmp(header, "Basic ", 6) == 0)
    {
        /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
        char *tmp, *userpass = util_base64_decode (header+6);
        if (userpass == NULL)
        {
            WARN1("Base64 decode of Authorization header \"%s\" failed",
                    header+6);
            return;
        }

        tmp = strchr(userpass, ':');
        if (tmp == NULL)
        {
            free (userpass);
            return;
        }

        *tmp = 0;
        username = userpass;
        password = tmp+1;
        client->username = strdup (username);
        client->password = strdup (password);
        free (userpass);
        return;
    }
    WARN1 ("unhandled authorization header: %s", header);
}


static auth_client *auth_client_setup (const char *mount, client_t *client)
{
    ice_config_t *config = config_get_config_unlocked();
    auth_client *auth_user = calloc (1, sizeof(auth_client));

    auth_user->mount = strdup (mount);
    auth_user->hostname = strdup (config->hostname);
    auth_user->port = config->port;
    auth_user->client = client;
    return auth_user;
}


static void queue_auth_client (auth_client *auth_user, mount_proxy *mountinfo)
{
    auth_t *auth;
    int i;

    if (auth_user == NULL || mountinfo == NULL)
        return;
    auth = mountinfo->auth;
    thread_mutex_lock (&auth->lock);
    auth_user->next = NULL;
    auth_user->auth = auth;
    auth->refcount++;
    *auth->tailp = auth_user;
    auth->tailp = &auth_user->next;
    auth->pending_count++;
    for (i=0; i<auth->handlers; i++)
    {
        if (auth->handles [i].thread == NULL)
        {
            DEBUG1 ("starting auth thread %d", i);
            auth->handles [i].thread = thread_create ("auth thread", auth_run_thread,
                    &auth->handles [i], THREAD_DETACHED);
            break;
        }
    }
    if (i == auth->handlers)
        DEBUG0 ("max authentication handlers allocated");
    INFO2 ("auth on %s has %d pending", auth->mount, auth->pending_count);
    thread_mutex_unlock (&auth->lock);
}


/* release the auth. It is referred to by multiple structures so this is
 * refcounted and only actual freed after the last use
 */
void auth_release (auth_t *authenticator)
{
    if (authenticator == NULL)
        return;

    thread_mutex_lock (&authenticator->lock);
    authenticator->refcount--;
    DEBUG2 ("...refcount on auth_t %s is now %d", authenticator->mount, authenticator->refcount);
    if (authenticator->refcount)
    {
        thread_mutex_unlock (&authenticator->lock);
        return;
    }

    /* cleanup auth threads attached to this auth */
    authenticator->running = 0;
    while (authenticator->handlers)
    {
        while (authenticator->handles [authenticator->handlers-1].thread)
            thread_sleep (5000);
        if (authenticator->release_thread_data)
            authenticator->release_thread_data (authenticator,
                    authenticator->handles [authenticator->handlers-1].data);
        authenticator->handlers--;
    }
    free (authenticator->handles);

    if (authenticator->free)
        authenticator->free (authenticator);
    xmlFree (authenticator->type);
    xmlFree (authenticator->realm);
    thread_mutex_unlock (&authenticator->lock);
    thread_mutex_destroy (&authenticator->lock);
    free (authenticator->mount);
    free (authenticator);
}


static void auth_client_free (auth_client *auth_user)
{
    if (auth_user == NULL)
        return;
    if (auth_user->client)
    {
        client_t *client = auth_user->client;

        if (client->respcode)
            client_destroy (client);
        else
            client_send_401 (client, auth_user->auth->realm);
        auth_user->client = NULL;
    }
    auth_release (auth_user->auth);
    free (auth_user->hostname);
    free (auth_user->mount);
    free (auth_user);
}


/* verify that the listener is still connected. */
static int is_listener_connected (client_t *client)
{
    int ret = 1;
    if (client)
    {
        if (sock_active (client->con->sock) == 0)
            ret = 0;
    }
    return ret;
}


/* wrapper function for auth thread to authenticate new listener
 * connection details
 */
static void auth_new_listener (auth_client *auth_user)
{
    client_t *client = auth_user->client;

    /* make sure there is still a client at this point, a slow backend request
     * can be avoided if client has disconnected */
    if (is_listener_connected (client) == 0)
    {
        DEBUG0 ("listener is no longer connected");
        client->respcode = 400;
        return;
    }
    if (auth_user->auth->authenticate)
    {
        switch (auth_user->auth->authenticate (auth_user))
        {
            case AUTH_OK:
            case AUTH_FAILED:
                break;
            default:
                return;
        }
    }
    if (auth_postprocess_listener (auth_user) < 0)
        INFO1 ("client %lu failed", client->con->id);
}


/* wrapper function for auth thread to drop listener connections
 */
static void auth_remove_listener (auth_client *auth_user)
{
    DEBUG0 ("...queue listener");
    if (auth_user->auth->release_listener)
        auth_user->auth->release_listener (auth_user);
    auth_release (auth_user->auth);
    auth_user->auth = NULL;
    /* client is going, so auth is not an issue at this point */
    auth_user->client->authenticated = 0;
    client_send_404 (auth_user->client, "Failed relay");
    auth_user->client = NULL;
}


/* Called from auth thread to process any request for source client
 * authentication. Only applies to source clients, not relays.
 */
static void stream_auth_callback (auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (auth_user->auth->stream_auth)
        auth_user->auth->stream_auth (auth_user);

    if (client->authenticated)
        auth_postprocess_source (auth_user);
    else
        WARN1 ("Failed auth for source \"%s\"", auth_user->mount);
}


/* Callback from auth thread to handle a stream start event, this applies
 * to both source clients and relays.
 */
static void stream_start_callback (auth_client *auth_user)
{
    auth_t *auth = auth_user->auth;

    if (auth->stream_start)
        auth->stream_start (auth_user);
}


/* Callback from auth thread to handle a stream start event, this applies
 * to both source clients and relays.
 */
static void stream_end_callback (auth_client *auth_user)
{
    auth_t *auth = auth_user->auth;

    if (auth->stream_end)
        auth->stream_end (auth_user);
}


/* The auth thread main loop. */
static void *auth_run_thread (void *arg)
{
    auth_thread_t *handler = arg;
    auth_t *auth = handler->auth;

    INFO2 ("Authentication thread %d started for %s", handler->id, auth->mount);

    while (auth->running)
    {
        /* usually no clients are waiting, so don't bother taking locks */
        if (auth->head)
        {
            auth_client *auth_user;

            /* may become NULL before lock taken */
            thread_mutex_lock (&auth->lock);
            auth_user = (auth_client*)auth->head;
            if (auth_user == NULL)
            {
                thread_mutex_unlock (&auth->lock);
                continue;
            }
            DEBUG2 ("%d client(s) pending on %s", auth->pending_count, auth->mount);
            auth->head = auth_user->next;
            if (auth->head == NULL)
                auth->tailp = &auth->head;
            auth->pending_count--;
            thread_mutex_unlock (&auth->lock);
            auth_user->next = NULL;

            /* associate per-thread data with auth_user here */
            auth_user->thread_data = handler->data;
            auth_user->handler = handler->id;

            if (auth_user->process)
                auth_user->process (auth_user);
            else
                ERROR0 ("client auth process not set");

            auth_client_free (auth_user);

            continue;
        }
        break;
    }
    INFO1 ("Authenication thread %d shutting down", handler->id);
    handler->thread = NULL;
    return NULL;
}


/* Check whether this listener is on this source. This is only called when
 * there is auth. This may flag an existing listener to terminate.
 * return 1 if ok to add or 0 to prevent
 */
static int check_duplicate_logins (source_t *source, client_t *client, auth_t *auth)
{
    client_t *existing;

    if (auth == NULL || auth->allow_duplicate_users)
        return 1;

    /* allow multiple authenticated relays */
    if (client->username == NULL || client->is_slave)
        return 1;

    existing = source->active_clients;
    while (existing)
    {
        if (existing->con->error == 0 && existing->username &&
                strcmp (existing->username, client->username) == 0)
        {
            if (auth->drop_existing_listener)
            {
                existing->con->error = 1;
                return 1;
            }
            else
                return 0;
        }
        existing = existing->next;
    }
    return 1;
}


/* Add client to source if it finds one. If a 0 is returned then the client should not be
 * touched, if the return value is -1 then it failed to add and should not be touched.
 * If it's a -2 value then the client is still around for any further processing.
 */
static int add_listener_to_source (const char *mount, mount_proxy *mountinfo, client_t *client)
{
    int loop = 10;
    int within_limits;
    source_t *source;
    mount_proxy *minfo = mountinfo;
    const char *passed_mount = mount;
    ice_config_t *config = config_get_config_unlocked();

    do
    {
        int64_t stream_bitrate = 0;

        do
        {
            source = source_find_mount_raw (mount);
            if (loop == 0)
            {
                WARN0 ("preventing a fallback loop");
                client_send_403 (client, "Fallback through too many mountpoints");
                return -1;
            }
            if (source)
            {
                thread_mutex_lock (&source->lock);
                if (source->running || source->on_demand)
                    break;
                thread_mutex_unlock (&source->lock);
            }
            if (minfo == NULL || minfo->fallback_mount == NULL)
                return -2;
            mount = minfo->fallback_mount;
            minfo = config_find_mount (config_get_config_unlocked(), mount);
            loop--;
        } while (1);

        /* ok, we found a source and it is locked */
        if (client->is_slave)
        {
            if (source->client == NULL && source->on_demand == 0)
            {
                client_send_403 (client, "Slave relay reading from time unregulated stream");
                return -1;
            }
            INFO0 ("client is from a slave, bypassing limits");
            break;
        }
        if (source->format)
        {
            stream_bitrate  = 8 * rate_avg (source->format->in_bitrate);

            if (config->max_bandwidth)
            {
                int64_t global_rate = (int64_t)8 * global_getrate_avg (global.out_bitrate);

                DEBUG1 ("server outgoing bitrate is %" PRId64, global_rate);
                if (global_rate + stream_bitrate > config->max_bandwidth)
                {
                    thread_mutex_unlock (&source->lock);
                    INFO0 ("server-wide outgoing bandwidth limit reached");
                    client_send_403redirect (client, passed_mount, "server bandwidth reached");
                    return -1;
                }
            }
        }

        if (mountinfo == NULL)
            break; /* allow adding listeners, no mount limits imposed */

        if (check_duplicate_logins (source, client, mountinfo->auth) == 0)
        {
            thread_mutex_unlock (&source->lock);
            client_send_403 (client, "Account already in use");
            return -1;
        }

        /* set a per-mount disconnect time if auth hasn't set one already */
        if (mountinfo->max_listener_duration && client->con->discon_time == 0)
            client->con->discon_time = global.time + mountinfo->max_listener_duration;

        INFO3 ("max on %s is %ld (cur %lu)", source->mount,
                mountinfo->max_listeners, source->listeners);

        within_limits = 1;
        if (mountinfo->max_bandwidth > -1 && stream_bitrate)
        {
            DEBUG3 ("checking bandwidth limits for %s (%" PRId64 ", %" PRId64 ")",
                    mountinfo->mountname, stream_bitrate, mountinfo->max_bandwidth);
            if ((source->listeners+1) * stream_bitrate > mountinfo->max_bandwidth)
            {
                INFO1 ("bandwidth limit reached on %s", source->mount);
                within_limits = 0;
            }
        }
        if (within_limits)
        {
            if (mountinfo->max_listeners == -1)
                break;

            if (source->listeners < (unsigned long)mountinfo->max_listeners)
                break;
            INFO1 ("max listener count reached on %s", source->mount);
        }

        /* minfo starts off as mountinfo put cascades through fallbacks */
        if (minfo && minfo->fallback_when_full && minfo->fallback_mount)
        {
            thread_mutex_unlock (&source->lock);
            mount = minfo->fallback_mount;
            INFO1 ("stream full trying %s", mount);
            loop--;
            continue;
        }

        /* now we fail the client */
        thread_mutex_unlock (&source->lock);
        client_send_403redirect (client, passed_mount, "max listeners reached");
        return -1;

    } while (1);

    client->write_to_client = format_generic_write_to_client;
    client->check_buffer = format_check_http_buffer;
    client->refbuf->len = PER_CLIENT_REFBUF_SIZE;
    memset (client->refbuf->data, 0, PER_CLIENT_REFBUF_SIZE);

    /* lets add the client to the active list */
    client->next = source->active_clients;
    source->active_clients = client;
    source->listeners++;

    thread_mutex_unlock (&source->lock);

    if (source->running == 0 && source->on_demand)
    {
        /* enable on-demand relay to start, wake up the slave thread */
        DEBUG0("kicking off on-demand relay");
        source->on_demand_req = 1;
    }
    DEBUG1 ("Added client to %s", source->mount);
    return 0;
}


/* Add listener to the pending lists of either the source or fserve thread. This can be run
 * from the connection or auth thread context. return -1 to indicate that client has been
 * terminated, 0 for receiving content.
 */
static int add_authenticated_listener (const char *mount, mount_proxy *mountinfo, client_t *client)
{
    int ret = 0;

    client->authenticated = 1;

    /* check whether we are processing a streamlist request for slaves */
    if (strcmp (mount, "/admin/streams") == 0)
    {
        if (client->parser->req_type == httpp_req_stats)
        {
            stats_add_listener (client, STATS_SLAVE|STATS_GENERAL);
            return 0;
        }
        mount = httpp_get_query_param (client->parser, "mount");
        if (mount == NULL)
        {
            command_list_mounts (client, TEXT);
            return 0;
        }
        mountinfo = config_find_mount (config_get_config_unlocked(), mount);
    }

    /* Here we are parsing the URI request to see if the extension is .xsl, if
     * so, then process this request as an XSLT request
     */
    if (util_check_valid_extension (mount) == XSLT_CONTENT)
    {
        /* If the file exists, then transform it, otherwise, write a 404 */
        DEBUG0("Stats request, sending XSL transformed stats");
        stats_transform_xslt (client, mount);
        return 0;
    }

    avl_tree_rlock (global.source_tree);
    ret = add_listener_to_source (mount, mountinfo, client);
    avl_tree_unlock (global.source_tree);

    if (ret == -2)
    {
        if (mountinfo && mountinfo->file_seekable == 0)
        {
            DEBUG1 ("disable seek on file matching %s", mountinfo->mountname);
            httpp_deletevar (client->parser, "range");
            httpp_setvar (client->parser, HTTPP_VAR_NO_CONTENT_LENGTH, "yes");
        }
        ret = fserve_client_create (client, mount);
    }
    return ret;
}


static int auth_postprocess_listener (auth_client *auth_user)
{
    int ret;
    client_t *client = auth_user->client;
    auth_t *auth = auth_user->auth;
    ice_config_t *config;
    mount_proxy *mountinfo;
    const char *mount = auth_user->mount;

    if (client == NULL)
        return -1;

    if (client->authenticated == 0)
    {
        /* auth failed so check to placing listeners elsewhere */
        if (auth_user->rejected_mount)
            mount = auth_user->rejected_mount;
        else if (auth->rejected_mount)
            mount = auth->rejected_mount;
        else
            return -1;
    }
    config = config_get_config();
    mountinfo = config_find_mount (config, mount);
    ret = add_authenticated_listener (mount, mountinfo, client);
    config_release_config();

    auth_user->client = NULL;

    return ret;
}


/* Decide whether we need to start a source or just process a source
 * admin request.
 */
void auth_postprocess_source (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    const char *mount = auth_user->mount;
    const char *req = httpp_getvar (client->parser, HTTPP_VAR_URI);

    auth_user->client = NULL;
    if (strcmp (req, "/admin.cgi") == 0 || strncmp ("/admin/metadata", req, 15) == 0)
    {
        DEBUG2 ("metadata request (%s, %s)", req, mount);
        admin_mount_request (client, "metadata");
    }
    else
    {
        DEBUG1 ("on mountpoint %s", mount);
        source_startup (client, mount, 0);
    }
}


/* Add a listener. Check for any mount information that states any
 * authentication to be used.
 */
void auth_add_listener (const char *mount, client_t *client)
{
    mount_proxy *mountinfo; 
    ice_config_t *config;

    /* we don't need any more data from the listener, just setup for writing */
    client->refbuf->len = PER_CLIENT_REFBUF_SIZE;

    if (connection_check_relay_pass (client->parser))
    {
        client->is_slave = 1;
        client->authenticated = 1;
        INFO0 ("client connected as slave");
    }
    config = config_get_config();
    mountinfo = config_find_mount (config, mount);
    if (mountinfo && mountinfo->no_mount)
    {
        config_release_config ();
        client_send_403 (client, "mountpoint unavailable");
        return;
    }
    if (client->authenticated == 0 && mountinfo && mountinfo->auth && mountinfo->auth->authenticate)
    {
        auth_client *auth_user;

        if (mountinfo->auth->pending_count > 1000)
        {
            config_release_config ();
            WARN0 ("too many clients awaiting authentication");
            client_send_403 (client, "busy, please try again later");
            return;
        }
        auth_user = auth_client_setup (mount, client);
        auth_user->process = auth_new_listener;
        INFO0 ("adding client for authentication");
        queue_auth_client (auth_user, mountinfo);
    }
    else
    {
        add_authenticated_listener (mount, mountinfo, client);
    }
    config_release_config ();
}


/* General listener client shutdown function. Here we free up the passed client but
 * if the client is authenticated and there's auth available then queue it.
 */
int auth_release_listener (client_t *client, const char *mount, mount_proxy *mountinfo)
{
    if (client->authenticated)
    {
        /* drop any queue reference here, we do not want a race between the source thread
         * and the auth/fserve thread */
        client_set_queue (client, NULL);

        if (mount && mountinfo && mountinfo->auth && mountinfo->auth->release_listener)
        {
            auth_client *auth_user = auth_client_setup (mount, client);
            auth_user->process = auth_remove_listener;
            queue_auth_client (auth_user, mountinfo);
            return 1;
        }
        client->authenticated = 0;
    }
    client_send_404 (client, NULL);
    return 0;
}


static int get_authenticator (auth_t *auth, config_options_t *options)
{
    if (auth->type == NULL)
    {
        WARN0 ("no authentication type defined");
        return -1;
    }
    do
    {
        DEBUG1 ("type is %s", auth->type);

        if (strcmp (auth->type, "url") == 0)
        {
#ifdef HAVE_AUTH_URL
            if (auth_get_url_auth (auth, options) < 0)
                return -1;
            break;
#else
            ERROR0 ("Auth URL disabled");
            return -1;
#endif
        }
        if (strcmp (auth->type, "command") == 0)
        {
#ifdef WIN32
            ERROR1("Authenticator type: \"%s\" not supported on win32 platform", auth->type);
            return -1;
#else
            if (auth_get_cmd_auth (auth, options) < 0)
                return -1;
            break;
#endif
        }
        if (strcmp (auth->type, "htpasswd") == 0)
        {
            if (auth_get_htpasswd_auth (auth, options) < 0)
                return -1;
            break;
        }

        ERROR1("Unrecognised authenticator type: \"%s\"", auth->type);
        return -1;
    } while (0);

    while (options)
    {
        if (strcmp (options->name, "allow_duplicate_users") == 0)
            auth->allow_duplicate_users = atoi (options->value);
        else if (strcmp(options->name, "realm") == 0)
            auth->realm = (char*)xmlStrdup (XMLSTR(options->value));
        else if (strcmp(options->name, "drop_existing_listener") == 0)
            auth->drop_existing_listener = atoi (options->value);
        else if (strcmp (options->name, "rejected_mount") == 0)
            auth->rejected_mount = (char*)xmlStrdup (XMLSTR(options->value));
        else if (strcmp(options->name, "handlers") == 0)
            auth->handlers = atoi (options->value);
        options = options->next;
    }
    if (auth->handlers < 1) auth->handlers = 1;
    if (auth->handlers > 20) auth->handlers = 20;
    return 0;
}


int auth_get_authenticator (xmlNodePtr node, void *x)
{
    auth_t *auth = calloc (1, sizeof (auth_t));
    config_options_t *options = NULL, **next_option = &options;
    xmlNodePtr option;
    int i;

    if (auth == NULL)
        return -1;

    option = node->xmlChildrenNode;
    while (option)
    {
        xmlNodePtr current = option;
        option = option->next;
        if (xmlStrcmp (current->name, XMLSTR("option")) == 0)
        {
            config_options_t *opt = calloc (1, sizeof (config_options_t));
            opt->name = (char *)xmlGetProp (current, XMLSTR("name"));
            if (opt->name == NULL)
            {
                free(opt);
                continue;
            }
            opt->value = (char *)xmlGetProp (current, XMLSTR("value"));
            if (opt->value == NULL)
            {
                xmlFree (opt->name);
                free (opt);
                continue;
            }
            *next_option = opt;
            next_option = &opt->next;
        }
        else
            if (xmlStrcmp (current->name, XMLSTR("text")) != 0)
                WARN1 ("unknown auth setting (%s)", current->name);
    }
    auth->type = (char *)xmlGetProp (node, XMLSTR("type"));
    if (get_authenticator (auth, options) < 0)
    {
        xmlFree (auth->type);
        free (auth);
        auth = NULL;
    }
    else
    {
        auth->tailp = &auth->head;
        thread_mutex_create ("auth_t", &auth->lock);

        /* allocate N threads */
        auth->handles = calloc (auth->handlers, sizeof (auth_thread_t));
        auth->refcount = 1;
        auth->running = 1;
        for (i=0; i<auth->handlers; i++)
        {
            if (auth->alloc_thread_data)
                auth->handles[i].data = auth->alloc_thread_data (auth);
            auth->handles[i].id = thread_id++;
            auth->handles[i].auth = auth;
        }
        *(auth_t**)x = auth;
    }

    while (options)
    {
        config_options_t *opt = options;
        options = opt->next;
        xmlFree (opt->name);
        xmlFree (opt->value);
        free (opt);
    }
    return 0;
}


/* Called when a source client connects and requires authentication via the
 * authenticator. This is called for both source clients and admin requests
 * that work on a specified mountpoint.
 */
int auth_stream_authenticate (client_t *client, const char *mount, mount_proxy *mountinfo)
{
    if (mountinfo && mountinfo->auth && mountinfo->auth->stream_auth)
    {
        auth_client *auth_user = auth_client_setup (mount, client);

        auth_user->process = stream_auth_callback;
        INFO1 ("request source auth for \"%s\"", mount);
        queue_auth_client (auth_user, mountinfo);
        return 1;
    }
    return 0;
}


/* called when the stream starts, so that authentication engine can do any
 * cleanup/initialisation.
 */
void auth_stream_start (mount_proxy *mountinfo, const char *mount)
{
    if (mountinfo && mountinfo->auth && mountinfo->auth->stream_start)
    {
        auth_client *auth_user = auth_client_setup (mount, NULL);
        auth_user->process = stream_start_callback;
        INFO1 ("request source start for \"%s\"", mount);

        queue_auth_client (auth_user, mountinfo);
    }
}


/* Called when the stream ends so that the authentication engine can do
 * any authentication cleanup
 */
void auth_stream_end (mount_proxy *mountinfo, const char *mount)
{
    if (mountinfo && mountinfo->auth && mountinfo->auth->stream_end)
    {
        auth_client *auth_user = auth_client_setup (mount, NULL);
        auth_user->process = stream_end_callback;
        INFO1 ("request source end for \"%s\"", mount);

        queue_auth_client (auth_user, mountinfo);
    }
}


/* these are called at server start and termination */

void auth_initialise (void)
{
    thread_spin_create ("auth lock", &auth_lock);
    thread_id = 0;
}

void auth_shutdown (void)
{
    thread_spin_destroy (&auth_lock);
    INFO0 ("Auth shutdown");
}

