/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __ICECASTTYPES_H__
#define __ICECASTTYPES_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "compat.h"

/* ---[ * ]--- */
/* XML namespaces */
#define XMLNS_REPORTXML         "http://icecast.org/specs/reportxml-0.0.1"
#define XMLNS_XSPF              "http://xspf.org/ns/0/"
#define XMLNS_LEGACY_STATS      "http://icecast.org/specs/legacystats-0.0.1"
#define XMLNS_LEGACY_RESPONSE   "http://icecast.org/specs/legacyresponse-0.0.1"

typedef enum {
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_ERROR = 2
} health_t;

#define health_atbest(a, b) (((a) > (b)) ? (a) : (b))

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
    ADMIN_FORMAT_HTML,
    ADMIN_FORMAT_PLAINTEXT,
    ADMIN_FORMAT_JSON
} admin_format_t;

/* ---[ acl.[ch] ]--- */

typedef struct acl_tag acl_t;

/* ---[ auth.[ch] ]--- */

typedef struct auth_tag auth_t;
typedef struct auth_stack_tag auth_stack_t;

/* ---[ cfgfile.[ch] ]--- */

typedef struct ice_config_tag ice_config_t;

typedef struct _config_options config_options_t;

typedef enum _operation_mode {
    /* Default operation mode. may depend on context */
    OMODE_DEFAULT = 0,
    /* The normal mode. */
    OMODE_NORMAL,
    /* Mimic some of the behavior of older versions.
     * This mode should only be used in transition to normal mode,
     * e.g. to give some clients time to upgrade to new API.
     */
    OMODE_LEGACY,
    /* The struct mode includes some behavior for future versions
     * that can for some reason not yet be used in the normal mode
     * e.g. because it may break interfaces in some way.
     * New applications should test against this mode and developer
     * of software interacting with Icecast on an API level should
     * have a look for strict mode behavior to avoid random breakage
     * with newer versions of Icecast.
     */
    OMODE_STRICT
} operation_mode;

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

typedef struct relay_tag relay_t;

/* ---[ module.[ch] ]--- */

typedef struct module_tag module_t;

typedef struct module_container_tag module_container_t;

/* ---[ reportxml.[ch] ]--- */

typedef struct reportxml_tag reportxml_t;
typedef struct reportxml_node_tag reportxml_node_t;
typedef struct reportxml_database_tag reportxml_database_t;

/* ---[ listensocket.[ch] ]--- */

typedef struct listensocket_container_tag listensocket_container_t;
typedef struct listensocket_tag listensocket_t;

/* ---[ digest.[ch] ]--- */

typedef struct digest_tag digest_t;
typedef struct hmac_tag hmac_t;

/* ---[ navigation.[ch] ]--- */

typedef struct mount_identifier_tag mount_identifier_t;

/* ---[ string_renderer.[ch] ]--- */

typedef struct string_renderer_tag string_renderer_t;

/* ---[ event.[ch] ]--- */

typedef struct event_tag event_t;
typedef struct event_registration_tag event_registration_t;

/* ---[ refobject.[ch] ]--- */

typedef struct refobject_base_tag refobject_base_t;

#ifdef HAVE_TYPE_ATTRIBUTE_TRANSPARENT_UNION
typedef union __attribute__ ((__transparent_union__)) {
    refobject_base_t *refobject_base;
    reportxml_t *reportxml;
    reportxml_node_t *reportxml_node;
    reportxml_database_t *reportxml_database;
    listensocket_container_t *listensocket_container;
    listensocket_t *listensocket;
    digest_t *digest;
    hmac_t *hmac;
    mount_identifier_t *mount_identifier;
} refobject_t;
#else
typedef void * refobject_t;
#endif

/* --- [ For libigloo ]--- */
#define igloo_RO_APPTYPES \
    igloo_RO_TYPE(string_renderer_t) \
    igloo_RO_TYPE(event_t) \
    igloo_RO_TYPE(event_registration_t) \
    igloo_RO_TYPE(module_t) \
    igloo_RO_TYPE(module_container_t)

#endif
