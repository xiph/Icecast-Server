/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2019, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "auth.h"
#include "client.h"

#include "logging.h"
#define CATMODULE "auth_enforce_auth"

static auth_result enforce_auth_auth(auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (client->password)
        return AUTH_NOMATCH;

    return AUTH_FAILED;
}

int  auth_get_enforce_auth_auth(auth_t *authenticator, config_options_t *options)
{
    (void)options;
    authenticator->authenticate_client = enforce_auth_auth;
    authenticator->immediate = 1;
    return 0;
}
