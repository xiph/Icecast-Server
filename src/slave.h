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

#include "client.h"

void slave_initialize(void);
void slave_shutdown(void);
void slave_restart (void);
void slave_update_all_mounts (void);
void slave_rebuild_mounts (void);
relay_server *slave_find_relay (relay_server *relays, const char *mount);
int  redirect_client (const char *mountpoint, struct _client_tag *client);
void redirector_update (struct _client_tag *client);
relay_server *relay_free (relay_server *relay);

#endif  /* __SLAVE_H__ */
