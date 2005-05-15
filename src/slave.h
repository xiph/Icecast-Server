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

#ifndef __SLAVE_H__
#define __SLAVE_H__

#include <thread/thread.h>

struct _client_tag;

typedef struct _relay_server {
    char *server;
    int port;
    char *mount;
    char *username;
    char *password;
    char *localmount;
    struct source_tag *source;
    int mp3metadata;
    int on_demand;
    int running;
    int cleanup;
    thread_type *thread;
    struct _relay_server *next;
} relay_server;

typedef struct _slave_host
{
    char *server;
    int port;
    unsigned int count;
    struct _slave_host *next;
} slave_host;

void slave_initialize(void);
void slave_shutdown(void);
void slave_recheck_mounts (void);
void slave_rebuild_mounts (void);
int slave_redirect (const char *mountpoint, struct _client_tag *client);
void slave_host_add (struct _client_tag *client, const char *header);
void slave_host_remove (struct _client_tag *client);
relay_server *relay_free (relay_server *relay);

#endif  /* __SLAVE_H__ */
