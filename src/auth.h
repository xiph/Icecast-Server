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

#include "source.h"
#include "client.h"
#include "config.h"
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

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
    auth_result (*authenticate)(struct auth_tag *self, 
            char *username, char *password);
    void (*free)(struct auth_tag *self);
    void *state;
    void *type;
} auth_t;

auth_result auth_check_client(source_t *source, client_t *client);

auth_t *auth_get_authenticator(char *type, config_options_t *options);
void *auth_clear(auth_t *authenticator);
int auth_get_userlist(source_t *source, xmlNodePtr srcnode);
int auth_adduser(source_t *source, char *username, char *password);
int auth_deleteuser(source_t *source, char *username);

#endif


