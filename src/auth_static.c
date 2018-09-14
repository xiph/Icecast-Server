/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* for strcmp() and strdup() */
#include <string.h>

#include "auth.h"
#include "cfgfile.h"
#include "client.h"
#include "util.h"

#include "logging.h"
#define CATMODULE "auth_static"

typedef struct auth_static {
    char *username;
    char *password;
    auth_alter_t action;
    char *arg;
} auth_static_t;

static auth_result static_auth(auth_client *auth_user)
{
    client_t        *client     = auth_user->client;
    auth_t          *auth       = client->auth;
    auth_static_t   *auth_info  = auth->state;

    if (auth_info->username) {
        if (!client->username)
            return AUTH_NOMATCH;
        if (strcmp(auth_info->username, client->username) != 0)
            return AUTH_NOMATCH;
    }

    if (!client->password)
        return AUTH_NOMATCH;

    if (strcmp(auth_info->password, client->password) != 0)
        return AUTH_FAILED;


    if (auth_info->action != AUTH_ALTER_NOOP) {
        if (auth_alter_client(auth, auth_user, auth_info->action, auth_info->arg) != 0) {
            ICECAST_LOG_ERROR("Can not alter client.");
        }
    }

    return AUTH_OK;
}

static void clear_auth (auth_t *auth)
{
    auth_static_t *auth_info = auth->state;
    if (!auth_info)
        return;

    free(auth_info->username);
    free(auth_info->password);
    free(auth_info->arg);
    free(auth_info);
    auth->state = NULL;
}

static auth_result static_userlist(auth_t *auth, xmlNodePtr srcnode)
{
    auth_static_t *auth_info = auth->state;
    xmlNodePtr    newnode;

    newnode = xmlNewChild(srcnode, NULL, XMLSTR("user"), NULL);
    xmlNewTextChild(newnode, NULL, XMLSTR("username"), XMLSTR(auth_info->username));

    return AUTH_OK;
}

int  auth_get_static_auth (auth_t *authenticator, config_options_t *options)
{
    auth_static_t *auth_info;
    int           need_user;

    if (strcmp(authenticator->type, AUTH_TYPE_STATIC) == 0) {
        need_user = 1;
    } else if (strcmp(authenticator->type, AUTH_TYPE_LEGACY_PASSWORD) == 0) {
        need_user = 0;
    } else {
        ICECAST_LOG_ERROR("Unknown type.");
        return -1;
    }

    auth_info = calloc(1, sizeof(auth_static_t));
    if (!auth_info)
        return -1;

    authenticator->authenticate_client = static_auth;
    authenticator->listuser = static_userlist;
    authenticator->free = clear_auth;
    authenticator->state = auth_info;
    authenticator->immediate = 1;

    while (options) {
        if (strcmp(options->name, "username") == 0) {
            if (auth_info->username)
                free(auth_info->username);
            auth_info->username = strdup(options->value);
        } else if (strcmp(options->name, "password") == 0) {
            if (auth_info->password)
                free(auth_info->password);
            auth_info->password = strdup(options->value);
        } else if (strcmp(options->name, "action") == 0) {
            auth_info->action = auth_str2alter(options->value);
            if (auth_info->action == AUTH_ALTER_NOOP) {
                ICECAST_LOG_ERROR("Invalid action given.");
                clear_auth(authenticator);
                return -1;
            }
        } else if (strcmp(options->name, "argument") == 0) {
            replace_string(&(auth_info->arg), options->value);
        } else {
            ICECAST_LOG_ERROR("Unknown option: %s", options->name);
        }
        options = options->next;
    }

    if (need_user && !auth_info->username) {
        ICECAST_LOG_ERROR("Username required, but not given.");
        clear_auth(authenticator);
        return -1;
    } else if (!auth_info->password) {
        ICECAST_LOG_ERROR("Password required, but not given.");
        clear_auth(authenticator);
        return -1;
    }

    return 0;
}
