/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __NAVIGATION_H__
#define __NAVIGATION_H__

#include "refobject.h"

REFOBJECT_FORWARD_TYPE(mount_identifier_t);

void navigation_initialize(void);
void navigation_shutdown(void);

mount_identifier_t * mount_identifier_new(const char *mount);

#endif
