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

#include <ldap.h>

#include "auth.h"
#include "client.h"

#include "logging.h"
#define CATMODULE "auth_ldap"

typedef struct auth_ldap {
    char *uri;
    char *userprefix;
    char *usersuffix;
} auth_ldap_t;

static auth_result ldap_auth (auth_client *auth_user) {
    unsigned long version = LDAP_VERSION3;
    client_t *client = auth_user->client;
    auth_t *auth = client->auth;
    auth_ldap_t *auth_info = auth->state;
    LDAP *ld;
    struct berval cred;
    int err;
    size_t userlen;
    char *user;

    if (!client->username || !client->password)
        return AUTH_NOMATCH;

    userlen = strlen(auth_info->userprefix) + strlen(auth_info->usersuffix) + strlen(client->username) + 1;
    user = malloc(userlen);
    if (!user)
        return AUTH_FAILED;

    snprintf(user, userlen, "%s%s%s", auth_info->userprefix, client->username, auth_info->usersuffix);

    cred.bv_val = client->password;
    cred.bv_len = strlen(client->password);

    if (ldap_initialize(&ld, auth_info->uri) != LDAP_SUCCESS)
        return AUTH_FAILED;

    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, (void*)&version);

    err = ldap_sasl_bind_s(ld, user, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
    free(user);

    ldap_unbind_ext_s(ld, NULL, NULL);

    if (err == LDAP_SUCCESS)
        return AUTH_OK;
    return AUTH_FAILED;
}

static void clear_auth (auth_t *auth) {
    auth_ldap_t *auth_info = auth->state;
    if (auth_info->uri) free(auth_info->uri);
    free(auth_info->userprefix);
    free(auth_info->usersuffix);
    free(auth_info);
    auth->state = NULL;
}

int  auth_get_ldap_auth (auth_t *authenticator, config_options_t *options) {
    auth_ldap_t *auth_info;

    auth_info = calloc(1, sizeof(auth_ldap_t));
    if (!auth_info)
        return -1;

    authenticator->authenticate_client = ldap_auth;
    authenticator->free = clear_auth;
    authenticator->state = auth_info;

    while (options) {
        if (strcmp(options->name, "uri") == 0) {
            if (auth_info->uri) free(auth_info->uri);
            auth_info->uri = strdup(options->value);
        } else if (strcmp(options->name, "userprefix") == 0) {
            if (auth_info->userprefix) free(auth_info->userprefix);
            auth_info->userprefix = strdup(options->value);
        } else if (strcmp(options->name, "usersuffix") == 0) {
            if (auth_info->usersuffix) free(auth_info->usersuffix);
            auth_info->usersuffix = strdup(options->value);
        } else {
            ICECAST_LOG_ERROR("Unknown option: %s", options->name);
        }
        options = options->next;
    }

    if (!auth_info->userprefix) auth_info->userprefix = strdup("");
    if (!auth_info->usersuffix) auth_info->usersuffix = strdup("");

    return 0;
}
