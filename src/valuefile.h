/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2024     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __VALUEFILE_H__
#define __VALUEFILE_H__

#include <stdbool.h>

#include "icecasttypes.h"

string_renderer_t * valuefile_export_database(void);

#endif
