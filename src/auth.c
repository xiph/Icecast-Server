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
        /* just add the client */
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


/* Check whether this client is currently on this mount, the client may be
 * on either the active or pending lists.
 * return 1 if ok to add or 0 to prevent
 */
static int check_duplicate_logins (source_t *source, client_t *client)
{
    auth_t *auth = source->authenticator;

    if (client->username == NULL)
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


/* try to place authenticated client on named source */
int auth_postprocess_client (const char *mount, client_t *client)
{
    int ret = -1;
    source_t *source;
    avl_tree_unlock (global.source_tree);
    source = source_find_mount (mount);
    if (source)
    {
        ret = 0;
        thread_mutex_lock (&source->lock);
        if ((source->running || source->on_demand )
                && check_duplicate_logins (source, client))
            add_authenticated_client (source, client);
        else
            ret = -1;
        thread_mutex_unlock (&source->lock);
    }
    avl_tree_unlock (global.source_tree);
    if (ret < 0)
        source_free_client (NULL, client);
    return ret;
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

