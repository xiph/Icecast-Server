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

#ifndef __ADMIN_H__
#define __ADMIN_H__

#include "client.h"

int  admin_handle_request (client_t *client, const char *uri);
void admin_mount_request (client_t *client, const char *uri);
void admin_source_listeners (source_t *source, xmlNodePtr node);

#endif  /* __ADMIN_H__ */
