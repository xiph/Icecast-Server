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
 * Copyright 2011,      Dave 'justdave' Miller <justdave@mozilla.com>.
 * Copyright 2011-2014, Thomas B. "dm8tbr" Ruecker <thomas@ruecker.fi>,
 * Copyright 2011-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fnmatch.h>
#endif
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "common/thread/thread.h"

#include "cfgfile.h"
#include "global.h"
#include "logging.h"
#include "util.h"
#include "auth.h"
#include "event.h"
#include "refobject.h"
#include "reportxml.h"

/* for config_reread_config() */
#include "yp.h"
#include "fserve.h"
#include "stats.h"
#include "connection.h"
#include "main.h"
#include "slave.h"
#include "xslt.h"
#include "prng.h"

#define CATMODULE                       "CONFIG"
#define CONFIG_DEFAULT_LOCATION         "Earth"
#define CONFIG_DEFAULT_ADMIN            "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT     256
#define CONFIG_DEFAULT_SOURCE_LIMIT     16
#define CONFIG_DEFAULT_QUEUE_SIZE_LIMIT (500*1024)
#define CONFIG_DEFAULT_BODY_SIZE_LIMIT  (4*1024)
#define CONFIG_DEFAULT_BURST_SIZE       (64*1024)
#define CONFIG_DEFAULT_THREADPOOL_SIZE  4
#define CONFIG_DEFAULT_CLIENT_TIMEOUT   30
#define CONFIG_DEFAULT_HEADER_TIMEOUT   15
#define CONFIG_DEFAULT_SOURCE_TIMEOUT   10
#define CONFIG_DEFAULT_BODY_TIMEOUT     (10 + CONFIG_DEFAULT_HEADER_TIMEOUT)
#define CONFIG_DEFAULT_MASTER_USERNAME  "relay"
#define CONFIG_DEFAULT_SHOUTCAST_MOUNT  "/stream"
#define CONFIG_DEFAULT_SHOUTCAST_USER   "source"
#define CONFIG_DEFAULT_FILESERVE        1
#define CONFIG_DEFAULT_HOSTNAME         "localhost"
#define CONFIG_DEFAULT_PLAYLIST_LOG     NULL
#define CONFIG_DEFAULT_ACCESS_LOG       "access.log"
#define CONFIG_DEFAULT_ERROR_LOG        "error.log"
#define CONFIG_DEFAULT_LOG_LEVEL        ICECAST_LOGLEVEL_INFO
#define CONFIG_DEFAULT_LOG_LINES_KEPT   64
#define CONFIG_DEFAULT_CHROOT           0
#define CONFIG_DEFAULT_CHUID            0
#define CONFIG_DEFAULT_USER             NULL
#define CONFIG_DEFAULT_GROUP            NULL
#define CONFIG_MASTER_UPDATE_INTERVAL   120
#define CONFIG_YP_URL_TIMEOUT           10
#define CONFIG_DEFAULT_RELAY_SERVER     "127.0.0.1"
#define CONFIG_DEFAULT_RELAY_PORT       80
#define CONFIG_DEFAULT_RELAY_MOUNT      "/"
#define CONFIG_DEFAULT_CIPHER_LIST      "ECDHE-ECDSA-CHACHA20-POLY1305:" \
                                        "ECDHE-RSA-CHACHA20-POLY1305:" \
                                        "ECDHE-ECDSA-AES128-GCM-SHA256:" \
                                        "ECDHE-RSA-AES128-GCM-SHA256:" \
                                        "ECDHE-ECDSA-AES256-GCM-SHA384:" \
                                        "ECDHE-RSA-AES256-GCM-SHA384:" \
                                        "DHE-RSA-AES128-GCM-SHA256:" \
                                        "DHE-RSA-AES256-GCM-SHA384:" \
                                        "ECDHE-ECDSA-AES128-SHA256:" \
                                        "ECDHE-RSA-AES128-SHA256:" \
                                        "ECDHE-ECDSA-AES128-SHA:" \
                                        "ECDHE-RSA-AES256-SHA384:" \
                                        "ECDHE-RSA-AES128-SHA:" \
                                        "ECDHE-ECDSA-AES256-SHA384:" \
                                        "ECDHE-ECDSA-AES256-SHA:" \
                                        "ECDHE-RSA-AES256-SHA:" \
                                        "DHE-RSA-AES128-SHA256:" \
                                        "DHE-RSA-AES128-SHA:" \
                                        "DHE-RSA-AES256-SHA256:" \
                                        "DHE-RSA-AES256-SHA:" \
                                        "ECDHE-ECDSA-DES-CBC3-SHA:" \
                                        "ECDHE-RSA-DES-CBC3-SHA:" \
                                        "EDH-RSA-DES-CBC3-SHA:" \
                                        "AES128-GCM-SHA256:" \
                                        "AES256-GCM-SHA384:" \
                                        "AES128-SHA256:" \
                                        "AES256-SHA256:" \
                                        "AES128-SHA:" \
                                        "AES256-SHA:" \
                                        "DES-CBC3-SHA:" \
                                        "!DSS:" \
                                        "!aNULL:!eNULL:" \
                                        "!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:" \
                                        "!EDH-DSS-DES-CBC3-SHA:" \
                                        "!EDH-RSA-DES-CBC3-SHA:" \
                                        "!KRB5-DES-CBC3-SHA"

#ifndef _WIN32
#define CONFIG_DEFAULT_BASE_DIR         "/usr/local/icecast"
#define CONFIG_DEFAULT_LOG_DIR          "/usr/local/icecast/logs"
#define CONFIG_DEFAULT_WEBROOT_DIR      "/usr/local/icecast/webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR    "/usr/local/icecast/admin"
#define CONFIG_DEFAULT_NULL_FILE        "/dev/null"
#define MIMETYPESFILE                   "/etc/mime.types"
#else
#define CONFIG_DEFAULT_BASE_DIR         ".\\"
#define CONFIG_DEFAULT_LOG_DIR          ".\\logs"
#define CONFIG_DEFAULT_WEBROOT_DIR      ".\\webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR    ".\\admin"
#define CONFIG_DEFAULT_NULL_FILE        "nul:"
#define MIMETYPESFILE                   ".\\mime.types"
#endif

/* Legacy values. */
#define CONFIG_LEGACY_ALL_METHODS           "get,options"

#define CONFIG_LEGACY_SOURCE_NAME_GLOBAL    "legacy-global-source"
#define CONFIG_LEGACY_SOURCE_NAME_MOUNT     "legacy-mount-source"
#define CONFIG_LEGACY_SOURCE_METHODS        CONFIG_LEGACY_ALL_METHODS ",post,source,put,delete"
#define CONFIG_LEGACY_SOURCE_ALLOW_WEB      0
#define CONFIG_LEGACY_SOURCE_ALLOW_ADMIN    "*"

#define CONFIG_LEGACY_ADMIN_NAME            "legacy-admin"
#define CONFIG_LEGACY_ADMIN_METHODS         CONFIG_LEGACY_ALL_METHODS ",post,head,stats,delete"
#define CONFIG_LEGACY_ADMIN_ALLOW_WEB       1
#define CONFIG_LEGACY_ADMIN_ALLOW_ADMIN     "*"

#define CONFIG_LEGACY_RELAY_NAME            "legacy-relay"
#define CONFIG_LEGACY_RELAY_METHODS         CONFIG_LEGACY_ALL_METHODS
#define CONFIG_LEGACY_RELAY_ALLOW_WEB       1
#define CONFIG_LEGACY_RELAY_ALLOW_ADMIN     "streamlist.txt"

#define CONFIG_LEGACY_ANONYMOUS_NAME        "anonymous"
#define CONFIG_LEGACY_ANONYMOUS_METHODS     CONFIG_LEGACY_ALL_METHODS ",post,head"
#define CONFIG_LEGACY_ANONYMOUS_ALLOW_WEB   1
#define CONFIG_LEGACY_ANONYMOUS_ALLOW_ADMIN NULL

enum bad_tag_reason {
    BTR_UNKNOWN,
    BTR_OBSOLETE,
    BTR_INVALID
};

static ice_config_t _current_configuration;
static ice_config_locks _locks;

static void __found_bad_tag(ice_config_t *configuration, xmlNodePtr node, enum bad_tag_reason reason, const char *extra);
static void _set_defaults(ice_config_t *c);
static void _parse_root(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_limits(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_oldstyle_directory(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_yp_directory(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_paths(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_logging(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_security(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);

static void _parse_authentication(xmlDocPtr                 doc,
                                  xmlNodePtr                node,
                                  ice_config_t             *c,
                                  char                    **source_password);

static void _parse_relay(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c, const char *mount);
static void _parse_mount(xmlDocPtr doc, xmlNodePtr parentnode, ice_config_t *c);

static void _parse_listen_socket(xmlDocPtr                  doc,
                                 xmlNodePtr                 node,
                                 ice_config_t              *c);

static void _parse_events(event_registration_t **events, xmlNodePtr node);

static void merge_mounts(mount_proxy * dst, mount_proxy * src);
static inline void _merge_mounts_all(ice_config_t *c);

operation_mode config_str_to_omode(ice_config_t *configuration, xmlNodePtr node, const char *str)
{
    if (!str || !*str)
        return OMODE_DEFAULT;
    if (strcasecmp(str, "default") == 0) {
        return OMODE_DEFAULT;
    } else if (strcasecmp(str, "normal") == 0) {
        return OMODE_NORMAL;
    } else if (strcasecmp(str, "legacy-compat") == 0 || strcasecmp(str, "legacy") == 0) {
        return OMODE_LEGACY;
    } else if (strcasecmp(str, "strict") == 0) {
        return OMODE_STRICT;
    } else {
        __found_bad_tag(configuration, node, BTR_INVALID, str);
        ICECAST_LOG_ERROR("Unknown operation mode \"%s\", falling back to DEFAULT.", str);
        return OMODE_DEFAULT;
    }
}

static listener_type_t config_str_to_listener_type(ice_config_t *configuration, xmlNodePtr node, const char *str)
{
    if (!str || !*str) {
        return LISTENER_TYPE_NORMAL;
    } else if (strcasecmp(str, "normal") == 0) {
        return LISTENER_TYPE_NORMAL;
    } else if (strcasecmp(str, "virtual") == 0) {
        return LISTENER_TYPE_VIRTUAL;
    } else {
        __found_bad_tag(configuration, node, BTR_INVALID, str);
        ICECAST_LOG_ERROR("Unknown listener type \"%s\", falling back to NORMAL.", str);
        return LISTENER_TYPE_NORMAL;
    }
}

static fallback_override_t config_str_to_fallback_override_t(ice_config_t *configuration, xmlNodePtr node, const char *str)
{
    if (!str || !*str || strcmp(str, "none") == 0) {
        return FALLBACK_OVERRIDE_NONE;
    } else if (strcasecmp(str, "all") == 0) {
        return FALLBACK_OVERRIDE_ALL;
    } else if (strcasecmp(str, "own") == 0) {
        return FALLBACK_OVERRIDE_OWN;
    } else {
        if (util_str_to_bool(str)) {
            ICECAST_LOG_WARN("Old style fallback override setting. Please replace %#H with \"all\".", str);
            return FALLBACK_OVERRIDE_ALL;
        } else {
            ICECAST_LOG_WARN("Old style fallback override setting. Please replace %#H with \"none\".", str);
            return FALLBACK_OVERRIDE_NONE;
        }
    }
}

char * config_href_to_id(ice_config_t *configuration, xmlNodePtr node, const char *href)
{
    if (!href || !*href)
        return NULL;

    if (*href != '#') {
        __found_bad_tag(configuration, node, BTR_INVALID, href);
        ICECAST_LOG_ERROR("Can not convert string \"%H\" to ID.", href);
        return NULL;
    }

    return strdup(href+1);
}

static void create_locks(void)
{
    thread_mutex_create(&_locks.relay_lock);
    thread_rwlock_create(&_locks.config_lock);
}

static void release_locks(void)
{
    thread_mutex_destroy(&_locks.relay_lock);
    thread_rwlock_destroy(&_locks.config_lock);
}

void config_initialize(void)
{
    create_locks();
}

void config_shutdown(void)
{
    config_get_config();
    config_clear(&_current_configuration);
    config_release_config();
    release_locks();
}

void config_init_configuration(ice_config_t *configuration)
{
    memset(configuration, 0, sizeof(ice_config_t));
    _set_defaults(configuration);
    configuration->reportxml_db = refobject_new(reportxml_database_t);
}

static inline void __read_int(xmlDocPtr doc, xmlNodePtr node, int *val, const char *warning)
{
    char *str = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!str || !*str) {
        ICECAST_LOG_WARN("%s", warning);
    } else {
        *val = util_str_to_int(str, *val);
    }
    if (str)
        xmlFree(str);
}

static inline void __read_unsigned_int(xmlDocPtr doc, xmlNodePtr node, unsigned int *val, const char *warning)
{
    char *str = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!str || !*str) {
        ICECAST_LOG_WARN("%s", warning);
    } else {
        *val = util_str_to_unsigned_int(str, *val);
    }
    if (str)
        xmlFree(str);
}         

static inline int __parse_public(const char *str)
{
    /* values that are not bool */
    if (strcasecmp(str, "client") == 0)
        return -1;

    /* old way of doing so */
    if (strcmp(str, "-1") == 0)
        return -1;

    /* ok, only normal bool left! */
    return util_str_to_bool(str);
}

/* This converts TLS mode strings to (tlsmode_t).
 * In older versions of Icecast2 this was just a bool.
 * So we need to handle boolean values as well.
 * See also: util_str_to_bool().
 */
static tlsmode_t str_to_tlsmode(const char *str) {
    /* consider NULL and empty strings as auto mode */
    if (!str || !*str)
        return ICECAST_TLSMODE_AUTO;

    if (strcasecmp(str, "disabled") == 0) {
        return ICECAST_TLSMODE_DISABLED;
    } else if (strcasecmp(str, "auto") == 0) {
        return ICECAST_TLSMODE_AUTO;
    } else if (strcasecmp(str, "auto_no_plain") == 0) {
        return ICECAST_TLSMODE_AUTO_NO_PLAIN;
    } else if (strcasecmp(str, "rfc2817") == 0) {
        return ICECAST_TLSMODE_RFC2817;
    } else if (strcasecmp(str, "rfc2818") == 0 ||
               /* boolean-style values */
               strcasecmp(str, "true") == 0 ||
               strcasecmp(str, "yes")  == 0 ||
               strcasecmp(str, "on")   == 0 ) {
        return ICECAST_TLSMODE_RFC2818;
    }

    /* old style numbers: consider everyting non-zero RFC2818 */
    if (atoi(str))
        return ICECAST_TLSMODE_RFC2818;

    /* we default to auto mode */
    return ICECAST_TLSMODE_AUTO;
}

/* This checks for the TLS implementation of a node */
static int __check_node_impl(xmlNodePtr node, const char *def)
{
    char *impl;
    int res;

    impl = (char *)xmlGetProp(node, XMLSTR("implementation"));
    if (!impl)
        impl = (char *)xmlGetProp(node, XMLSTR("impl"));
    if (!impl)
        impl = (char *)xmlStrdup(XMLSTR(def));

    res = tls_check_impl(impl);

    xmlFree(impl);

    return res;
}

static char *__build_node_name(xmlNodePtr node)
{
    char *buf[4];
    size_t have;
    size_t i;
    size_t len = 4;
    char *ret;
    char *p;

    memset(buf, 0, sizeof(buf));

    for (have = 0; have < (sizeof(buf)/sizeof(*buf)); have++) {
        int ret = -1;
        xmlChar *id;

        id = xmlGetProp(node, XMLSTR("id"));
        if (id) {
            ret = asprintf(&(buf[have]), "%s[@id=\"%s\"]", node->name, id);
            xmlFree(id);
        } else if (xmlStrcmp(node->name, XMLSTR("mount")) == 0) {
            xmlChar *mount = NULL;
            xmlNodePtr child = node->xmlChildrenNode;
            while (child && !mount) {
                if (xmlStrcmp(child->name, XMLSTR("mount-name")) == 0) {
                    mount = xmlNodeListGetString(child->doc, child->xmlChildrenNode, 1);
                }
                child = child->next;
            }
            if (mount) {
                ret = asprintf(&(buf[have]), "%s[mount-name/text()=\"%s\"]", node->name, mount);
                xmlFree(mount);
            } else {
                ret = asprintf(&(buf[have]), "%s", node->name);
            }
        } else {
            ret = asprintf(&(buf[have]), "%s", node->name);
        }

        if (ret < 1) {
            buf[have] = NULL;
            for (i = 0; i < (sizeof(buf)/sizeof(*buf)); i++)
                free(buf[i]);
            return "<error>";
        }

        node = node->parent;
        if (!node)
            break;
    }

    for (i = 0; i < (sizeof(buf)/sizeof(*buf)); i++) {
        if (buf[i])
            len += strlen(buf[i]) + 1;
    }

    p = ret = malloc(len);
    if (!ret) {
        for (i = 0; i < (sizeof(buf)/sizeof(*buf)); i++)
            free(buf[i]);
        return "<error>";
    }

    if (node && node->type != XML_DOCUMENT_NODE) {
        memcpy(p, "...", 3);
        p += 3;
    }

    for (i = have; i > 0; i--) {
        char *b = buf[i - 1];

        if (b) {
            size_t l = strlen(b);

            *p = '/';
            p++;
            memcpy(p, b, l);
            p += l;
            free(b);
        }
    }

    *p = 0;

    return ret;
}

static void __found_bad_tag(ice_config_t *configuration, xmlNodePtr node, enum bad_tag_reason reason, const char *extra)
{
    char *name = NULL;

    /* ignore non-configuration errors */
    if (!configuration)
        return;

    // ignore comments.
    if (node->type == XML_COMMENT_NODE)
        return;

    if (node)
        name = __build_node_name(node);

    switch (reason) {
        case BTR_UNKNOWN:
            configuration->config_problems |= CONFIG_PROBLEM_UNKNOWN_NODE;
            if (name) {
                ICECAST_LOG_WARN("Unknown tag in config: %s", name);
            } else {
                ICECAST_LOG_WARN("Unknown tag in config");
            }
        break;
        case BTR_OBSOLETE:
            configuration->config_problems |= CONFIG_PROBLEM_OBSOLETE_NODE;
            if (name) {
                ICECAST_LOG_WARN("Obsolete tag in config: %s", name);
                if (extra) {
                    ICECAST_LOG_WARN("Obsolete tag %s can be replaced: %s", name, extra);
                }
            } else {
                ICECAST_LOG_WARN("Obsolete tag in config");
            }
        break;
        case BTR_INVALID:
            configuration->config_problems |= CONFIG_PROBLEM_INVALID_NODE;
            if (name) {
                if (extra) {
                    ICECAST_LOG_WARN("Invalid content for tag: %s: %s", name, extra);
                } else {
                    ICECAST_LOG_WARN("Invalid content for tag: %s", name);
                }
            } else {
                ICECAST_LOG_WARN("Invalid content for tag");
            }
        break;
    }

    free(name);
}

static void __append_old_style_auth(auth_stack_t       **stack,
                                    const char          *name,
                                    const char          *type,
                                    const char          *username,
                                    const char          *password,
                                    const char          *match_method,
                                    const char          *allow_method,
                                    int                  allow_web,
                                    const char          *allow_admin)
{
    xmlNodePtr  role,
                user,
                pass;
    auth_t     *auth;

    if (!type)
        return;

    role = xmlNewNode(NULL, XMLSTR("role"));

    xmlSetProp(role, XMLSTR("type"), XMLSTR(type));
    xmlSetProp(role, XMLSTR("deny-method"), XMLSTR("*"));
    if (allow_method)
        xmlSetProp(role, XMLSTR("allow-method"), XMLSTR(allow_method));

    if (name)
        xmlSetProp(role, XMLSTR("name"), XMLSTR(name));

    if (match_method)
        xmlSetProp(role, XMLSTR("match-method"), XMLSTR(match_method));

    if (allow_web) {
        xmlSetProp(role, XMLSTR("allow-web"), XMLSTR("*"));
    } else {
        xmlSetProp(role, XMLSTR("deny-web"), XMLSTR("*"));
    }

    if (allow_admin && strcmp(allow_admin, "*") == 0) {
        xmlSetProp(role, XMLSTR("allow-admin"), XMLSTR("*"));
    } else {
        xmlSetProp(role, XMLSTR("deny-admin"), XMLSTR("*"));
        if (allow_admin)
            xmlSetProp(role, XMLSTR("allow-admin"), XMLSTR(allow_admin));
    }

    if (username) {
        user = xmlNewChild(role, NULL, XMLSTR("option"), NULL);
        xmlSetProp(user, XMLSTR("name"), XMLSTR("username"));
        xmlSetProp(user, XMLSTR("value"), XMLSTR(username));
    }
    if (password) {
        pass = xmlNewChild(role, NULL, XMLSTR("option"), NULL);
        xmlSetProp(pass, XMLSTR("name"), XMLSTR("password"));
        xmlSetProp(pass, XMLSTR("value"), XMLSTR(password));
    }

    auth = auth_get_authenticator(role);
    auth_stack_push(stack, auth);
    auth_release(auth);

    xmlFreeNode(role);
}

static void __append_option_tag(xmlNodePtr  parent,
                                const char *name,
                                const char *value)
{
    xmlNodePtr node;

    if (!name || !value)
        return;

    node = xmlNewChild(parent, NULL, XMLSTR("option"), NULL);
    xmlSetProp(node, XMLSTR("name"), XMLSTR(name));
    xmlSetProp(node, XMLSTR("value"), XMLSTR(value));
}

static void __append_old_style_urlauth(auth_stack_t **stack,
                                       const char    *client_add,
                                       const char    *client_remove,
                                       const char    *action_add,
                                       const char    *action_remove,
                                       const char    *username,
                                       const char    *password,
                                       int            is_source,
                                       const char    *auth_header,
                                       const char    *timelimit_header,
                                       const char    *headers,
                                       const char    *header_prefix)
{
    xmlNodePtr   role;
    auth_t      *auth;

    if (!stack || (!client_add && !client_remove))
        return;

    role = xmlNewNode(NULL, XMLSTR("role"));

    xmlSetProp(role, XMLSTR("type"), XMLSTR("url"));

    if (is_source) {
        xmlSetProp(role, XMLSTR("method"), XMLSTR("source,put"));
        xmlSetProp(role, XMLSTR("deny-method"), XMLSTR("*"));
        xmlSetProp(role, XMLSTR("allow-method"), XMLSTR("source,put"));
        xmlSetProp(role, XMLSTR("allow-web"), XMLSTR("*"));
        xmlSetProp(role, XMLSTR("allow-admin"), XMLSTR("*"));
    } else {
        xmlSetProp(role, XMLSTR("method"), XMLSTR("get,post,head,options"));
        xmlSetProp(role, XMLSTR("deny-method"), XMLSTR("*"));
        xmlSetProp(role, XMLSTR("allow-method"), XMLSTR("get,post,head,options"));
        xmlSetProp(role, XMLSTR("allow-web"), XMLSTR("*"));
        xmlSetProp(role, XMLSTR("deny-admin"), XMLSTR("*"));
    }

    __append_option_tag(role, "client_add", client_add);
    __append_option_tag(role, "client_remove", client_remove);
    __append_option_tag(role, "action_add", action_add);
    __append_option_tag(role, "action_remove", action_remove);
    __append_option_tag(role, "username", username);
    __append_option_tag(role, "password", password);
    __append_option_tag(role, "auth_header", auth_header);
    __append_option_tag(role, "timelimit_header", timelimit_header);
    __append_option_tag(role, "headers", headers);
    __append_option_tag(role, "header_prefix", header_prefix);

    auth = auth_get_authenticator(role);
    if (auth) {
        auth_stack_push(stack, auth);
        auth_release(auth);
        ICECAST_LOG_DEBUG("Pushed authenticator %p on stack %p.", auth, stack);
    } else {
        ICECAST_LOG_DEBUG("Failed to set up authenticator.");
    }

    xmlFreeNode(role);
}

static void __append_old_style_exec_event(event_registration_t **list,
                                          const char            *trigger,
                                          const char            *executable)
{
    xmlNodePtr            exec;
    event_registration_t *er;

    exec = xmlNewNode(NULL, XMLSTR("event"));

    xmlSetProp(exec, XMLSTR("type"), XMLSTR("exec"));
    xmlSetProp(exec, XMLSTR("trigger"), XMLSTR(trigger));

    __append_option_tag(exec, "executable", executable);

    er = event_new_from_xml_node(exec);
    event_registration_push(list, er);
    event_registration_release(er);

    xmlFreeNode(exec);
}

static void __append_old_style_url_event(event_registration_t   **list,
                                         const char              *trigger,
                                         const char              *url,
                                         const char              *action,
                                         const char              *username,
                                         const char              *password)
{
    xmlNodePtr            exec;
    event_registration_t *er;

    exec = xmlNewNode(NULL, XMLSTR("event"));

    xmlSetProp(exec, XMLSTR("type"), XMLSTR("url"));
    xmlSetProp(exec, XMLSTR("trigger"), XMLSTR(trigger));

    __append_option_tag(exec, "url", url);
    __append_option_tag(exec, "action", action);
    __append_option_tag(exec, "username", username);
    __append_option_tag(exec, "password", password);

    er = event_new_from_xml_node(exec);
    event_registration_push(list, er);
    event_registration_release(er);

    xmlFreeNode(exec);
}

void config_clear_http_header(ice_config_http_header_t *header)
{
    ice_config_http_header_t *old;

    while (header) {
        xmlFree(header->name);
        if (header->value)
            xmlFree(header->value);
        old = header;
        header = header->next;
        free(old);
    }
}

static inline ice_config_http_header_t *config_copy_http_header(ice_config_http_header_t *header)
{
    ice_config_http_header_t *ret = NULL;
    ice_config_http_header_t *cur = NULL;
    ice_config_http_header_t *old = NULL;

    while (header) {
        if (cur) {
            cur->next = calloc(1, sizeof(ice_config_http_header_t));
            old = cur;
            cur = cur->next;
        } else {
            ret = calloc(1, sizeof(ice_config_http_header_t));
            cur = ret;
        }

        if (!cur)
            return ret; /* TODO: do better error handling */

        cur->type   = header->type;
        cur->name   = (char *)xmlCharStrdup(header->name);
        cur->value  = (char *)xmlCharStrdup(header->value);
        cur->status = header->status;

        if (!cur->name || !cur->value) {
            if (cur->name)
                xmlFree(cur->name);
            if (cur->value)
                xmlFree(cur->value);
            if (old) {
                old->next = NULL;
            } else {
                ret = NULL;
            }
            free(cur);
            return ret;
        }
        header = header->next;
    }
    return ret;
}

static void config_clear_mount(mount_proxy *mount)
{
    if (mount->mountname)           xmlFree(mount->mountname);
    if (mount->dumpfile)            xmlFree(mount->dumpfile);
    if (mount->intro_filename)      xmlFree(mount->intro_filename);
    if (mount->fallback_mount)      xmlFree(mount->fallback_mount);
    if (mount->stream_name)         xmlFree(mount->stream_name);
    if (mount->stream_description)  xmlFree(mount->stream_description);
    if (mount->stream_url)          xmlFree(mount->stream_url);
    if (mount->stream_genre)        xmlFree(mount->stream_genre);
    if (mount->bitrate)             xmlFree(mount->bitrate);
    if (mount->type)                xmlFree(mount->type);
    if (mount->charset)             xmlFree(mount->charset);
    if (mount->cluster_password)    xmlFree(mount->cluster_password);
    if (mount->authstack)           auth_stack_release(mount->authstack);

    event_registration_release(mount->event);
    config_clear_http_header(mount->http_headers);
    free(mount);
}

static void config_clear_resource(resource_t *resource)
{
    resource_t *nextresource;

    while (resource) {
        nextresource = resource->next;
        xmlFree(resource->source);
        xmlFree(resource->destination);
        xmlFree(resource->bind_address);
        xmlFree(resource->vhost);
        xmlFree(resource->module);
        xmlFree(resource->handler);
        free(resource->listen_socket);
        free(resource);
        resource = nextresource;
    }
}

static void config_clear_yp_directories(yp_directory_t *yp_dir)
{
    yp_directory_t *next_yp_dir;

    while (yp_dir) {
        next_yp_dir = yp_dir->next;
        free(yp_dir->url);
        free(yp_dir->listen_socket_id);
        free(yp_dir);
        yp_dir = next_yp_dir;
    }
}

listener_t *config_clear_listener(listener_t *listener)
{
    listener_t *next = NULL;
    if (listener)
    {
        next = listener->next;
        if (listener->id)               xmlFree(listener->id);
        if (listener->on_behalf_of)     free(listener->on_behalf_of);
        if (listener->bind_address)     xmlFree(listener->bind_address);
        if (listener->shoutcast_mount)  xmlFree(listener->shoutcast_mount);
        if (listener->authstack)        auth_stack_release(listener->authstack);
        if (listener->http_headers)     config_clear_http_header(listener->http_headers);
        free (listener);
    }
    return next;
}

static void config_clear_prng_seed(prng_seed_config_t *seed)
{
    while (seed) {
        prng_seed_config_t *next = seed->next;
        if (seed->filename) xmlFree(seed->filename);
        seed = next;
    }
}

void config_clear(ice_config_t *c)
{
    mount_proxy         *mount,
                        *nextmount;
    size_t              i;

    free(c->config_filename);

    xmlFree(c->server_id);
    if (c->location)        xmlFree(c->location);
    if (c->admin)           xmlFree(c->admin);
    if (c->hostname)        xmlFree(c->hostname);
    if (c->base_dir)        xmlFree(c->base_dir);
    if (c->log_dir)         xmlFree(c->log_dir);
    if (c->webroot_dir)     xmlFree(c->webroot_dir);
    if (c->adminroot_dir)   xmlFree(c->adminroot_dir);
    if (c->null_device)     xmlFree(c->null_device);
    if (c->pidfile)         xmlFree(c->pidfile);
    if (c->banfile)         xmlFree(c->banfile);
    if (c->allowfile)       xmlFree(c->allowfile);
    if (c->playlist_log)    xmlFree(c->playlist_log);
    if (c->access_log)      xmlFree(c->access_log);
    if (c->error_log)       xmlFree(c->error_log);
    if (c->shoutcast_mount) xmlFree(c->shoutcast_mount);
    if (c->shoutcast_user)  xmlFree(c->shoutcast_user);
    if (c->authstack)       auth_stack_release(c->authstack);
    if (c->master_server)   xmlFree(c->master_server);
    if (c->master_username) xmlFree(c->master_username);
    if (c->master_password) xmlFree(c->master_password);
    if (c->user)            xmlFree(c->user);
    if (c->group)           xmlFree(c->group);
    if (c->mimetypes_fn)    xmlFree(c->mimetypes_fn);

    if (c->tls_context.cert_file)       xmlFree(c->tls_context.cert_file);
    if (c->tls_context.key_file)        xmlFree(c->tls_context.key_file);
    if (c->tls_context.cipher_list)     xmlFree(c->tls_context.cipher_list);

    event_registration_release(c->event);

    while ((c->listen_sock = config_clear_listener(c->listen_sock)));

    thread_mutex_lock(&(_locks.relay_lock));
    for (i = 0; i < c->relay_length; i++) {
        relay_config_free(c->relay[i]);
    }
    free(c->relay);
    thread_mutex_unlock(&(_locks.relay_lock));

    mount = c->mounts;
    while (mount) {
        nextmount = mount->next;
        config_clear_mount(mount);
        mount = nextmount;
    }

    config_clear_resource(c->resources);

#ifdef USE_YP
    config_clear_yp_directories(c->yp_directories);
#endif

    config_clear_http_header(c->http_headers);

    refobject_unref(c->reportxml_db);

    config_clear_prng_seed(c->prng_seed);

    memset(c, 0, sizeof(ice_config_t));
}

void config_reread_config(void)
{
    int           ret;
    ice_config_t *config;
    ice_config_t  new_config;
    /* reread config file */

    config = config_grab_config(); /* Both to get the lock, and to be able
                                     to find out the config filename */
    xmlSetGenericErrorFunc("config", log_parse_failure);
    ret = config_parse_file(config->config_filename, &new_config);
    if(ret < 0) {
        ICECAST_LOG_ERROR("Error parsing config, not replacing existing config");
        switch (ret) {
            case CONFIG_EINSANE:
                ICECAST_LOG_ERROR("Config filename null or blank");
            break;
            case CONFIG_ENOROOT:
                ICECAST_LOG_ERROR("Root element not found in %s", config->config_filename);
            break;
            case CONFIG_EBADROOT:
                ICECAST_LOG_ERROR("Not an icecast2 config file: %s",
                        config->config_filename);
            break;
            default:
                ICECAST_LOG_ERROR("Parse error in reading %s", config->config_filename);
            break;
        }
        config_release_config();
    } else {
        config_clear(config);
        config_set_config(&new_config);
        config = config_get_config_unlocked();
        restart_logging(config);
        prng_configure(config);
        main_config_reload(config);
        connection_reread_config(config);
        yp_recheck_config(config);
        fserve_recheck_mime_types(config);
        stats_global(config);
        config_release_config();
        slave_update_all_mounts();
        xslt_clear_cache();
    }
}

int config_initial_parse_file(const char *filename)
{
    /* Since we're already pointing at it, we don't need to copy it in place */
    return config_parse_file(filename, &_current_configuration);
}

int config_parse_file(const char *filename, ice_config_t *configuration)
{
    xmlDocPtr  doc;
    xmlNodePtr node;

    if (filename == NULL || strcmp(filename, "") == 0)
        return CONFIG_EINSANE;

    doc = xmlParseFile(filename);
    if (doc == NULL)
        return CONFIG_EPARSE;
    node = xmlDocGetRootElement(doc);
    if (node == NULL) {
        xmlFreeDoc(doc);
        return CONFIG_ENOROOT;
    }

    if (xmlStrcmp(node->name, XMLSTR("icecast")) != 0) {
        xmlFreeDoc(doc);
        return CONFIG_EBADROOT;
    }

    config_init_configuration(configuration);
    configuration->config_filename = strdup(filename);
    _parse_root(doc, node->xmlChildrenNode, configuration);
    xmlFreeDoc(doc);
    _merge_mounts_all(configuration);
    return 0;
}

int config_parse_cmdline(int arg, char **argv)
{
    return 0;
}

ice_config_locks *config_locks(void)
{
    return &_locks;
}

void config_release_config(void)
{
    thread_rwlock_unlock(&(_locks.config_lock));
}

ice_config_t *config_get_config(void)
{
    thread_rwlock_rlock(&(_locks.config_lock));
    return &_current_configuration;
}

ice_config_t *config_grab_config(void)
{
    thread_rwlock_wlock(&(_locks.config_lock));
    return &_current_configuration;
}

/* MUST be called with the lock held! */
void config_set_config(ice_config_t *config)
{
    memcpy(&_current_configuration, config, sizeof(ice_config_t));
}

ice_config_t *config_get_config_unlocked(void)
{
    return &_current_configuration;
}

static void _set_defaults(ice_config_t *configuration)
{
    configuration
        ->location = (char *) xmlCharStrdup(CONFIG_DEFAULT_LOCATION);
    configuration
        ->server_id = (char *) xmlCharStrdup(ICECAST_VERSION_STRING);
    configuration
        ->admin = (char *) xmlCharStrdup(CONFIG_DEFAULT_ADMIN);
    configuration
        ->client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
    configuration
        ->source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
    configuration
        ->queue_size_limit = CONFIG_DEFAULT_QUEUE_SIZE_LIMIT;
    configuration
        ->body_size_limit = CONFIG_DEFAULT_BODY_SIZE_LIMIT;
    configuration
        ->client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    configuration
        ->header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
    configuration
        ->source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
    configuration
        ->body_timeout = CONFIG_DEFAULT_BODY_TIMEOUT;
    configuration
        ->shoutcast_mount = (char *) xmlCharStrdup(CONFIG_DEFAULT_SHOUTCAST_MOUNT);
    configuration
        ->shoutcast_user = (char *) xmlCharStrdup(CONFIG_DEFAULT_SHOUTCAST_USER);
    configuration
        ->fileserve  = CONFIG_DEFAULT_FILESERVE;
    configuration
        ->on_demand = 0;
    configuration
        ->hostname = (char *) xmlCharStrdup(CONFIG_DEFAULT_HOSTNAME);
    configuration
        ->mimetypes_fn = (char *) xmlCharStrdup(MIMETYPESFILE);
    configuration
        ->master_server = NULL;
    configuration
        ->master_server_port = 0;
    configuration
        ->master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
    configuration
        ->master_username = (char *) xmlCharStrdup(CONFIG_DEFAULT_MASTER_USERNAME);
    configuration
        ->master_password = NULL;
    configuration
        ->base_dir = (char *) xmlCharStrdup(CONFIG_DEFAULT_BASE_DIR);
    configuration
        ->log_dir = (char *) xmlCharStrdup(CONFIG_DEFAULT_LOG_DIR);
    configuration
        ->null_device = (char *) xmlCharStrdup(CONFIG_DEFAULT_NULL_FILE);
    configuration
        ->webroot_dir = (char *) xmlCharStrdup(CONFIG_DEFAULT_WEBROOT_DIR);
    configuration
        ->adminroot_dir = (char *) xmlCharStrdup(CONFIG_DEFAULT_ADMINROOT_DIR);
    configuration
        ->playlist_log = (char *) xmlCharStrdup(CONFIG_DEFAULT_PLAYLIST_LOG);
    configuration
        ->access_log = (char *) xmlCharStrdup(CONFIG_DEFAULT_ACCESS_LOG);
    configuration
        ->error_log = (char *) xmlCharStrdup(CONFIG_DEFAULT_ERROR_LOG);
    configuration
        ->loglevel = CONFIG_DEFAULT_LOG_LEVEL;
    configuration
        ->playlist_log_lines_kept = CONFIG_DEFAULT_LOG_LINES_KEPT;
    configuration
        ->access_log_lines_kept = CONFIG_DEFAULT_LOG_LINES_KEPT;
    configuration
        ->error_log_lines_kept = CONFIG_DEFAULT_LOG_LINES_KEPT;
    configuration
        ->chroot = CONFIG_DEFAULT_CHROOT;
    configuration
        ->chuid = CONFIG_DEFAULT_CHUID;
    configuration
        ->user = NULL;
    configuration
        ->group = NULL;
    /* default to a typical prebuffer size used by clients */
    configuration
        ->burst_size = CONFIG_DEFAULT_BURST_SIZE;
    configuration->tls_context
        .cipher_list = (char *) xmlCharStrdup(CONFIG_DEFAULT_CIPHER_LIST);
}

static inline void __check_hostname(ice_config_t *configuration)
{
    int sane_hostname = 0;
    char *p;

    /* ensure we have a non-NULL buffer: */
    if (!configuration->hostname)
        configuration->hostname = (char *)xmlCharStrdup(CONFIG_DEFAULT_HOSTNAME);

    /* convert to lower case: */
    for (p = configuration->hostname; *p; p++) {
        if ( *p >= 'A' && *p <= 'Z' )
            *p += 'a' - 'A';
    }

    switch (util_hostcheck(configuration->hostname)) {
        case HOSTCHECK_SANE:
            sane_hostname = 1;
        break;
        case HOSTCHECK_ERROR:
            ICECAST_LOG_ERROR("Can not check hostname \"%s\".",
                configuration->hostname);
        break;
        case HOSTCHECK_NOT_FQDN:
            ICECAST_LOG_WARN("Warning, <hostname> seems not to be set to a "
                "fully qualified domain name (FQDN). This may cause problems, "
                "e.g. with YP directory listings.");
        break;
        case HOSTCHECK_IS_LOCALHOST:
            ICECAST_LOG_WARN("Warning, <hostname> not configured, using "
                "default value \"%s\". This will cause problems, e.g. "
                "this breaks YP directory listings. YP directory listing "
                "support will be disabled.", CONFIG_DEFAULT_HOSTNAME);
                /* FIXME actually disable YP */
        break;
        case HOSTCHECK_IS_IPV4:
            ICECAST_LOG_WARN("Warning, <hostname> seems to be set to an IPv4 "
                "address. This may cause problems, e.g. with YP directory "
                "listings.");
        break;
        case HOSTCHECK_IS_IPV6:
            ICECAST_LOG_WARN("Warning, <hostname> seems to be set to an IPv6 "
                "address. This may cause problems, e.g. with YP directory "
                "listings.");
        break;
        case HOSTCHECK_BADCHAR:
            ICECAST_LOG_WARN("Warning, <hostname> contains unusual "
                "characters. This may cause problems, e.g. with YP directory "
                "listings.");
        break;
    }

    if (!sane_hostname)
        configuration->config_problems |= CONFIG_PROBLEM_HOSTNAME;
}

static void _parse_root(xmlDocPtr       doc,
                        xmlNodePtr      node,
                        ice_config_t   *configuration)
{
    char *tmp;
    char *source_password = NULL;

    configuration
        ->listen_sock       = calloc(1, sizeof(*configuration->listen_sock));
    configuration
        ->listen_sock->port = 8000;
    configuration
        ->listen_sock_count = 1;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;
        if (xmlStrcmp(node->name, XMLSTR("location")) == 0) {
            if (configuration->location)
                xmlFree(configuration->location);
            configuration->location = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("admin")) == 0) {
            if (configuration->admin)
                xmlFree(configuration->admin);
            configuration->admin = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("server-id")) == 0) {
            xmlFree(configuration->server_id);
            configuration->server_id = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            ICECAST_LOG_WARN("Warning, server version string override "
                "detected. This may lead to unexpected client software "
                "behavior.");
        } else if (xmlStrcmp(node->name, XMLSTR("authentication")) == 0) {
            _parse_authentication(doc, node->xmlChildrenNode, configuration, &source_password);
        } else if (xmlStrcmp(node->name, XMLSTR("source-password")) == 0) {
            /* TODO: This is the backwards-compatibility location */
            ICECAST_LOG_WARN("<source-password> defined outside "
                "<authentication>. This is deprecated and will be removed in "
                "version 2.X.0");
            __found_bad_tag(configuration, node, BTR_OBSOLETE, "Use <source-password> in <authentication>.");
	    /* FIXME Settle target version for removal of this functionality! */
            if (source_password)
                xmlFree(source_password);
            source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("icelogin")) == 0) {
            ICECAST_LOG_ERROR("<icelogin> support has been removed.");
            __found_bad_tag(configuration, node, BTR_OBSOLETE, NULL);
        } else if (xmlStrcmp(node->name, XMLSTR("fileserve")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->fileserve = util_str_to_bool(tmp);
            if (tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("relays-on-demand")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->on_demand = util_str_to_bool(tmp);
            if (tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("hostname")) == 0) {
            if (configuration->hostname)
                xmlFree(configuration->hostname);
            configuration->hostname = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("mime-types")) == 0) {
            __found_bad_tag(configuration, node, BTR_OBSOLETE, "Use <mime-types> in <paths>.");
            if (configuration->mimetypes_fn)
                xmlFree(configuration->mimetypes_fn);
            configuration->mimetypes_fn = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("listen-socket")) == 0) {
            _parse_listen_socket(doc, node, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp && *tmp) {
                configuration->port = atoi(tmp);
                configuration->listen_sock->port = atoi(tmp);
                xmlFree(tmp);
            } else {
                ICECAST_LOG_WARN("<port> setting must not be empty.");
            }
        } else if (xmlStrcmp(node->name, XMLSTR("bind-address")) == 0) {
            if (configuration->listen_sock->bind_address)
                xmlFree(configuration->listen_sock->bind_address);
            configuration->listen_sock->bind_address = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("master-server")) == 0) {
            if (configuration->master_server)
                xmlFree(configuration->master_server);
            configuration->master_server = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("master-username")) == 0) {
            if (configuration->master_username)
                xmlFree(configuration->master_username);
            configuration->master_username = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("master-password")) == 0) {
            if (configuration->master_password)
                xmlFree(configuration->master_password);
            configuration->master_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("master-server-port")) == 0) {
            __read_int(doc, node, &configuration->master_server_port, "<master-server-port> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("master-update-interval")) == 0) {
            __read_int(doc, node, &configuration->master_update_interval, "<master-update-interval> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("shoutcast-mount")) == 0) {
            if (configuration->shoutcast_mount)
                xmlFree(configuration->shoutcast_mount);
            configuration->shoutcast_mount = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("limits")) == 0) {
            _parse_limits(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("http-headers")) == 0) {
            config_parse_http_headers(node->xmlChildrenNode, &(configuration->http_headers));
        } else if (xmlStrcmp(node->name, XMLSTR("relay")) == 0) {
            _parse_relay(doc, node->xmlChildrenNode, configuration, NULL);
        } else if (xmlStrcmp(node->name, XMLSTR("mount")) == 0) {
            _parse_mount(doc, node, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("directory")) == 0) {
            _parse_oldstyle_directory(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("yp-directory")) == 0) {
            _parse_yp_directory(doc, node, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("paths")) == 0) {
            _parse_paths(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("logging")) == 0) {
            _parse_logging(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("security")) == 0) {
            _parse_security(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp(node->name, XMLSTR("event-bindings")) == 0 ||
                   xmlStrcmp(node->name, XMLSTR("kartoffelsalat")) == 0) {
            _parse_events(&configuration->event, node->xmlChildrenNode);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));

    /* global source password is set.
     * We need to set it on default mount.
     * If default mount has a authstack not NULL we don't need to do anything.
     */
    if (source_password) {
        mount_proxy *mount = config_find_mount(configuration, NULL, MOUNT_TYPE_DEFAULT);
        if (!mount) {
            /* create a default mount here */
            xmlNodePtr node;
            node = xmlNewNode(NULL, XMLSTR("mount"));
            xmlSetProp(node, XMLSTR("type"), XMLSTR("default"));
            _parse_mount(doc, node, configuration);
            xmlFreeNode(node);
            mount = config_find_mount(configuration, NULL, MOUNT_TYPE_DEFAULT);
        }
        if (mount) {
            if (!mount->authstack) {
                __append_old_style_auth(&mount->authstack,
                                        CONFIG_LEGACY_SOURCE_NAME_GLOBAL,
                                        AUTH_TYPE_STATIC, "source",
                                        source_password, NULL,
                                        CONFIG_LEGACY_SOURCE_METHODS, CONFIG_LEGACY_SOURCE_ALLOW_WEB, CONFIG_LEGACY_SOURCE_ALLOW_ADMIN);
            }
        } else {
            ICECAST_LOG_ERROR("Can not find nor create default mount, but "
                "global legacy source password set. This is bad.");
        }
        xmlFree(source_password);
    }
    /* drop the first listening socket details if more than one is defined, as we only
     * have port or listen-socket not both */
    if (configuration->listen_sock_count > 1) {
        configuration->listen_sock = config_clear_listener(configuration->listen_sock);
        configuration->listen_sock_count--;
    }
    if (configuration->port == 0)
        configuration->port = 8000;

    if (!configuration->prng_seed) {
        configuration->config_problems |= CONFIG_PROBLEM_PRNG;
#ifndef _WIN32
        configuration->prng_seed = calloc(1, sizeof(prng_seed_config_t));
        if (configuration->prng_seed) {
            configuration->prng_seed->filename = (char*)xmlStrdup(XMLSTR("linux")); // the linux profile is also fine on BSD.
            configuration->prng_seed->type = PRNG_SEED_TYPE_PROFILE;
            configuration->prng_seed->size = -1;
            ICECAST_LOG_WARN("Warning, no PRNG seed configured, using default profile \"linux\".");
        } else {
            ICECAST_LOG_ERROR("No PRNG seed configured and unable to add one. PRNG is insecure.");
        }
#else
        ICECAST_LOG_ERROR("No PRNG seed configured and unable to add one. PRNG is insecure.");
#endif
    }

    /* issue some warnings on bad configurations */
    if (!configuration->fileserve)
        ICECAST_LOG_WARN("Warning, serving of static files has been disabled "
            "in the config, this will also affect files used by the web "
            "interface (stylesheets, images).");

    __check_hostname(configuration);

    if (!configuration->location ||
        strcmp(configuration->location, CONFIG_DEFAULT_LOCATION) == 0) {
        ICECAST_LOG_WARN("Warning, <location> not configured, using default "
            "value \"%s\".", CONFIG_DEFAULT_LOCATION);
        if (!configuration->location)
            configuration->location = (char *) xmlCharStrdup(CONFIG_DEFAULT_LOCATION);
        configuration->config_problems |= CONFIG_PROBLEM_LOCATION;
    }

    if (!configuration->admin ||
        strcmp(configuration->admin, CONFIG_DEFAULT_ADMIN) == 0) {
        ICECAST_LOG_WARN("Warning, <admin> contact not configured, using "
            "default value \"%s\". This breaks YP directory listings. "
            "YP directory support will be disabled.", CONFIG_DEFAULT_ADMIN);
            /* FIXME actually disable YP */
        if (!configuration->admin)
            configuration->admin = (char *) xmlCharStrdup(CONFIG_DEFAULT_ADMIN);
        configuration->config_problems |= CONFIG_PROBLEM_ADMIN;
    }
}

static void _parse_limits(xmlDocPtr     doc,
                          xmlNodePtr    node,
                          ice_config_t *configuration)
{
    char *tmp;
    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("clients")) == 0) {
            __read_int(doc, node, &configuration->client_limit, "<clients> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("sources")) == 0) {
            __read_int(doc, node, &configuration->source_limit, "<sources> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("bodysize")) == 0) {
            __read_int(doc, node, &configuration->body_size_limit, "<bodysize> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("queue-size")) == 0) {
            __read_unsigned_int(doc, node, &configuration->queue_size_limit, "<queue-size> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("client-timeout")) == 0) {
            __read_int(doc, node, &configuration->client_timeout, "<client-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("header-timeout")) == 0) {
            __read_int(doc, node, &configuration->header_timeout, "<header-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("source-timeout")) == 0) {
            __read_int(doc, node, &configuration->source_timeout, "<source-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("body-timeout")) == 0) {
            __read_int(doc, node, &configuration->body_timeout, "<body-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("burst-on-connect")) == 0) {
            __found_bad_tag(configuration, node, BTR_OBSOLETE, "Use <burst-size>.");
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (util_str_to_int(tmp, 0) == 0)
                configuration->burst_size = 0;
            if (tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("burst-size")) == 0) {
            __read_unsigned_int(doc, node, &configuration->burst_size, "<burst-size> must not be empty.");
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));
}

static void _parse_authentication_node(xmlNodePtr node, auth_stack_t  **authstack)
{
    xmlChar *tmp;

    if (xmlStrcmp(node->name, XMLSTR("authentication")) != 0)
        return;

    tmp = xmlGetProp(node, XMLSTR("type"));
    if (tmp) {
        ICECAST_LOG_ERROR("new style parser called on old style config.");
        xmlFree(tmp);
        return;
    }

    xmlNodePtr child = node->xmlChildrenNode;
    do {
        if (child == NULL)
            break;
        if (xmlIsBlankNode(child))
            continue;
        if (xmlStrcmp(child->name, XMLSTR("role")) == 0) {
            auth_t *auth = auth_get_authenticator(child);
            auth_stack_push(authstack, auth);
            auth_release(auth);
        }
    } while ((child = child->next));
}

static void _parse_mount_oldstyle_authentication(mount_proxy    *mount,
                                                 xmlNodePtr      node,
                                                 auth_stack_t  **authstack)
{
     int        allow_duplicate_users = 1;
     auth_t    *auth;
     char      *type;
     char      *name;
     char      *value;
     xmlNodePtr child;

     child = node->xmlChildrenNode;

     while (child) {
         if (xmlStrcmp(child->name, XMLSTR("option")) == 0) {
             name = (char *)xmlGetProp(child, XMLSTR("name"));
             value = (char *)xmlGetProp(child, XMLSTR("value"));
             if (name && value) {
                 if (strcmp(name, "allow_duplicate_users") == 0) {
                     allow_duplicate_users = util_str_to_bool(value);
                 }
             }
             if (name)
                 xmlFree(name);
             if (value)
                 xmlFree(value);
         }
         child = child->next;
     }

     type = (char *)xmlGetProp(node, XMLSTR("type"));
     if (strcmp(type, AUTH_TYPE_HTPASSWD) == 0) {
         if (!allow_duplicate_users)
             xmlSetProp(node, XMLSTR("connections-per-user"), XMLSTR("0"));

         auth = auth_get_authenticator(node);
         if (auth) {
             auth_stack_push(authstack, auth);
             auth_release(auth);
         }

         __append_old_style_auth(authstack, NULL, AUTH_TYPE_ANONYMOUS,
             NULL, NULL, CONFIG_LEGACY_ANONYMOUS_METHODS, NULL, 0, NULL);
     } else if (strcmp(type, AUTH_TYPE_URL) == 0) {
         /* This block is super fun! Attention! Super fun ahead! Ladies and Gentlemen take care and watch your children! */
         /* Stuff that was of help:
          * $ sed 's/^.*name="\([^"]*\)".*$/         const char *\1 = NULL;/'
          * $ sed 's/^.*name="\([^"]*\)".*$/         if (\1)\n             xmlFree(\1);/'
          * $ sed 's/^.*name="\([^"]*\)".*$/                     } else if (strcmp(name, "\1") == 0) {\n                         \1 = value;\n                         value = NULL;/'
          */
         /* urls */
         char *mount_add        = NULL;
         char *mount_remove     = NULL;
         char *listener_add     = NULL;
         char *listener_remove  = NULL;
         char *stream_auth      = NULL;
         /* request credentials */
         char *username         = NULL;
         char *password         = NULL;
         /* general options */
         char *auth_header      = NULL;
         char *timelimit_header = NULL;
         char *headers          = NULL;
         char *header_prefix    = NULL;

         child = node->xmlChildrenNode;
         while (child) {
             if (xmlStrcmp(child->name, XMLSTR("option")) == 0) {
                 name = (char *)xmlGetProp(child, XMLSTR("name"));
                 value = (char *)xmlGetProp(child, XMLSTR("value"));

                 if (name && value) {
                     if (strcmp(name, "mount_add") == 0) {
                         mount_add = value;
                         value = NULL;
                     } else if (strcmp(name, "mount_remove") == 0) {
                         mount_remove = value;
                         value = NULL;
                     } else if (strcmp(name, "listener_add") == 0) {
                         listener_add = value;
                         value = NULL;
                     } else if (strcmp(name, "listener_remove") == 0) {
                         listener_remove = value;
                         value = NULL;
                     } else if (strcmp(name, "username") == 0) {
                         username = value;
                         value = NULL;
                     } else if (strcmp(name, "password") == 0) {
                         password = value;
                         value = NULL;
                     } else if (strcmp(name, "auth_header") == 0) {
                         auth_header = value;
                         value = NULL;
                     } else if (strcmp(name, "timelimit_header") == 0) {
                         timelimit_header = value;
                         value = NULL;
                     } else if (strcmp(name, "headers") == 0) {
                         headers = value;
                         value = NULL;
                     } else if (strcmp(name, "header_prefix") == 0) {
                         header_prefix = value;
                         value = NULL;
                     } else if (strcmp(name, "stream_auth") == 0) {
                         stream_auth = value;
                         value = NULL;
                     }
                 }

                 if (name)
                     xmlFree(name);
                 if (value)
                     xmlFree(value);
             }
             child = child->next;
         }

         if (mount_add)
             __append_old_style_url_event(&mount->event, "source-connect",
                 mount_add, "mount_add", username, password);
         if (mount_remove)
             __append_old_style_url_event(&mount->event, "source-disconnect",
                 mount_add, "mount_remove", username, password);

         __append_old_style_urlauth(authstack, listener_add, listener_remove,
             "listener_add", "listener_remove", username, password, 0,
             auth_header, timelimit_header, headers, header_prefix);
         __append_old_style_urlauth(authstack, stream_auth, NULL, "stream_auth",
             NULL, username, password, 1, auth_header, timelimit_header,
             headers, header_prefix);
         if (listener_add)
             __append_old_style_auth(authstack, NULL, AUTH_TYPE_ANONYMOUS, NULL,
                 NULL, CONFIG_LEGACY_ANONYMOUS_METHODS, NULL, 0, NULL);
         if (stream_auth)
             __append_old_style_auth(authstack, NULL, AUTH_TYPE_ANONYMOUS, NULL,
                 NULL, CONFIG_LEGACY_SOURCE_METHODS, NULL, 0, NULL);

         if (mount_add)
             xmlFree(mount_add);
         if (mount_remove)
             xmlFree(mount_remove);
         if (listener_add)
             xmlFree(listener_add);
         if (listener_remove)
             xmlFree(listener_remove);
         if (username)
             xmlFree(username);
         if (password)
             xmlFree(password);
         if (auth_header)
             xmlFree(auth_header);
         if (timelimit_header)
             xmlFree(timelimit_header);
         if (headers)
             xmlFree(headers);
         if (header_prefix)
             xmlFree(header_prefix);
         if (stream_auth)
             xmlFree(stream_auth);
     } else {
         ICECAST_LOG_ERROR("Unknown authentication type in legacy mode. "
             "Anonymous listeners and global login for sources disabled.");
         __append_old_style_auth(authstack, NULL, AUTH_TYPE_ANONYMOUS, NULL,
             NULL, NULL, NULL, 0, NULL);
     }
     xmlFree(type);
}

static void _parse_mount(xmlDocPtr      doc,
                         xmlNodePtr     parentnode,
                         ice_config_t  *configuration)
{
    char         *tmp;
    mount_proxy  *mount      = calloc(1, sizeof(mount_proxy));
    mount_proxy  *current    = configuration->mounts;
    mount_proxy  *last       = NULL;
    char         *username   = NULL;
    char         *password   = NULL;
    auth_stack_t *authstack  = NULL;
    xmlNodePtr    node;

    /* default <mount> settings */
    mount->mounttype            = MOUNT_TYPE_NORMAL;
    mount->max_listeners        = -1;
    mount->burst_size           = -1;
    mount->mp3_meta_interval    = -1;
    mount->yp_public            = -1;
    mount->max_history          = -1;
    mount->next                 = NULL;

    tmp = (char *)xmlGetProp(parentnode, XMLSTR("type"));
    if (tmp) {
        if (strcmp(tmp, "normal") == 0) {
            mount->mounttype = MOUNT_TYPE_NORMAL;
        } else if (strcmp(tmp, "default") == 0) {
            mount->mounttype = MOUNT_TYPE_DEFAULT;
        } else {
            ICECAST_LOG_WARN("Unknown mountpoint type: %s", tmp);
            config_clear_mount(mount);
            return;
        }
        xmlFree(tmp);
    }

    node = parentnode->xmlChildrenNode;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("mount-name")) == 0) {
            mount->mountname = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("username")) == 0) {
            username = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("password")) == 0) {
            password = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("dump-file")) == 0) {
            mount->dumpfile = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("intro")) == 0) {
            mount->intro_filename = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("fallback-mount")) == 0) {
            mount->fallback_mount = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("fallback-when-full")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->fallback_when_full = util_str_to_bool(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("max-listeners")) == 0) {
            __read_int(doc, node, &mount->max_listeners, "<max-listeners> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("max-history")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->max_history = util_str_to_int(tmp, mount->max_history);
            if (mount->max_history < 1 || mount->max_history > 256)
                mount->max_history = 256; /* deny super huge values */
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("charset")) == 0) {
            mount->charset = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("mp3-metadata-interval")) == 0) {
            __found_bad_tag(configuration, node, BTR_OBSOLETE, "Use <icy-metadata-interval>.");
                /* FIXME when do we plan to remove this? */
            __read_int(doc, node, &mount->mp3_meta_interval, "<mp3-metadata-interval> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("icy-metadata-interval")) == 0) {
            __read_int(doc, node, &mount->mp3_meta_interval, "<icy-metadata-interval> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("fallback-override")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->fallback_override = config_str_to_fallback_override_t(configuration, node, tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("no-mount")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->no_mount = util_str_to_bool(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("no-yp")) == 0) {
            __found_bad_tag(configuration, node, BTR_OBSOLETE, "Use <public>.");
                /* FIXME when do we plan to remove this? */
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = util_str_to_bool(tmp) == 0 ? -1 : 0;
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("hidden")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->hidden = util_str_to_bool(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("authentication")) == 0) {
            tmp = (char *)xmlGetProp(node, XMLSTR("type"));
            if (tmp) {
                xmlFree(tmp);
                _parse_mount_oldstyle_authentication(mount, node, &authstack);
            } else {
                _parse_authentication_node(node, &authstack);
            }
        } else if (xmlStrcmp(node->name, XMLSTR("on-connect")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp) {
                __append_old_style_exec_event(&mount->event,
                    "source-connect", tmp);
                xmlFree(tmp);
            }
        } else if (xmlStrcmp(node->name, XMLSTR("on-disconnect")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp) {
                __append_old_style_exec_event(&mount->event,
                    "source-disconnect", tmp);
                xmlFree(tmp);
            }
        } else if (xmlStrcmp(node->name, XMLSTR("max-listener-duration")) == 0) {
            __read_unsigned_int(doc, node, &mount->max_listener_duration, "<max-listener-duration> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("queue-size")) == 0) {
            __read_unsigned_int(doc, node, &mount->queue_size_limit, "<queue-size> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("source-timeout")) == 0) {
            __read_unsigned_int(doc, node, &mount->source_timeout, "<source-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("burst-size")) == 0) {
            __read_int(doc, node, &mount->burst_size, "<burst-size> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("cluster-password")) == 0) {
            mount->cluster_password = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("stream-name")) == 0) {
            mount->stream_name = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("stream-description")) == 0) {
            mount->stream_description = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("stream-url")) == 0) {
            mount->stream_url = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("genre")) == 0) {
            mount->stream_genre = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("bitrate")) == 0) {
            mount->bitrate = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("public")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = __parse_public(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("type")) == 0) {
            mount->type = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("subtype")) == 0) {
            mount->subtype = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("http-headers")) == 0) {
            config_parse_http_headers(node->xmlChildrenNode,
                &(mount->http_headers));
        } else if (xmlStrcmp(node->name, XMLSTR("event-bindings")) == 0 ||
                   xmlStrcmp(node->name, XMLSTR("kartoffelsalat")) == 0) {
            _parse_events(&mount->event, node->xmlChildrenNode);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));

    /* Do a second interation as we need to know mount->mountname, and mount->mounttype first */
    node = parentnode->xmlChildrenNode;

    do {
        if (node == NULL)
            break;

        if (xmlStrcmp(node->name, XMLSTR("relay")) == 0) {
            if (mount->mounttype != MOUNT_TYPE_NORMAL) {
                ICECAST_LOG_WARN("<relay> set within <mount> for mountpoint %s%s%s that is not type=\"normal\"",
                                 (mount->mountname ? "\"" : ""), (mount->mountname ? mount->mountname : "<no name>"), (mount->mountname ? "\"" : ""));
            } else if (!mount->mountname || mount->mountname[0] != '/') {
                ICECAST_LOG_WARN("<relay> set within <mount> with no mountpoint defined.");
            } else {
                _parse_relay(doc, node->xmlChildrenNode, configuration, mount->mountname);
            }
        }
    } while ((node = node->next));

    if (password) {
        auth_stack_t *old_style = NULL;
        __append_old_style_auth(&old_style, CONFIG_LEGACY_SOURCE_NAME_MOUNT,
            AUTH_TYPE_STATIC, username ? username : "source", password, NULL,
            CONFIG_LEGACY_SOURCE_METHODS, CONFIG_LEGACY_SOURCE_ALLOW_WEB, CONFIG_LEGACY_SOURCE_ALLOW_ADMIN);
        if (authstack) {
            auth_stack_append(old_style, authstack);
            auth_stack_release(authstack);
        }
        authstack = old_style;
    }

    if (username)
        xmlFree(username);
    if (password)
        xmlFree(password);

    if (mount->authstack)
        auth_stack_release(mount->authstack);
    auth_stack_addref(mount->authstack = authstack);

    ICECAST_LOG_DEBUG("Mount %p (mountpoint %s) has %sactive "
        "roles on authstack.", mount, mount->mountname, authstack ? "" : "no ");

    /* make sure we have at least the mountpoint name */
    if (mount->mountname == NULL && mount->mounttype != MOUNT_TYPE_DEFAULT) {
        config_clear_mount(mount);
        return;
    } else if (mount->mountname != NULL && mount->mounttype == MOUNT_TYPE_DEFAULT) {
        ICECAST_LOG_WARN("Default mount %s has mount-name set. This is "
            "not supported. Behavior may not be consistent.", mount->mountname);
    }

    while (authstack) {
        auth_t *auth = auth_stack_get(authstack);
        if (mount->mountname) {
            auth->mount = strdup((char *)mount->mountname);
        } else if (mount->mounttype == MOUNT_TYPE_DEFAULT ) {
            auth->mount = strdup("(default mount)");
        }
        auth_release(auth);
        auth_stack_next(&authstack);
    }

    while(current) {
        last = current;
        current = current->next;
    }

    if (!mount->fallback_mount && (mount->fallback_when_full || mount->fallback_override != FALLBACK_OVERRIDE_NONE)) {
        ICECAST_LOG_WARN("Config for mount %s contains fallback options "
            "but no fallback mount.", mount->mountname);
    }

    if(last) {
        last->next = mount;
    } else {
        configuration->mounts = mount;
    }
}

void config_parse_http_headers(xmlNodePtr                  node,
                               ice_config_http_header_t  **http_headers)
{
    ice_config_http_header_t *header;
    ice_config_http_header_t *next;
    char                     *name  = NULL;
    char                     *value = NULL;
    char                     *tmp;
    int                       status;
    http_header_type          type;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;
        if (xmlStrcmp(node->name, XMLSTR("header")) != 0)
            continue;
        if (!(name = (char *)xmlGetProp(node, XMLSTR("name"))))
            break;

        value = (char *)xmlGetProp(node, XMLSTR("value"));

        type = HTTP_HEADER_TYPE_STATIC; /* default */
        if ((tmp = (char *)xmlGetProp(node, XMLSTR("type")))) {
            if (strcmp(tmp, "static") == 0) {
                type = HTTP_HEADER_TYPE_STATIC;
            } else if (strcmp(tmp, "cors") == 0 || strcmp(tmp, "corpse") == 0) {
                type = HTTP_HEADER_TYPE_CORS;
            } else {
                ICECAST_LOG_WARN("Unknown type %s for "
                    "HTTP Header %s", tmp, name);
                xmlFree(tmp);
                break;
            }
            xmlFree(tmp);
        }

        status = 0; /* default: any */
        if ((tmp = (char *)xmlGetProp(node, XMLSTR("status")))) {
            status = util_str_to_int(tmp, 0);
            xmlFree(tmp);
        }

        header = calloc(1, sizeof(ice_config_http_header_t));
        if (!header)
            break;

        header->type    = type;
        header->name    = name;
        header->value   = value;
        header->status  = status;
        name            = NULL;
        value           = NULL;

        if (!*http_headers) {
            *http_headers = header;
            continue;
        }
        next = *http_headers;
        while (next->next) {
            next = next->next;
        }
        next->next = header;
    } while ((node = node->next));
    /* in case we used break we may need to clean those up */
    if (name)
        xmlFree(name);
    if (value)
        xmlFree(value);
}

static void _parse_relay_upstream(xmlDocPtr      doc,
                                  xmlNodePtr     node,
                                  relay_config_upstream_t *upstream)
{
    char         *tmp;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("server")) == 0) {
            if (upstream->server)
                xmlFree(upstream->server);
            upstream->server = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("port")) == 0) {
            __read_int(doc, node, &upstream->port, "<port> setting must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("mount")) == 0) {
            if (upstream->mount)
                xmlFree(upstream->mount);
            upstream->mount = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("relay-shoutcast-metadata")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            upstream->mp3metadata = util_str_to_bool(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("username")) == 0) {
            if (upstream->username)
                xmlFree(upstream->username);
            upstream->username = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("password")) == 0) {
            if (upstream->password)
                xmlFree(upstream->password);
            upstream->password = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("bind")) == 0) {
            if (upstream->bind)
                xmlFree(upstream->bind);
            upstream->bind = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));
}

static void _parse_relay_upstream_apply_defaults(relay_config_upstream_t *upstream)
{
    if (!upstream->server)
        upstream->server = (char *)xmlCharStrdup(CONFIG_DEFAULT_RELAY_SERVER);
    if (!upstream->port)
        upstream->port = CONFIG_DEFAULT_RELAY_PORT;
    if (!upstream->mount)
        upstream->mount = (char *)xmlCharStrdup(CONFIG_DEFAULT_RELAY_MOUNT);
}

static void _parse_relay(xmlDocPtr      doc,
                         xmlNodePtr     node,
                         ice_config_t  *configuration,
                         const char *mount)
{
    char         *tmp;
    relay_config_t *relay       = calloc(1, sizeof(relay_config_t));
    relay_config_t **n          = realloc(configuration->relay, sizeof(*configuration->relay)*(configuration->relay_length + 1));

    if (!n) {
        free(relay);
        ICECAST_LOG_ERROR("Can not allocate memory for additional relay.");
        return;
    }

    configuration->relay = n;
    configuration->relay[configuration->relay_length++] = relay;

    relay->upstream_default.mp3metadata     = 1;
    relay->on_demand                        = configuration->on_demand;

    _parse_relay_upstream(doc, node, &(relay->upstream_default));

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("local-mount")) == 0) {
            if (mount) {
                ICECAST_LOG_WARN("Relay defined within mount \"%s\" defines <local-mount> which is ignored.", mount);
            } else {
                if (relay->localmount)
                    xmlFree(relay->localmount);
                relay->localmount = (char *)xmlNodeListGetString(doc,
                        node->xmlChildrenNode, 1);
            }
        } else if (xmlStrcmp(node->name, XMLSTR("on-demand")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->on_demand = util_str_to_bool(tmp);
            if (tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("upstream")) == 0) {
            tmp = (char *)xmlGetProp(node, XMLSTR("type"));

            if (tmp == NULL || strcmp(tmp, "normal") == 0) {
                relay_config_upstream_t *n = realloc(relay->upstream, sizeof(*n)*(relay->upstreams + 1));
                if (n) {
                    relay->upstream = n;
                    memset(&(n[relay->upstreams]), 0, sizeof(relay_config_upstream_t));
                    _parse_relay_upstream(doc, node->xmlChildrenNode, &(n[relay->upstreams]));
                    relay->upstreams++;
                }
            } else if (strcmp(tmp, "default") == 0) {
                _parse_relay_upstream(doc, node->xmlChildrenNode, &(relay->upstream_default));
            } else {
                ICECAST_LOG_WARN("<upstream> of unknown type is ignored.");
            }

            if (tmp)
                xmlFree(tmp);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));

    _parse_relay_upstream_apply_defaults(&(relay->upstream_default));

    if (mount) {
        relay->localmount = (char *)xmlStrdup(XMLSTR(mount));
    }

    if (relay->localmount == NULL)
        relay->localmount = (char *)xmlStrdup(XMLSTR(relay->upstream_default.mount));
}

static void _parse_listen_socket(xmlDocPtr      doc,
                                 xmlNodePtr     node,
                                 ice_config_t  *configuration)
{
    char        *tmp;
    listener_t  *listener = calloc(1, sizeof(listener_t));

    if (listener == NULL)
        return;
    listener->port = 8000;

    listener->id  = (char *)xmlGetProp(node, XMLSTR("id"));

    tmp = (char*)xmlGetProp(node, XMLSTR("on-behalf-of"));
    if (tmp) {
        listener->on_behalf_of = config_href_to_id(configuration, node, tmp);
        xmlFree(tmp);
    }

    tmp  = (char *)xmlGetProp(node, XMLSTR("type"));
    listener->type = config_str_to_listener_type(configuration, node, tmp);
    xmlFree(tmp);

    node = node->xmlChildrenNode;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp) {
                if(configuration->port == 0)
                    configuration->port = util_str_to_int(tmp, 0);
                listener->port = util_str_to_int(tmp, listener->port);
                xmlFree(tmp);
            } else {
                ICECAST_LOG_WARN("<port> setting must not be empty.");
            }
        } else if (xmlStrcmp(node->name, XMLSTR("tls")) == 0 ||
                   xmlStrcmp(node->name, XMLSTR("ssl")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            listener->tls = str_to_tlsmode(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("shoutcast-compat")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            listener->shoutcast_compat = util_str_to_bool(tmp);
            if(tmp)
                xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("shoutcast-mount")) == 0) {
            if (listener->shoutcast_mount)
                xmlFree(listener->shoutcast_mount);
            listener->shoutcast_mount = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("bind-address")) == 0) {
            if (listener->bind_address)
                xmlFree(listener->bind_address);
            listener->bind_address = (char *)xmlNodeListGetString(doc,
                node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("so-sndbuf")) == 0) {
            __read_int(doc, node, &listener->so_sndbuf, "<so-sndbuf> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("listen-backlog")) == 0) {
            __read_int(doc, node, &listener->listen_backlog, "<listen-backlog> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("authentication")) == 0) {
            _parse_authentication_node(node, &(listener->authstack));
        } else if (xmlStrcmp(node->name, XMLSTR("http-headers")) == 0) {
            config_parse_http_headers(node->xmlChildrenNode, &(listener->http_headers));
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));

    /* we know there's at least one of these, so add this new one after the first
     * that way it can be removed easily later on */
    listener->next = configuration->listen_sock->next;
    configuration->listen_sock->next = listener;
    configuration->listen_sock_count++;
    if (listener->shoutcast_mount && !listener->shoutcast_compat) {
        listener_t *sc_port = calloc(1, sizeof(listener_t));
        sc_port->port = listener->port+1;
        sc_port->shoutcast_compat = 1;
        sc_port->shoutcast_mount = (char*)xmlStrdup(XMLSTR(listener->shoutcast_mount));
        if (listener->bind_address)
            sc_port->bind_address = (char*)xmlStrdup(XMLSTR(listener->bind_address));

        sc_port->next = listener->next;
        listener->next = sc_port;
        configuration->listen_sock_count++;
    }
}

static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration, char **source_password)
{
    char *admin_password = NULL,
         *admin_username = NULL;
    char *relay_password = NULL,
         *relay_username = (char*)xmlCharStrdup(CONFIG_DEFAULT_MASTER_USERNAME);
    auth_stack_t *old_style = NULL,
                 *new_style = NULL;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("source-password")) == 0) {
            if (xmlGetProp(node, XMLSTR("mount"))) {
                ICECAST_LOG_ERROR("Mount level source password defined within global <authentication> section.");
            } else {
                if (*source_password)
                    xmlFree(*source_password);
                *source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (xmlStrcmp(node->name, XMLSTR("admin-password")) == 0) {
            if(admin_password)
                xmlFree(admin_password);
            admin_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("admin-user")) == 0) {
            if(admin_username)
                xmlFree(admin_username);
            admin_username = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("relay-password")) == 0) {
            if(relay_password)
                xmlFree(relay_password);
            relay_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("relay-user")) == 0) {
            if(relay_username)
                xmlFree(relay_username);
            relay_username = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("shoutcast-user")) == 0) {
            if (configuration->shoutcast_user)
                xmlFree(configuration->shoutcast_user);
            configuration->shoutcast_user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("role")) == 0) {
            auth_t *auth = auth_get_authenticator(node);
            auth_stack_push(&new_style, auth);
            auth_release(auth);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));

    if (admin_password && admin_username)
        __append_old_style_auth(&old_style, CONFIG_LEGACY_ADMIN_NAME, AUTH_TYPE_STATIC,
            admin_username, admin_password, NULL, CONFIG_LEGACY_ADMIN_METHODS, CONFIG_LEGACY_ADMIN_ALLOW_WEB, CONFIG_LEGACY_ADMIN_ALLOW_ADMIN);

    if (relay_password && relay_username)
        __append_old_style_auth(&old_style, CONFIG_LEGACY_RELAY_NAME, AUTH_TYPE_STATIC,
            relay_username, relay_password, NULL, CONFIG_LEGACY_RELAY_METHODS, CONFIG_LEGACY_RELAY_ALLOW_WEB, CONFIG_LEGACY_RELAY_ALLOW_ADMIN);

    if (admin_password)
        xmlFree(admin_password);
    if (admin_username)
        xmlFree(admin_username);
    if (relay_password)
        xmlFree(relay_password);
    if (relay_username)
        xmlFree(relay_username);

    if (old_style && new_style) {
        auth_stack_append(old_style, new_style);
        auth_stack_release(new_style);
    } else if (new_style) {
        old_style = new_style;
    }

    /* default unauthed anonymous account */
    __append_old_style_auth(&old_style, CONFIG_LEGACY_ANONYMOUS_NAME, AUTH_TYPE_ANONYMOUS,
        NULL, NULL, NULL, CONFIG_LEGACY_ANONYMOUS_METHODS, CONFIG_LEGACY_ANONYMOUS_ALLOW_WEB, CONFIG_LEGACY_ANONYMOUS_ALLOW_ADMIN);
    if (!old_style)
        ICECAST_LOG_ERROR("BAD. old_style=NULL");

    if (configuration->authstack)
        auth_stack_release(configuration->authstack);
    configuration->authstack = old_style;
}

static void _parse_oldstyle_directory(xmlDocPtr      doc,
                                      xmlNodePtr     node,
                                      ice_config_t  *configuration)
{
    yp_directory_t *yp_dir,
                   *current, *last;

    yp_dir = calloc(1, sizeof(*yp_dir));
    if (yp_dir == NULL) {
        ICECAST_LOG_ERROR("Can not allocate memory for YP directory entry.");
        return;
    }

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("yp-url")) == 0) {
            if (yp_dir->url)
                xmlFree(yp_dir->url);
            yp_dir->url = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("yp-url-timeout")) == 0) {
            __read_int(doc, node, &yp_dir->timeout, "<yp-url-timeout> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("touch-interval")) == 0) {
            __read_int(doc, node, &yp_dir->touch_interval, "<touch-interval> must not be empty.");
        }
    } while ((node = node->next));

    if (yp_dir->url == NULL)
        return;

    /* Append YP directory entry to the global list */
    current = configuration->yp_directories;
    last = NULL;
    while (current) {
        last = current;
        current = current->next;
    }
    if (last) {
        last->next = yp_dir;
    } else {
        configuration->yp_directories = yp_dir;
    }
}

static void _parse_yp_directory(xmlDocPtr      doc,
                                xmlNodePtr     node,
                                ice_config_t  *configuration)
{
    char *url;
    config_options_t *options;
    yp_directory_t *yp_dir,
                   *current, *last;

    url = (char *)xmlGetProp(node, XMLSTR("url"));
    if (url == NULL) {
        ICECAST_LOG_ERROR("Missing mandatory attribute 'url' for <yp-directory>.");
        return;
    }

    yp_dir = calloc(1, sizeof(*yp_dir));
    if (yp_dir == NULL) {
        ICECAST_LOG_ERROR("Can not allocate memory for YP directory entry.");
        return;
    }

    yp_dir->url = url;

    options = config_parse_options(node);
    for (config_options_t *opt = options; opt; opt = opt->next) {
        if (!opt->name || !opt->value) {
            ICECAST_LOG_WARN("Invalid <option>, missing 'name' and 'value' attributes.");
            continue;
        }

        if (strcmp(opt->name, "timeout") == 0) {
            yp_dir->timeout = util_str_to_int(opt->value, yp_dir->timeout);
        } else if (strcmp(opt->name, "touch-interval") == 0) {
            yp_dir->touch_interval = util_str_to_int(opt->value, yp_dir->touch_interval);
        } else if (strcmp(opt->name, "listen-socket") == 0) {
            if (yp_dir->listen_socket_id) {
                ICECAST_LOG_ERROR(
                    "Multiple 'listen-socket' in <yp-directory> currently unsupported. "
                    "Only the last one will be used.");
                free(yp_dir->listen_socket_id);
            }
            /* FIXME: Pass the correct node to config_href_to_id(). */
            yp_dir->listen_socket_id = config_href_to_id(configuration, NULL, opt->value);
        } else {
            ICECAST_LOG_WARN("Invalid YP <option> with unknown 'name' attribute.");
        }
    }
    config_clear_options(options);

    /* Append YP directory entry to the global list */
    current = configuration->yp_directories;
    last = NULL;
    while (current) {
        last = current;
        current = current->next;
    }
    if (last) {
        last->next = yp_dir;
    } else {
        configuration->yp_directories = yp_dir;
    }
}

static void _parse_resource(xmlDocPtr      doc,
                            xmlNodePtr     node,
                            ice_config_t  *configuration)
{
    char *temp;
    resource_t  *resource,
                *current,
                *last;

    resource = calloc(1, sizeof(resource_t));
    if (resource == NULL) {
        ICECAST_LOG_ERROR("Can not allocate memory for resource.");
        return;
    }

    resource->next = NULL;

    resource->source = (char *)xmlGetProp(node, XMLSTR("source"));
    resource->destination = (char *)xmlGetProp(node, XMLSTR("destination"));

    if (!resource->destination)
        resource->destination = (char *)xmlGetProp(node, XMLSTR("dest"));

    if (!resource->source && resource->destination) {
        resource->source = resource->destination;
        resource->destination = NULL;
    } else if (!resource->source && !resource->destination) {
        config_clear_resource(resource);
        return;
    }

    temp = (char *)xmlGetProp(node, XMLSTR("port"));
    if(temp != NULL) {
        resource->port = util_str_to_int(temp, resource->port);
        xmlFree(temp);
    } else {
        resource->port = -1;
    }

    resource->bind_address = (char *)xmlGetProp(node, XMLSTR("bind-address"));

    temp = (char *)xmlGetProp(node, XMLSTR("listen-socket"));
    if (temp) {
        resource->listen_socket = config_href_to_id(configuration, node, temp);
        xmlFree(temp);
    }

    resource->vhost = (char *)xmlGetProp(node, XMLSTR("vhost"));

    resource->module = (char *)xmlGetProp(node, XMLSTR("module"));
    resource->handler = (char *)xmlGetProp(node, XMLSTR("handler"));

    temp = (char *)xmlGetProp(node, XMLSTR("omode"));
    if (temp) {
        resource->omode = config_str_to_omode(configuration, node, temp);
        xmlFree(temp);
    } else {
        resource->omode = OMODE_DEFAULT;
    }

    temp = (char *)xmlGetProp(node, XMLSTR("prefixmatch"));
    if (temp) {
        resource->flags |= util_str_to_bool(temp) ? ALIAS_FLAG_PREFIXMATCH : 0;
        xmlFree(temp);
    }

    /* Attach new <resource> as last entry into the global list. */
    current = configuration->resources;
    last = NULL;
    while (current) {
        last = current;
        current = current->next;
    }
    if (last) {
        last->next = resource;
    } else {
        configuration->resources = resource;
    }
}

static void _parse_paths(xmlDocPtr      doc,
                         xmlNodePtr     node,
                         ice_config_t  *configuration)
{
    char        *temp;

    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("basedir")) == 0) {
            if (configuration->base_dir)
                xmlFree(configuration->base_dir);
            configuration->base_dir =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("logdir")) == 0) {
            if (!(temp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<logdir> setting must not be empty.");
                continue;
            }
            if (configuration->log_dir)
                xmlFree(configuration->log_dir);
            configuration->log_dir = temp;
        } else if (xmlStrcmp(node->name, XMLSTR("pidfile")) == 0) {
            if (configuration->pidfile)
                xmlFree(configuration->pidfile);
            configuration->pidfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("nulldevice")) == 0) {
            if (configuration->null_device)
                xmlFree(configuration->null_device);
            configuration->null_device = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("mime-types")) == 0) {
            if (configuration->mimetypes_fn)
                xmlFree(configuration->mimetypes_fn);
            configuration->mimetypes_fn = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("deny-ip")) == 0) {
            if (configuration->banfile)
                xmlFree(configuration->banfile);
            configuration->banfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("allow-ip")) == 0) {
            if (configuration->allowfile)
                xmlFree(configuration->allowfile);
            configuration->allowfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("tls-certificate")) == 0 ||
                   xmlStrcmp(node->name, XMLSTR("ssl-certificate")) == 0) {
            if (__check_node_impl(node, "generic") != 0) {
                ICECAST_LOG_WARN("Node %s uses unsupported implementation.", node->name);
                continue;
            }

            if (configuration->tls_context.cert_file)
                xmlFree(configuration->tls_context.cert_file);
            configuration->tls_context.cert_file = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("tls-allowed-ciphers")) == 0 ||
                   xmlStrcmp(node->name, XMLSTR("ssl-allowed-ciphers")) == 0) {
            if (__check_node_impl(node, "openssl") != 0) {
                ICECAST_LOG_WARN("Node %s uses unsupported implementation.", node->name);
                continue;
            }

            if (configuration->tls_context.cipher_list)
                xmlFree(configuration->tls_context.cipher_list);
            configuration->tls_context.cipher_list = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("webroot")) == 0) {
            if (!(temp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<webroot> setting must not be empty.");
                continue;
            }
            if (configuration->webroot_dir)
                xmlFree(configuration->webroot_dir);
            configuration->webroot_dir = temp;
            if (configuration->webroot_dir[strlen(configuration->webroot_dir)-1] == '/')
                configuration->webroot_dir[strlen(configuration->webroot_dir)-1] = 0;
        } else if (xmlStrcmp(node->name, XMLSTR("adminroot")) == 0) {
            if (!(temp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<adminroot> setting must not be empty.");
                continue;
            }
            if (configuration->adminroot_dir)
                xmlFree(configuration->adminroot_dir);
            configuration->adminroot_dir = (char *)temp;
            if (configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] == '/')
                configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] = 0;
        } else if (xmlStrcmp(node->name, XMLSTR("reportxmldb")) == 0) {
            reportxml_t *report;
            xmlDocPtr dbdoc;

            if (!(temp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<reportxmldb> setting must not be empty.");
                continue;
            }
            dbdoc = xmlParseFile(temp);
            if (!doc) {
                ICECAST_LOG_ERROR("Can not read report xml database \"%H\" as XML", temp);
            } else {
                report = reportxml_parse_xmldoc(dbdoc);
                xmlFreeDoc(dbdoc);
                if (!report) {
                    ICECAST_LOG_ERROR("Can not parse report xml database \"%H\"", temp);
                } else {
                    reportxml_database_add_report(configuration->reportxml_db, report);
                    refobject_unref(report);
                    ICECAST_LOG_INFO("File \"%H\" added to report xml database", temp);
                }
            }
            xmlFree(temp);
        } else if (xmlStrcmp(node->name, XMLSTR("resource")) == 0 || xmlStrcmp(node->name, XMLSTR("alias")) == 0) {
            _parse_resource(doc, node, configuration);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));
}

static void _parse_logging(xmlDocPtr        doc,
                           xmlNodePtr       node,
                           ice_config_t    *configuration)
{
    char *tmp;
    do {
        if (node == NULL)
            break;
        if (xmlIsBlankNode(node))
            continue;

        if (xmlStrcmp(node->name, XMLSTR("accesslog")) == 0) {
            if (!(tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<accesslog> setting must not be empty.");
                continue;
            }
            if (configuration->access_log)
                xmlFree(configuration->access_log);
            configuration->access_log = tmp;
        } else if (xmlStrcmp(node->name, XMLSTR("errorlog")) == 0) {
            if (!(tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1))) {
                ICECAST_LOG_WARN("<errorlog> setting must not be empty.");
                continue;
            }
            if (configuration->error_log)
                xmlFree(configuration->error_log);
            configuration->error_log = tmp;
        } else if (xmlStrcmp(node->name, XMLSTR("playlistlog")) == 0) {
            if (configuration->playlist_log)
                xmlFree(configuration->playlist_log);
            configuration->playlist_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("logsize")) == 0) {
            __read_int(doc, node, &configuration->logsize, "<logsize> must not be empty.");
        } else if (xmlStrcmp(node->name, XMLSTR("loglevel")) == 0) {
           char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->loglevel = util_str_to_loglevel(tmp);
           if (tmp)
               xmlFree(tmp);
        } else if (xmlStrcmp(node->name, XMLSTR("logarchive")) == 0) {
            char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->logarchive = util_str_to_bool(tmp);
            if (tmp) {
                xmlFree(tmp);
            } else {
                ICECAST_LOG_WARN("<logarchive> must not be empty.");
            }
        } else if (xmlStrcmp(node->name, XMLSTR("memorybacklog")) == 0) {
            int val = CONFIG_DEFAULT_LOG_LINES_KEPT;
            char *logfile = (char *)xmlGetProp(node, XMLSTR("logfile"));

            __read_int(doc, node, &val, "<memorybacklog> must not be empty.");

            if (logfile) {
                if (!strcmp(logfile, "error")) {
                    configuration->error_log_lines_kept = val;
                } else if (!strcmp(logfile, "access")) {
                    configuration->access_log_lines_kept = val;
                } else if (!strcmp(logfile, "playlist")) {
                    configuration->playlist_log_lines_kept = val;
                } else {
                    ICECAST_LOG_WARN("Invalid logfile for <memorybacklog>: %H", logfile);
                }
                xmlFree(logfile);
            } else {
                configuration->error_log_lines_kept = val;
                configuration->access_log_lines_kept = val;
                configuration->playlist_log_lines_kept = val;
            }
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }
    } while ((node = node->next));
}

static void _parse_tls_context(xmlDocPtr       doc,
                               xmlNodePtr      node,
                               ice_config_t   *configuration)
{
    config_tls_context_t *context = &configuration->tls_context;

    node = node->xmlChildrenNode;

   do {
       if (node == NULL)
           break;
       if (xmlIsBlankNode(node))
           continue;

        if (xmlStrcmp(node->name, XMLSTR("tls-certificate")) == 0) {
            if (__check_node_impl(node, "generic") != 0) {
                ICECAST_LOG_WARN("Node %s uses unsupported implementation.", node->name);
                continue;
            }

            if (context->cert_file)
                xmlFree(context->cert_file);
            context->cert_file = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("tls-key")) == 0) {
            if (__check_node_impl(node, "generic") != 0) {
                ICECAST_LOG_WARN("Node %s uses unsupported implementation.", node->name);
                continue;
            }

            if (context->key_file)
                xmlFree(context->key_file);
            context->key_file = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp(node->name, XMLSTR("tls-allowed-ciphers")) == 0) {
            if (__check_node_impl(node, "openssl") != 0) {
                ICECAST_LOG_WARN("Node %s uses unsupported implementation.", node->name);
                continue;
            }

            if (context->cipher_list)
                xmlFree(context->cipher_list);
            context->cipher_list = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else {
            __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
        }

   } while ((node = node->next));
}

static void _parse_security(xmlDocPtr       doc,
                            xmlNodePtr      node,
                            ice_config_t   *configuration)
{
   char         *tmp;
   xmlNodePtr    oldnode;

   do {
       if (node == NULL)
           break;
       if (xmlIsBlankNode(node))
           continue;

       if (xmlStrcmp(node->name, XMLSTR("chroot")) == 0) {
           tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->chroot = util_str_to_bool(tmp);
           if (tmp)
               xmlFree(tmp);
       } else if (xmlStrcmp(node->name, XMLSTR("tls-context")) == 0) {
            _parse_tls_context(doc, node, configuration);
       } else if (xmlStrcmp(node->name, XMLSTR("changeowner")) == 0) {
           configuration->chuid = 1;
           oldnode = node;
           node = node->xmlChildrenNode;
           do {
               if (node == NULL)
                   break;
               if (xmlIsBlankNode(node))
                   continue;
               if (xmlStrcmp(node->name, XMLSTR("user")) == 0) {
                   if (configuration->user)
                       xmlFree(configuration->user);
                   configuration->user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               } else if (xmlStrcmp(node->name, XMLSTR("group")) == 0) {
                   if (configuration->group)
                       xmlFree(configuration->group);
                   configuration->group = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               }
           } while((node = node->next));
           node = oldnode;
       } else if (xmlStrcmp(node->name, XMLSTR("prng-seed")) == 0) {
           tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           if (tmp) {
               prng_seed_config_t *seed = calloc(1, sizeof(prng_seed_config_t));
               seed->filename = tmp;
               seed->type = PRNG_SEED_TYPE_READ_ONCE;
               seed->size = -1;

               tmp = (char *)xmlGetProp(node, XMLSTR("type"));
               if (tmp) {
                   if (strcmp(tmp, "read-once") == 0) {
                       seed->type = PRNG_SEED_TYPE_READ_ONCE;
                   } else if (strcmp(tmp, "read-write") == 0) {
                       seed->type = PRNG_SEED_TYPE_READ_WRITE;
                   } else if (strcmp(tmp, "device") == 0) {
                       seed->type = PRNG_SEED_TYPE_DEVICE;
                   } else if (strcmp(tmp, "static") == 0) {
                       seed->type = PRNG_SEED_TYPE_STATIC;
                   } else if (strcmp(tmp, "profile") == 0) {
                       seed->type = PRNG_SEED_TYPE_PROFILE;
                   } else {
                       ICECAST_LOG_WARN("Unknown type for <prng-seed>: %s", tmp);
                   }
                   xmlFree(tmp);
               }

               tmp = (char *)xmlGetProp(node, XMLSTR("size"));
               if (tmp) {
                   seed->size = atoi(tmp);
                   xmlFree(tmp);
               }

               if (configuration->prng_seed)
                   seed->next = configuration->prng_seed;
               configuration->prng_seed = seed;
           }
       } else {
           __found_bad_tag(configuration, node, BTR_UNKNOWN, NULL);
       }
   } while ((node = node->next));
}

static void _parse_events(event_registration_t **events, xmlNodePtr node)
{
    while (node) {
        if (xmlStrcmp(node->name, XMLSTR("event")) == 0) {
            event_registration_t *reg = event_new_from_xml_node(node);
            event_registration_push(events, reg);
            event_registration_release(reg);
        }
        node = node->next;
    }
}

config_options_t *config_parse_options(xmlNodePtr node)
{
    config_options_t *ret = NULL;
    config_options_t *cur = NULL;

    if (!node)
        return NULL;

    node = node->xmlChildrenNode;

    if (!node)
        return NULL;

    do {
        if (xmlStrcmp(node->name, XMLSTR("option")) != 0)
            continue;
        if (cur) {
           cur->next = calloc(1, sizeof(config_options_t));
           cur = cur->next;
        } else {
          cur = ret = calloc(1, sizeof(config_options_t));
        }

        cur->type  = (char *)xmlGetProp(node, XMLSTR("type"));
        cur->name  = (char *)xmlGetProp(node, XMLSTR("name"));
        cur->value = (char *)xmlGetProp(node, XMLSTR("value"));

        /* map type="default" to NULL. This is to avoid many extra xmlCharStrdup()s. */
        if (cur->type && strcmp(cur->type, "default") == 0) {
            xmlFree(cur->type);
            cur->type = NULL;
        }
    } while ((node = node->next));

    return ret;
}

void config_clear_options(config_options_t *options)
{
    while (options) {
        config_options_t *opt = options;
        options = opt->next;
        if (opt->type)
            xmlFree(opt->type);
        if (opt->name)
            xmlFree(opt->name);
        if (opt->value)
            xmlFree(opt->value);
        free(opt);
    }
}

static void merge_mounts(mount_proxy * dst, mount_proxy * src)
{
    ice_config_http_header_t *http_header_next;
    ice_config_http_header_t **http_header_tail;

    if (!dst || !src)
        return;

    if (!dst->dumpfile)
        dst->dumpfile = (char*)xmlStrdup((xmlChar*)src->dumpfile);
    if (!dst->intro_filename)
        dst->intro_filename = (char*)xmlStrdup((xmlChar*)src->intro_filename);
    if (!dst->fallback_when_full)
        dst->fallback_when_full = src->fallback_when_full;
    if (dst->max_listeners == -1)
        dst->max_listeners = src->max_listeners;
    if (!dst->fallback_mount)
        dst->fallback_mount = (char*)xmlStrdup((xmlChar*)src->fallback_mount);
    if (dst->fallback_override == FALLBACK_OVERRIDE_NONE)
        dst->fallback_override = src->fallback_override;
    if (!dst->no_mount)
        dst->no_mount = src->no_mount;
    if (dst->burst_size == -1)
        dst->burst_size = src->burst_size;
    if (!dst->queue_size_limit)
        dst->queue_size_limit = src->queue_size_limit;
    if (!dst->hidden)
        dst->hidden = src->hidden;
    if (!dst->source_timeout)
        dst->source_timeout = src->source_timeout;
    if (!dst->charset)
        dst->charset = (char*)xmlStrdup((xmlChar*)src->charset);
    if (dst->mp3_meta_interval == -1)
        dst->mp3_meta_interval = src->mp3_meta_interval;
    if (!dst->cluster_password)
        dst->cluster_password = (char*)xmlStrdup((xmlChar*)src->cluster_password);
    if (!dst->max_listener_duration)
        dst->max_listener_duration = src->max_listener_duration;
    if (!dst->stream_name)
        dst->stream_name = (char*)xmlStrdup((xmlChar*)src->stream_name);
    if (!dst->stream_description)
        dst->stream_description = (char*)xmlStrdup((xmlChar*)src->stream_description);
    if (!dst->stream_url)
        dst->stream_url = (char*)xmlStrdup((xmlChar*)src->stream_url);
    if (!dst->stream_genre)
        dst->stream_genre = (char*)xmlStrdup((xmlChar*)src->stream_genre);
    if (!dst->bitrate)
        dst->bitrate = (char*)xmlStrdup((xmlChar*)src->bitrate);
    if (!dst->type)
        dst->type = (char*)xmlStrdup((xmlChar*)src->type);
    if (!dst->subtype)
        dst->subtype = (char*)xmlStrdup((xmlChar*)src->subtype);
    if (dst->yp_public == -1)
        dst->yp_public = src->yp_public;
    if (dst->max_history == -1)
        dst->max_history = src->max_history;

    if (dst->http_headers) {
        http_header_next = dst->http_headers;
        while (http_header_next->next) http_header_next = http_header_next->next;
        http_header_tail = &(http_header_next->next);
    } else {
        http_header_tail = &(dst->http_headers);
    }
    *http_header_tail = config_copy_http_header(src->http_headers);
}

static inline void _merge_mounts_all(ice_config_t *c) {
    mount_proxy *mountinfo = c->mounts;
    mount_proxy *default_mount;

    for (; mountinfo; mountinfo = mountinfo->next) {
        if (mountinfo->mounttype != MOUNT_TYPE_NORMAL)
            continue;
        default_mount = config_find_mount(c, mountinfo->mountname, MOUNT_TYPE_DEFAULT);
        merge_mounts(mountinfo, default_mount);
    }
}

/* return the mount details that match the supplied mountpoint */
mount_proxy *config_find_mount (ice_config_t        *config,
                                const char          *mount,
                                mount_type           type)
{
    mount_proxy *mountinfo = config->mounts;

    /* invalid args */
    if (!mount && type != MOUNT_TYPE_DEFAULT)
        return NULL;

    for (; mountinfo; mountinfo = mountinfo->next) {
        if (mountinfo->mounttype != type)
            continue;
        if (!mount && !mountinfo->mountname)
            break;
        if (mountinfo->mounttype == MOUNT_TYPE_NORMAL) {
            if (!mount || !mountinfo->mountname)
                continue;
            if (strcmp(mountinfo->mountname, mount) == 0)
                break;
        } else if (mountinfo->mounttype == MOUNT_TYPE_DEFAULT) {
            if (!mount || !mountinfo->mountname)
                break;
#ifndef _WIN32
            if (fnmatch(mountinfo->mountname, mount, FNM_PATHNAME) == 0)
                break;
#else
            if (strcmp(mountinfo->mountname, mount) == 0)
                break;
#endif
        }
    }
    /* retry with default mount */
    if (!mountinfo && type == MOUNT_TYPE_NORMAL)
        mountinfo = config_find_mount(config, mount, MOUNT_TYPE_DEFAULT);
    return mountinfo;
}

listener_t *config_copy_listener_one(const listener_t *listener) {
    listener_t *n;

    if (listener == NULL)
        return NULL;

    n = calloc(1, sizeof(*n));
    if (n == NULL)
        return NULL;

    n->next = NULL;
    n->port = listener->port;
    n->so_sndbuf = listener->so_sndbuf;
    n->listen_backlog = listener->listen_backlog;
    n->type = listener->type;
    n->id = (char*)xmlStrdup(XMLSTR(listener->id));
    if (listener->on_behalf_of) {
        n->on_behalf_of = strdup(listener->on_behalf_of);
    }
    n->bind_address = (char*)xmlStrdup(XMLSTR(listener->bind_address));
    n->shoutcast_compat = listener->shoutcast_compat;
    n->shoutcast_mount = (char*)xmlStrdup(XMLSTR(listener->shoutcast_mount));
    n->tls = listener->tls;

    if (listener->authstack) {
        auth_stack_addref(n->authstack = listener->authstack);
    }
    if (listener->http_headers)
        n->http_headers = config_copy_http_header(listener->http_headers);

    return n;
}
