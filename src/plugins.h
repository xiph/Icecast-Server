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
 * Copyright 2012,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __PLUGINS_H__
#define __PLUGINS_H__

#include "cfgfile.h"

void plugins_initialize(void);
void plugins_shutdown(void);
void plugins_load(ice_config_t * config);

#endif  /* __PLUGINS_H__ */
