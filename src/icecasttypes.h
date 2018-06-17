/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __ICECASTTYPES_H__
#define __ICECASTTYPES_H__

#include "compat.h"

/* ---[ client.[ch] ]--- */

typedef struct _client_tag client_t;

/* ---[ source.[ch] ]--- */

typedef struct source_tag source_t;

/* ---[ admin.[ch] ]--- */

/* Command IDs */
typedef int32_t admin_command_id_t;

/* formats */
typedef enum {
    ADMIN_FORMAT_AUTO,
    ADMIN_FORMAT_RAW,
    ADMIN_FORMAT_TRANSFORMED,
    ADMIN_FORMAT_PLAINTEXT
} admin_format_t;

/* ---[ acl.[ch] ]--- */

typedef struct acl_tag acl_t;

/* ---[ auth.[ch] ]--- */

typedef struct auth_tag auth_t;

/* ---[ cfgfile.[ch] ]--- */

typedef struct ice_config_tag ice_config_t;

#endif
