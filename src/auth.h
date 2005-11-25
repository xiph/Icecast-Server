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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

struct source_tag;
struct auth_tag;

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "cfgfile.h"
#include "client.h"
#include "thread/thread.h"

typedef enum
{
    AUTH_UNDEFINED,
    AUTH_OK,
    AUTH_FAILED,
    AUTH_USERADDED,
    AUTH_USEREXISTS,
    AUTH_USERDELETED,
} auth_result;

typedef struct auth_client_tag
{
    char        *mount;
    client_t    *client;
    void        (*process)(struct auth_client_tag *auth_user);
    struct auth_client_tag *next;
} auth_client;


typedef struct auth_tag
{
    char *mount;

    /* Authenticate using the given username and password */
    auth_result (*authenticate)(auth_client *aclient);
    auth_result (*release_client)(auth_client *auth_user);

    /* callbacks to specific auth for notifying auth server on source
     * startup or shutdown
     */
    void (*stream_start)(auth_client *auth_user);
    void (*stream_end)(auth_client *auth_user);

    void (*free)(struct auth_tag *self);
    auth_result (*adduser)(struct auth_tag *auth, const char *username, const char *password);
    auth_result (*deleteuser)(struct auth_tag *auth, const char *username);
    auth_result (*listuser)(struct auth_tag *auth, xmlNodePtr srcnode);

    mutex_t lock;
    int refcount;
    int allow_duplicate_users;
    int drop_existing_listener;

    void *state;
    char *type;
    char *realm;
} auth_t;

void add_client (const char *mount, client_t *client);
int  release_client (client_t *client);

void auth_initialise ();
void auth_shutdown ();

auth_t  *auth_get_authenticator (xmlNodePtr node);
void    auth_release (auth_t *authenticator);

/* call to send a url request when source starts */
void auth_stream_start (struct _mount_proxy *mountinfo, const char *mount);

/* call to send a url request when source ends */
void auth_stream_end (struct _mount_proxy *mountinfo, const char *mount);

/* called from auth thread, after the client has successfully authenticated
 * and requires adding to source or fserve. */
int auth_postprocess_client (auth_client *auth_user);

#endif


