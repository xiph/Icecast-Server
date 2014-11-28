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

#include "auth.h"
#include "client.h"

#include "logging.h"
#define CATMODULE "auth_anonymous"

static auth_result anonymous_auth (auth_client *auth_user) {
    return AUTH_OK;
}

int  auth_get_anonymous_auth (auth_t *authenticator, config_options_t *options) {
    authenticator->authenticate_client = anonymous_auth;
    return 0;
}
