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

#ifndef __AUTH_H__
#define __AUTH_H__

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "source.h"
#include "client.h"

typedef enum
{
    AUTH_OK,
    AUTH_FAILED,
    AUTH_USERADDED,
    AUTH_USEREXISTS,
    AUTH_USERDELETED,
} auth_result;

typedef struct auth_tag
{
    /* Authenticate using the given username and password */
    auth_result (*authenticate)(source_t *source, client_t *client);
    void (*free)(struct auth_tag *self);
    auth_result (*adduser)(struct auth_tag *auth, const char *username, const char *password);
    auth_result (*deleteuser)(struct auth_tag *auth, const char *username);
    auth_result (*listuser)(struct auth_tag *auth, xmlNodePtr srcnode);
    void (*release_client)(struct source_tag *source, client_t *client);
    int (*checkuser)(source_t *source, client_t *client);

    int allow_duplicate_users;

    void *state;
    void *type;
} auth_t;

auth_result auth_check_client(source_t *source, client_t *client);

auth_t *auth_get_authenticator(const char *type, config_options_t *options);
void auth_clear(auth_t *authenticator);
int auth_postprocess_client (const char *mount, client_t *client);
void auth_close_client (client_t *client);

#endif


