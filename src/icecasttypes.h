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

/* ---[ connection.[ch] ]--- */

typedef struct connection_tag connection_t;

typedef enum {
    /* no TLS is used at all */
    ICECAST_TLSMODE_DISABLED = 0,
    /* TLS mode is to be detected */
    ICECAST_TLSMODE_AUTO,
    /* Like ICECAST_TLSMODE_AUTO but enforces TLS */
    ICECAST_TLSMODE_AUTO_NO_PLAIN,
    /* TLS via HTTP Upgrade:-header [RFC2817] */
    ICECAST_TLSMODE_RFC2817,
    /* TLS for transport layer like HTTPS [RFC2818] does */
    ICECAST_TLSMODE_RFC2818
} tlsmode_t;

/* ---[ slave.[ch] ]--- */

typedef struct _relay_server relay_server;

#endif
