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
    AUTH_USERDELETED
} auth_result;

typedef struct auth_client_tag
{
    char        *mount;
    char        *hostname;
    int         port;
    int         handler;
    client_t    *client;
    struct auth_tag *auth;
    char        *rejected_mount;
    void        *thread_data;
    void        (*process)(struct auth_client_tag *auth_user);
    struct auth_client_tag *next;
} auth_client;


typedef struct _auth_thread_t
{
    thread_type *thread;
    void *data;
    unsigned int id;
    struct auth_tag *auth;
} auth_thread_t;

typedef struct auth_tag
{
    char *mount;

    /* Authenticate using the given username and password */
    auth_result (*authenticate)(auth_client *aclient);
    auth_result (*release_listener)(auth_client *auth_user);

    /* auth handler for authenicating a connecting source client */
    void (*stream_auth)(auth_client *auth_user);

    /* auth handler for source startup, no client passed as it may disappear */
    void (*stream_start)(auth_client *auth_user);

    /* auth handler for source exit, no client passed as it may disappear */
    void (*stream_end)(auth_client *auth_user);

    /* auth state-specific free call */
    void (*free)(struct auth_tag *self);

    /* call to allocate any per auth thread data */
    void *(*alloc_thread_data)(struct auth_tag *self);

    /* call to freeup any per auth thread data */
    void (*release_thread_data)(struct auth_tag *self, void *data);

    auth_result (*adduser)(struct auth_tag *auth, const char *username, const char *password);
    auth_result (*deleteuser)(struct auth_tag *auth, const char *username);
    auth_result (*listuser)(struct auth_tag *auth, xmlNodePtr srcnode);

    mutex_t lock;
    int running;
    int refcount;
    int allow_duplicate_users;
    int drop_existing_listener;
    int handlers;

    /* mountpoint to send unauthenticated listeners */
    char *rejected_mount;

    /* runtime allocated array of thread handlers for this auth */
    auth_thread_t *handles;

    /* per-auth queue for clients */
    auth_client *head, **tailp;
    int pending_count;

    void *state;
    char *type;
    char *realm;
} auth_t;

void auth_add_listener (const char *mount, client_t *client);
int  auth_release_listener (client_t *client, const char *mount, struct _mount_proxy *mountinfo);

void auth_initialise (void);
void auth_shutdown (void);

int auth_get_authenticator (xmlNodePtr node, void *x);
void    auth_release (auth_t *authenticator);

/* call to send a url request when source starts */
void auth_stream_start (struct _mount_proxy *mountinfo, const char *mount);

/* call to send a url request when source ends */
void auth_stream_end (struct _mount_proxy *mountinfo, const char *mount);

/* */
int auth_stream_authenticate (client_t *client, const char *mount,
        struct _mount_proxy *mountinfo);

/* called from auth thread */
void auth_postprocess_source (auth_client *auth_user);

void auth_check_http (client_t *client);

#endif


