/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2023-2023, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __VERSION_H__
#define __VERSION_H__

#include "icecasttypes.h"

typedef struct {
    const char *name;
    const char *compiletime;
    const char *runtime;
} icecast_dependency_t;

const char * const * version_get_compiletime_flags(void);

const icecast_dependency_t * version_get_dependencies(void);

#endif  /* __VERSION_H__ */
