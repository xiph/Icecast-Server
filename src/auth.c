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


static void auth_postprocess_source (auth_client *auth_user);


static auth_client *auth_client_setup (const char *mount, client_t *client)
{
    /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
    const char *header = httpp_getvar(client->parser, "authorization");
    char *userpass, *tmp;
    char *username, *password;
    auth_client *auth_user;

    do
    {
        if (header == NULL)
            break;

        if (strncmp(header, "Basic ", 6) == 0)
        {
            userpass = util_base64_decode (header+6);
            if (userpass == NULL)
            {
                ICECAST_LOG_WARN("Base64 decode of Authorization header \"%s\" failed",
                        header+6);
                break;
            }

            tmp = strchr(userpass, ':');
            if (tmp == NULL)
            { 
                free (userpass);
                break;
            }

            *tmp = 0;
            username = userpass;
            password = tmp+1;
            client->username = strdup (username);
            client->password = strdup (password);
            free (userpass);
            break;
        }
        ICECAST_LOG_INFO("unhandled authorization header: %s", header);

    } while (0);

    auth_user = calloc (1, sizeof(auth_client));
    auth_user->mount = strdup (mount);
    auth_user->client = client;
    return auth_user;
}


static void queue_auth_client (auth_client *auth_user, mount_proxy *mountinfo)
{
    auth_t *auth;

    if (auth_user == NULL)
        return;
    auth_user->next = NULL;
    if (mountinfo)
    {
        auth = mountinfo->auth;
        thread_mutex_lock (&auth->lock);
        if (auth_user->client)
            auth_user->client->auth = auth;
        auth->refcount++;
    }
    else
    {
        if (auth_user->client == NULL || auth_user->client->auth == NULL)
        {
            ICECAST_LOG_WARN("internal state is incorrect for %p", auth_user->client);
            return;
        }
        auth = auth_user->client->auth;
        thread_mutex_lock (&auth->lock);
    }
    ICECAST_LOG_DEBUG("...refcount on auth_t %s is now %d", auth->mount, auth->refcount);
    *auth->tailp = auth_user;
    auth->tailp = &auth_user->next;
    auth->pending_count++;
    ICECAST_LOG_INFO("auth on %s has %d pending", auth->mount, auth->pending_count);
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
    ICECAST_LOG_DEBUG("...refcount on auth_t %s is now %d", authenticator->mount, authenticator->refcount);
    if (authenticator->refcount)
    {
        thread_mutex_unlock (&authenticator->lock);
        return;
    }

    /* cleanup auth thread attached to this auth */
    authenticator->running = 0;
    thread_join (authenticator->thread);

    if (authenticator->free)
        authenticator->free (authenticator);
    xmlFree (authenticator->type);
    thread_mutex_unlock (&authenticator->lock);
    thread_mutex_destroy (&authenticator->lock);
    if (authenticator->mount)
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
            client_send_401 (client);
        auth_user->client = NULL;
    }
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
static void auth_new_listener (auth_t *auth, auth_client *auth_user)
{
    client_t *client = auth_user->client;

    /* make sure there is still a client at this point, a slow backend request
     * can be avoided if client has disconnected */
    if (is_listener_connected (client) == 0)
    {
        ICECAST_LOG_DEBUG("listener is no longer connected");
        client->respcode = 400;
        auth_release (client->auth);
        client->auth = NULL;
        return;
    }
    if (auth->authenticate)
    {
        if (auth->authenticate (auth_user) != AUTH_OK)
        {
            auth_release (client->auth);
            client->auth = NULL;
            return;
        }
    }
    if (auth_postprocess_listener (auth_user) < 0)
    {
        auth_release (client->auth);
        client->auth = NULL;
        ICECAST_LOG_INFO("client %lu failed", client->con->id);
    }
}


/* wrapper function for auth thread to drop listener connections
 */
static void auth_remove_listener (auth_t *auth, auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (client->auth->release_listener)
        client->auth->release_listener (auth_user);
    auth_release (client->auth);
    client->auth = NULL;
    /* client is going, so auth is not an issue at this point */
    client->authenticated = 0;
}


/* Called from auth thread to process any request for source client
 * authentication. Only applies to source clients, not relays.
 */
static void stream_auth_callback (auth_t *auth, auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (auth->stream_auth)
        auth->stream_auth (auth_user);

    auth_release (auth);
    client->auth = NULL;
    if (client->authenticated)
        auth_postprocess_source (auth_user);
    else
        ICECAST_LOG_WARN("Failed auth for source \"%s\"", auth_user->mount);
}


/* Callback from auth thread to handle a stream start event, this applies
 * to both source clients and relays.
 */
static void stream_start_callback (auth_t *auth, auth_client *auth_user)
{
    if (auth->stream_start)
        auth->stream_start (auth_user);
    auth_release (auth);
}


/* Callback from auth thread to handle a stream start event, this applies
 * to both source clients and relays.
 */
static void stream_end_callback (auth_t *auth, auth_client *auth_user)
{
    if (auth->stream_end)
        auth->stream_end (auth_user);
    auth_release (auth);
}


/* The auth thread main loop. */
static void *auth_run_thread (void *arg)
{
    auth_t *auth = arg;

    ICECAST_LOG_INFO("Authentication thread started");
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
            ICECAST_LOG_DEBUG("%d client(s) pending on %s", auth->pending_count, auth->mount);
            auth->head = auth_user->next;
            if (auth->head == NULL)
                auth->tailp = &auth->head;
            auth->pending_count--;
            thread_mutex_unlock (&auth->lock);
            auth_user->next = NULL;

            if (auth_user->process)
                auth_user->process (auth, auth_user);
            else
                ICECAST_LOG_ERROR("client auth process not set");

            auth_client_free (auth_user);

            continue;
        }
        thread_sleep (150000);
    }
    ICECAST_LOG_INFO("Authenication thread shutting down");
    return NULL;
}


/* Check whether this client is currently on this mount, the client may be
 * on either the active or pending lists.
 * return 1 if ok to add or 0 to prevent
 */
static int check_duplicate_logins (source_t *source, client_t *client, auth_t *auth)
{
    /* allow multiple authenticated relays */
    if (client->username == NULL)
        return 1;

    if (auth && auth->allow_duplicate_users == 0)
    {
        avl_node *node;

        avl_tree_rlock (source->client_tree);
        node = avl_get_first (source->client_tree);
        while (node)
        {   
            client_t *existing_client = (client_t *)node->key;
            if (existing_client->username && 
                    strcmp (existing_client->username, client->username) == 0)
            {
                avl_tree_unlock (source->client_tree);
                return 0;
            }
            node = avl_get_next (node);
        }       
        avl_tree_unlock (source->client_tree);

        avl_tree_rlock (source->pending_tree);
        node = avl_get_first (source->pending_tree);
        while (node)
        {
            client_t *existing_client = (client_t *)node->key;
            if (existing_client->username && 
                    strcmp (existing_client->username, client->username) == 0)
            {
                avl_tree_unlock (source->pending_tree);
                return 0;
            }
            node = avl_get_next (node);
        }
        avl_tree_unlock (source->pending_tree);
    }
    return 1;
}


/* if 0 is returned then the client should not be touched, however if -1
 * is returned then the caller is responsible for handling the client
 */
static int add_listener_to_source (source_t *source, client_t *client)
{
    int loop = 10;
    do
    {
        ICECAST_LOG_DEBUG("max on %s is %ld (cur %lu)", source->mount,
                source->max_listeners, source->listeners);
        if (source->max_listeners == -1)
            break;
        if (source->listeners < (unsigned long)source->max_listeners)
            break;

        if (loop && source->fallback_when_full && source->fallback_mount)
        {
            source_t *next = source_find_mount (source->fallback_mount);
            if (!next) {
                ICECAST_LOG_ERROR("Fallback '%s' for full source '%s' not found", 
                        source->mount, source->fallback_mount);
                return -1;
            }

            ICECAST_LOG_INFO("stream full trying %s", next->mount);
            source = next;
            loop--;
            continue;
        }
        /* now we fail the client */
        return -1;

    } while (1);

    client->write_to_client = format_generic_write_to_client;
    client->check_buffer = format_check_http_buffer;
    client->refbuf->len = PER_CLIENT_REFBUF_SIZE;
    memset (client->refbuf->data, 0, PER_CLIENT_REFBUF_SIZE);

    /* lets add the client to the active list */
    avl_tree_wlock (source->pending_tree);
    avl_insert (source->pending_tree, client);
    avl_tree_unlock (source->pending_tree);

    if (source->running == 0 && source->on_demand)
    {
        /* enable on-demand relay to start, wake up the slave thread */
        ICECAST_LOG_DEBUG("kicking off on-demand relay");
        source->on_demand_req = 1;
    }
    ICECAST_LOG_DEBUG("Added client to %s", source->mount);
    return 0;
}


/* Add listener to the pending lists of either the  source or fserve thread.
 * This can be run from the connection or auth thread context
 */
static int add_authenticated_listener (const char *mount, mount_proxy *mountinfo, client_t *client)
{
    int ret = 0;
    source_t *source = NULL;

    client->authenticated = 1;

    /* Here we are parsing the URI request to see if the extension is .xsl, if
     * so, then process this request as an XSLT request
     */
    if (util_check_valid_extension (mount) == XSLT_CONTENT)
    {
        /* If the file exists, then transform it, otherwise, write a 404 */
        ICECAST_LOG_DEBUG("Stats request, sending XSL transformed stats");
        stats_transform_xslt (client, mount);
        return 0;
    }

    avl_tree_rlock (global.source_tree);
    source = source_find_mount (mount);

    if (source)
    {
        if (mountinfo)
        {
            if (check_duplicate_logins (source, client, mountinfo->auth) == 0)
            {
                avl_tree_unlock (global.source_tree);
                return -1;
            }

            /* set a per-mount disconnect time if auth hasn't set one already */
            if (mountinfo->max_listener_duration && client->con->discon_time == 0)
                client->con->discon_time = time(NULL) + mountinfo->max_listener_duration;
        }

        ret = add_listener_to_source (source, client);
        avl_tree_unlock (global.source_tree);
        if (ret == 0)
            ICECAST_LOG_DEBUG("client authenticated, passed to source");
    }
    else
    {
        avl_tree_unlock (global.source_tree);
        fserve_client_create (client, mount);
    }
    return ret;
}


int auth_postprocess_listener (auth_client *auth_user)
{
    int ret;
    client_t *client = auth_user->client;
    ice_config_t *config = config_get_config();

    mount_proxy *mountinfo = config_find_mount (config, auth_user->mount, MOUNT_TYPE_NORMAL);

    ret = add_authenticated_listener (auth_user->mount, mountinfo, client);
    config_release_config();

    if (ret < 0)
        client_send_401 (auth_user->client);
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
    client->authenticated = 1;
    if (strcmp (req, "/admin.cgi") == 0 || strncmp ("/admin/metadata", req, 15) == 0)
    {
        ICECAST_LOG_DEBUG("metadata request (%s, %s)", req, mount);
        admin_handle_request (client, "/admin/metadata");
    }
    else
    {
        ICECAST_LOG_DEBUG("on mountpoint %s", mount);
        source_startup (client, mount, 0);
    }
}


/* Add a listener. Check for any mount information that states any
 * authentication to be used.
 */
void auth_add_listener (const char *mount, client_t *client)
{
    mount_proxy *mountinfo; 
    ice_config_t *config = config_get_config();

    mountinfo = config_find_mount (config, mount, MOUNT_TYPE_NORMAL);
    if (mountinfo && mountinfo->no_mount)
    {
        config_release_config ();
        client_send_403 (client, "mountpoint unavailable");
        return;
    }
    if (mountinfo && mountinfo->auth)
    {
        auth_client *auth_user;

        if (mountinfo->auth->pending_count > 100)
        {
            config_release_config ();
            ICECAST_LOG_WARN("too many clients awaiting authentication");
            client_send_403 (client, "busy, please try again later");
            return;
        }
        auth_user = auth_client_setup (mount, client);
        auth_user->process = auth_new_listener;
        ICECAST_LOG_INFO("adding client for authentication");
        queue_auth_client (auth_user, mountinfo);
        config_release_config ();
    }
    else
    {
        int ret = add_authenticated_listener (mount, mountinfo, client);
        config_release_config ();
        if (ret < 0)
            client_send_403 (client, "max listeners reached");
    }
}


/* determine whether we need to process this client further. This
 * involves any auth exit, typically for external auth servers.
 */
int auth_release_listener (client_t *client)
{
    if (client->authenticated)
    {
        const char *mount = httpp_getvar (client->parser, HTTPP_VAR_URI);

        /* drop any queue reference here, we do not want a race between the source thread
         * and the auth/fserve thread */
        client_set_queue (client, NULL);

        if (mount && client->auth && client->auth->release_listener)
        {
            auth_client *auth_user = auth_client_setup (mount, client);
            auth_user->process = auth_remove_listener;
            queue_auth_client (auth_user, NULL);
            return 1;
        }
        client->authenticated = 0;
    }
    return 0;
}


static int get_authenticator (auth_t *auth, config_options_t *options)
{
    if (auth->type == NULL)
    {
        ICECAST_LOG_WARN("no authentication type defined");
        return -1;
    }
    do
    {
        ICECAST_LOG_DEBUG("type is %s", auth->type);

        if (strcmp (auth->type, "url") == 0)
        {
#ifdef HAVE_AUTH_URL
            if (auth_get_url_auth (auth, options) < 0)
                return -1;
            break;
#else
            ICECAST_LOG_ERROR("Auth URL disabled");
            return -1;
#endif
        }
        if (strcmp (auth->type, "htpasswd") == 0)
        {
            if (auth_get_htpasswd_auth (auth, options) < 0)
                return -1;
            break;
        }

        ICECAST_LOG_ERROR("Unrecognised authenticator type: \"%s\"", auth->type);
        return -1;
    } while (0);

    while (options)
    {
        if (strcmp (options->name, "allow_duplicate_users") == 0)
            auth->allow_duplicate_users = atoi ((char*)options->value);
        options = options->next;
    }
    return 0;
}


auth_t *auth_get_authenticator (xmlNodePtr node)
{
    auth_t *auth = calloc (1, sizeof (auth_t));
    config_options_t *options = NULL, **next_option = &options;
    xmlNodePtr option;

    if (auth == NULL)
        return NULL;

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
                ICECAST_LOG_WARN("unknown auth setting (%s)", current->name);
    }
    auth->type = (char*)xmlGetProp (node, XMLSTR("type"));
    if (get_authenticator (auth, options) < 0)
    {
        xmlFree (auth->type);
        free (auth);
        auth = NULL;
    }
    else
    {
        auth->tailp = &auth->head;
        thread_mutex_create (&auth->lock);
        auth->refcount = 1;
        auth->running = 1;
        auth->thread = thread_create ("auth thread", auth_run_thread, auth, THREAD_ATTACHED);
    }

    while (options)
    {
        config_options_t *opt = options;
        options = opt->next;
        xmlFree (opt->name);
        xmlFree (opt->value);
        free (opt);
    }
    return auth;
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
        ICECAST_LOG_INFO("request source auth for \"%s\"", mount);
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
        auth_client *auth_user = calloc (1, sizeof (auth_client));
        if (auth_user)
        {
            auth_user->mount = strdup (mount);
            auth_user->process = stream_start_callback;

            queue_auth_client (auth_user, mountinfo);
        }
    }
}


/* Called when the stream ends so that the authentication engine can do
 * any authentication cleanup
 */
void auth_stream_end (mount_proxy *mountinfo, const char *mount)
{
    if (mountinfo && mountinfo->auth && mountinfo->auth->stream_end)
    {
        auth_client *auth_user = calloc (1, sizeof (auth_client));
        if (auth_user)
        {
            auth_user->mount = strdup (mount);
            auth_user->process = stream_end_callback;

            queue_auth_client (auth_user, mountinfo);
        }
    }
}


/* these are called at server start and termination */

void auth_initialise (void)
{
}

void auth_shutdown (void)
{
    ICECAST_LOG_INFO("Auth shutdown");
}

