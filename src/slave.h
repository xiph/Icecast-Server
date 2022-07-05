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
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __SLAVE_H__
#define __SLAVE_H__

#include "common/thread/thread.h"
#include "icecasttypes.h"
#include "cfgfile.h"

void slave_initialize(void);
void slave_shutdown(void);
void slave_update_all_mounts (void);
void slave_rebuild_mounts (void);
void relay_config_free (relay_config_t *relay);
relay_t *relay_free (relay_t *relay);

#endif  /* __SLAVE_H__ */
