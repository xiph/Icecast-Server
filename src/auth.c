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
#include "md5.h"

#include "logging.h"
#define CATMODULE "auth"



auth_result auth_check_client(source_t *source, client_t *client)
{
    auth_t *authenticator = source->authenticator;
    auth_result result;

    if (client->is_slave == 0 && authenticator) {
        /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
        char *header = httpp_getvar(client->parser, "authorization");
        char *userpass, *tmp;
        char *username, *password;

        if(header == NULL)
            return AUTH_FAILED;

        if(strncmp(header, "Basic ", 6)) {
            INFO0("Authorization not using Basic");
            return 0;
        }

        userpass = util_base64_decode(header+6);
        if(userpass == NULL) {
            WARN1("Base64 decode of Authorization header \"%s\" failed",
                    header+6);
            return AUTH_FAILED;
        }

        tmp = strchr(userpass, ':');
        if(!tmp) { 
            free(userpass);
            return AUTH_FAILED;
        }

        *tmp = 0;
        username = userpass;
        password = tmp+1;

        client->username = strdup (username);
        client->password = strdup (password);

        result = authenticator->authenticate (source, client);

        free(userpass);

        return result;
    }
    else
    {
        /* just add the client, this shouldn't fail */
        add_authenticated_client (source, client);
        return AUTH_OK;
    }
}


void auth_clear(auth_t *authenticator)
{
    if (authenticator == NULL)
        return;
    authenticator->free (authenticator);
    free (authenticator->type);
    free (authenticator);
}


auth_t *auth_get_authenticator(const char *type, config_options_t *options)
{
    auth_t *auth = NULL;
#ifdef HAVE_AUTH_URL
    if(!strcmp(type, "url")) {
        auth = auth_get_url_auth(options);
        auth->type = strdup(type);
    }
    else
#endif
    if(!strcmp(type, "command")) {
#ifdef WIN32
        ERROR1("Authenticator type: \"%s\" not supported on win32 platform", type);
        return NULL;
#else
        auth = auth_get_cmd_auth(options);
        auth->type = strdup(type);
#endif
    }
    else if(!strcmp(type, "htpasswd")) {
        auth = auth_get_htpasswd_auth(options);
        auth->type = strdup(type);
    }
    else {
        ERROR1("Unrecognised authenticator type: \"%s\"", type);
        return NULL;
    }

    if (auth)
    {
        while (options)
        {
            if (!strcmp(options->name, "allow_duplicate_users"))
                auth->allow_duplicate_users = atoi(options->value);
            options = options->next;
        }
        return auth;
    }
    ERROR1("Couldn't configure authenticator of type \"%s\"", type);
    return NULL;
}


/* This should be called after auth has completed. After the user has
 * been authenticated the client is ready to be placed on the source, but
 * may still be dropped.
 * Don't use client after this call, however a return of 0 indicates that
 * client is on the source whereas -1 means that the client has failed
 */
int auth_postprocess_client (const char *mount, client_t *client)
{
    int ret = -1;
    source_t *source;
    avl_tree_rlock (global.source_tree);
    source = source_find_mount (mount);
    if (source)
    {
        thread_mutex_lock (&source->lock);

        if (source->running || source->on_demand)
            ret = add_authenticated_client (source, client);

        thread_mutex_unlock (&source->lock);
    }
    avl_tree_unlock (global.source_tree);
    return ret;
}


void auth_failed_client (const char *mount)
{
    source_t *source;

    avl_tree_rlock (global.source_tree);
    source = source_find_mount (mount);
    if (source)
    {
        thread_mutex_lock (&source->lock);

        if (source->new_listeners)
            source->new_listeners--;

        thread_mutex_unlock (&source->lock);
    }
    avl_tree_unlock (global.source_tree);
}


void auth_close_client (client_t *client)
{
    /* failed client, drop global count */
    global_lock();
    global.clients--;
    global_unlock();
    if (client->respcode)
        client_destroy (client);
    else
        client_send_401 (client);
}


/* Check whether this client is currently on this mount, the client may be
 * on either the active or pending lists.
 * return 1 if ok to add or 0 to prevent
 */
static int check_duplicate_logins (source_t *source, client_t *client)
{
    auth_t *auth = source->authenticator;

    /* allow multiple authenticated relays */
    if (client->username == NULL || client->is_slave)
        return 1;

    if (auth && auth->allow_duplicate_users == 0)
    {
        client_t *existing;

        existing = source->active_clients;
        while (existing)
        {
            if (existing->username && strcmp (existing->username, client->username) == 0)
                return 0;
            existing = existing->next;
        }
        existing = source->pending_clients;
        while (existing)
        {
            if (existing->username && strcmp (existing->username, client->username) == 0)
                return 0;
            existing = existing->next;
        }
    }
    return 1;
}


/* The actual add client routine, this requires the source to be locked.
 * if 0 is returned then the client should not be touched, however if -1
 * is returned then the caller is responsible for handling the client
 */
int add_authenticated_client (source_t *source, client_t *client)
{
    if (source->authenticator && check_duplicate_logins (source, client) == 0)
        return -1;
    /* lets add the client to the pending list */
    client->next = source->pending_clients;
    source->pending_clients = client;

    client->write_to_client = format_http_write_to_client;
    client->refbuf = refbuf_new (4096);

    sock_set_blocking (client->con->sock, SOCK_NONBLOCK);
    sock_set_nodelay (client->con->sock);
    if (source->running == 0 && source->on_demand)
    {
        /* enable on-demand relay to start, wake up the slave thread */
        DEBUG0("kicking off on-demand relay");
        source->on_demand_req = 1;
        slave_rescan();
    }
    DEBUG1 ("Added client to pending on %s", source->mount);
    stats_event_inc (NULL, "clients");
    return 0;
}


/* try to add client to a pending list.  return
 *  0 for success
 *  -1 too many clients
 *  -2 mount needs authentication
 *  -3 mount is unavailable
 */
static int _add_client (char *passed_mount, client_t *client, int initial_connection)
{
    source_t *source;
    char *mount = passed_mount;
    
    while (1)
    {
        source = source_find_mount (mount);
        if (passed_mount != mount) 
            free (mount);
        if (source == NULL)
            return -3;
        if (initial_connection && source->no_mount
                && strcmp (source->mount, passed_mount) == 0)
            return -3;
        thread_mutex_lock (&source->lock);

        if (source->running || source->on_demand)
        {
            DEBUG2 ("max on %s is %d", source->mount, source->max_listeners);
            DEBUG2 ("pending %d, current %d", source->new_listeners, source->listeners);
            if (source->max_listeners == -1)
                break;
            if (client->is_slave)
                break;
            if (source->new_listeners + source->listeners < (unsigned int)source->max_listeners)
                break;

            INFO2 ("max listeners (%d) reached on %s", source->max_listeners, source->mount);
            if (source->fallback_when_full == 0 || source->fallback_mount == NULL)
            {
                thread_mutex_unlock (&source->lock);
                return -1;
            }
            if (source->fallback_mount)
                mount = strdup (source->fallback_mount);
            else
                mount = NULL;
        }

        thread_mutex_unlock (&source->lock);
    }

    if (auth_check_client (source, client) != AUTH_OK)
    {
        thread_mutex_unlock (&source->lock);
        INFO0 ("listener failed to authenticate");
        return -2;
    }
    source->new_listeners++;

    thread_mutex_unlock (&source->lock);
    return 0;
}


void add_client (char *mount, client_t *client)
{
    int added = -3;

    if (mount)
    {
        if (connection_check_relay_pass(client->parser))
        {
            client_as_slave (client);
            INFO0 ("client connected as slave");
        }
        thread_mutex_lock (&move_clients_mutex);
        avl_tree_rlock (global.source_tree);
        added = _add_client (mount, client, 1);
        avl_tree_unlock (global.source_tree);
        thread_mutex_unlock (&move_clients_mutex);
    }
    switch (added)
    {
    case -1: 
        /* there may be slaves we can re-direct to */
        if (slave_redirect (mount, client))
            break;
        client_send_404 (client,
                "Too many clients on this mountpoint. Try again later.");
        DEBUG1 ("max clients on %s", mount);
        break;
    case -2:
        client_send_401 (client);
        break;
    case -3:
        client_send_404 (client, "The file you requested could not be found");
        break;
    default:
        return;
    }
    /* failed client, drop global count */
    global_lock();
    global.clients--;
    global_unlock();
}

