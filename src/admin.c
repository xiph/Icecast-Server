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
 * Copyright 2012-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlversion.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_GETRLIMIT && HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/opensslv.h>
#endif

#include <vorbis/codec.h>

#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif

#ifdef HAVE_SPEEX
#include <speex/speex.h>
#endif

#ifdef HAVE_CURL
#include <curl/curlver.h>
#include <curl/curl.h>
#endif

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#include <igloo/error.h>

#include "common/net/sock.h"

#include "admin.h"
#include "compat.h"
#include "cfgfile.h"
#include "connection.h"
#include "listensocket.h"
#include "refbuf.h"
#include "client.h"
#include "source.h"
#include "global.h"
#include "stats.h"
#include "xslt.h"
#include "fserve.h"
#include "errors.h"
#include "reportxml.h"
#include "reportxml_helper.h"
#include "xml2json.h"

#include "format.h"

#include "logging.h"
#include "auth.h"
#include "acl.h"
#ifdef _WIN32
#define snprintf _snprintf
#endif

#define CATMODULE "admin"

#define ADMIN_MAX_COMMAND_TABLES        8

/* Helper macros */
#define COMMAND_REQUIRE(client,name,var)                                \
    do {                                                                \
        (var) = httpp_get_param((client)->parser, (name));        \
        if((var) == NULL) {                                             \
            client_send_error_by_id(client, ICECAST_ERROR_ADMIN_MISSING_PARAMETER); \
            return;                                                     \
        }                                                               \
    } while(0);

#define COMMAND_OPTIONAL(client,name,var) \
(var) = httpp_get_param((client)->parser, (name))

#define FALLBACK_RAW_REQUEST                "fallbacks"
#define FALLBACK_HTML_REQUEST               "fallbacks.xsl"
#define FALLBACK_JSON_REQUEST               "fallbacks.json"
#define SHOUTCAST_METADATA_REQUEST          "admin.cgi"
#define METADATA_RAW_REQUEST                "metadata"
#define METADATA_HTML_REQUEST               "metadata.xsl"
#define METADATA_JSON_REQUEST               "metadata.json"
#define LISTCLIENTS_RAW_REQUEST             "listclients"
#define LISTCLIENTS_HTML_REQUEST            "listclients.xsl"
#define LISTCLIENTS_JSON_REQUEST            "listclients.json"
#define STATS_RAW_REQUEST                   "stats"
#define STATS_HTML_REQUEST                  "stats.xsl"
#define STATS_JSON_REQUEST                  "stats.json"
#define PUBLICSTATS_RAW_REQUEST             "publicstats"
#define PUBLICSTATS_JSON_REQUEST            "publicstats.json"
#define QUEUE_RELOAD_RAW_REQUEST            "reloadconfig"
#define QUEUE_RELOAD_HTML_REQUEST           "reloadconfig.xsl"
#define QUEUE_RELOAD_JSON_REQUEST           "reloadconfig.json"
#define LISTMOUNTS_RAW_REQUEST              "listmounts"
#define LISTMOUNTS_HTML_REQUEST             "listmounts.xsl"
#define LISTMOUNTS_JSON_REQUEST             "listmounts.json"
#define STREAMLIST_RAW_REQUEST              "streamlist"
#define STREAMLIST_HTML_REQUEST             "streamlist.xsl"
#define STREAMLIST_JSON_REQUEST             "streamlist.json"
#define STREAMLIST_PLAINTEXT_REQUEST        "streamlist.txt"
#define LISTENSOCKETLIST_RAW_REQUEST        "listensocketlist"
#define LISTENSOCKETLIST_HTML_REQUEST       "listensocketlist.xsl"
#define MOVECLIENTS_RAW_REQUEST             "moveclients"
#define MOVECLIENTS_HTML_REQUEST            "moveclients.xsl"
#define MOVECLIENTS_JSON_REQUEST            "moveclients.json"
#define KILLCLIENT_RAW_REQUEST              "killclient"
#define KILLCLIENT_HTML_REQUEST             "killclient.xsl"
#define KILLCLIENT_JSON_REQUEST             "killclient.json"
#define KILLSOURCE_RAW_REQUEST              "killsource"
#define KILLSOURCE_HTML_REQUEST             "killsource.xsl"
#define KILLSOURCE_JSON_REQUEST             "killsource.json"
#define DUMPFILECONTROL_RAW_REQUEST         "dumpfilecontrol"
#define DUMPFILECONTROL_HTML_REQUEST        "dumpfilecontrol.xsl"
#define ADMIN_XSL_RESPONSE                  "response.xsl"
#define MANAGEAUTH_RAW_REQUEST              "manageauth"
#define MANAGEAUTH_HTML_REQUEST             "manageauth.xsl"
#define MANAGEAUTH_JSON_REQUEST             "manageauth.json"
#define UPDATEMETADATA_RAW_REQUEST          "updatemetadata"
#define UPDATEMETADATA_HTML_REQUEST         "updatemetadata.xsl"
#define UPDATEMETADATA_JSON_REQUEST         "updatemetadata.json"
#define SHOWLOG_RAW_REQUEST                 "showlog"
#define SHOWLOG_HTML_REQUEST                "showlog.xsl"
#define SHOWLOG_JSON_REQUEST                "showlog.json"
#define MARKLOG_RAW_REQUEST                 "marklog"
#define MARKLOG_HTML_REQUEST                "marklog.xsl"
#define MARKLOG_JSON_REQUEST                "marklog.json"
#define DASHBOARD_RAW_REQUEST               "dashboard"
#define DASHBOARD_HTML_REQUEST              "dashboard.xsl"
#define DASHBOARD_JSON_REQUEST              "dashboard.json"
#define VERSION_RAW_REQUEST                 "version"
#define VERSION_HTML_REQUEST                "version.xsl"
#define DEFAULT_RAW_REQUEST                 ""
#define DEFAULT_HTML_REQUEST                ""
#define BUILDM3U_RAW_REQUEST                "buildm3u"

typedef struct {
    const char *prefix;
    size_t length;
    const admin_command_handler_t *handlers;
} admin_command_table_t;

static void command_default_selector    (client_t *client, source_t *source, admin_format_t response);
static void command_fallback            (client_t *client, source_t *source, admin_format_t response);
static void command_metadata            (client_t *client, source_t *source, admin_format_t response);
static void command_shoutcast_metadata  (client_t *client, source_t *source, admin_format_t response);
static void command_show_listeners      (client_t *client, source_t *source, admin_format_t response);
static void command_stats               (client_t *client, source_t *source, admin_format_t response);
static void command_public_stats        (client_t *client, source_t *source, admin_format_t response);
static void command_queue_reload        (client_t *client, source_t *source, admin_format_t response);
static void command_list_mounts         (client_t *client, source_t *source, admin_format_t response);
static void command_list_listen_sockets (client_t *client, source_t *source, admin_format_t response);
static void command_move_clients        (client_t *client, source_t *source, admin_format_t response);
static void command_kill_client         (client_t *client, source_t *source, admin_format_t response);
static void command_kill_source         (client_t *client, source_t *source, admin_format_t response);
static void command_dumpfile_control    (client_t *client, source_t *source, admin_format_t response);
static void command_manageauth          (client_t *client, source_t *source, admin_format_t response);
static void command_updatemetadata      (client_t *client, source_t *source, admin_format_t response);
static void command_buildm3u            (client_t *client, source_t *source, admin_format_t response);
static void command_show_log            (client_t *client, source_t *source, admin_format_t response);
static void command_mark_log            (client_t *client, source_t *source, admin_format_t response);
static void command_dashboard           (client_t *client, source_t *source, admin_format_t response);
static void command_version             (client_t *client, source_t *source, admin_format_t response);

static const admin_command_handler_t handlers[] = {
    { "*",                                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   NULL, NULL}, /* for ACL framework */
    { FALLBACK_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_HYBRID,   command_fallback, NULL},
    { FALLBACK_HTML_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_HYBRID,   command_fallback, NULL},
    { FALLBACK_JSON_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_HYBRID,   command_fallback, NULL},
    { METADATA_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_metadata, NULL},
    { METADATA_HTML_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_metadata, NULL},
    { METADATA_JSON_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_UNSAFE,   command_metadata, NULL},
    { SHOUTCAST_METADATA_REQUEST,           ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_shoutcast_metadata, NULL},
    { LISTCLIENTS_RAW_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_show_listeners, NULL},
    { LISTCLIENTS_HTML_REQUEST,             ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_show_listeners, NULL},
    { LISTCLIENTS_JSON_REQUEST,             ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_show_listeners, NULL},
    { STATS_RAW_REQUEST,                    ADMINTYPE_HYBRID,       ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_stats, NULL},
    { STATS_HTML_REQUEST,                   ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_stats, NULL},
    { STATS_JSON_REQUEST,                   ADMINTYPE_HYBRID,       ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_stats, NULL},
    { "stats.xml",                          ADMINTYPE_HYBRID,       ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_stats, NULL},
    { PUBLICSTATS_RAW_REQUEST,              ADMINTYPE_HYBRID,       ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_public_stats, NULL},
    { PUBLICSTATS_JSON_REQUEST,             ADMINTYPE_HYBRID,       ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_public_stats, NULL},
    { QUEUE_RELOAD_RAW_REQUEST,             ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_queue_reload, NULL},
    { QUEUE_RELOAD_HTML_REQUEST,            ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_queue_reload, NULL},
    { QUEUE_RELOAD_JSON_REQUEST,            ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_UNSAFE,   command_queue_reload, NULL},
    { LISTMOUNTS_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { LISTMOUNTS_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { LISTMOUNTS_JSON_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { STREAMLIST_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { STREAMLIST_PLAINTEXT_REQUEST,         ADMINTYPE_GENERAL,      ADMIN_FORMAT_PLAINTEXT,     ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { STREAMLIST_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { STREAMLIST_JSON_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_list_mounts, NULL},
    { LISTENSOCKETLIST_RAW_REQUEST,         ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_list_listen_sockets, NULL},
    { LISTENSOCKETLIST_HTML_REQUEST,        ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_list_listen_sockets, NULL},
    { MOVECLIENTS_RAW_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_HYBRID,   command_move_clients, NULL},
    { MOVECLIENTS_HTML_REQUEST,             ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          ADMINSAFE_HYBRID,   command_move_clients, NULL},
    { MOVECLIENTS_JSON_REQUEST,             ADMINTYPE_HYBRID,       ADMIN_FORMAT_JSON,          ADMINSAFE_HYBRID,   command_move_clients, NULL},
    { KILLCLIENT_RAW_REQUEST,               ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_kill_client, NULL},
    { KILLCLIENT_HTML_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_kill_client, NULL},
    { KILLCLIENT_JSON_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_UNSAFE,   command_kill_client, NULL},
    { KILLSOURCE_RAW_REQUEST,               ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_kill_source, NULL},
    { KILLSOURCE_HTML_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_kill_source, NULL},
    { KILLSOURCE_JSON_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_UNSAFE,   command_kill_source, NULL},
    { DUMPFILECONTROL_RAW_REQUEST,          ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_dumpfile_control, NULL},
    { DUMPFILECONTROL_HTML_REQUEST,         ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_dumpfile_control, NULL},
    { MANAGEAUTH_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_HYBRID,   command_manageauth, NULL},
    { MANAGEAUTH_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_HYBRID,   command_manageauth, NULL},
    { MANAGEAUTH_JSON_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_HYBRID,   command_manageauth, NULL},
    { UPDATEMETADATA_RAW_REQUEST,           ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_updatemetadata, NULL},
    { UPDATEMETADATA_HTML_REQUEST,          ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_updatemetadata, NULL},
    { UPDATEMETADATA_JSON_REQUEST,          ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_updatemetadata, NULL},
    { BUILDM3U_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_buildm3u, NULL},
    { SHOWLOG_RAW_REQUEST,                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_show_log, NULL},
    { SHOWLOG_HTML_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_show_log, NULL},
    { SHOWLOG_JSON_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_show_log, NULL},
    { MARKLOG_RAW_REQUEST,                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_UNSAFE,   command_mark_log, NULL},
    { MARKLOG_HTML_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_UNSAFE,   command_mark_log, NULL},
    { MARKLOG_JSON_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_UNSAFE,   command_mark_log, NULL},
    { DASHBOARD_RAW_REQUEST,                ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_dashboard, NULL},
    { DASHBOARD_HTML_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_dashboard, NULL},
    { DASHBOARD_JSON_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          ADMINSAFE_SAFE,     command_dashboard, NULL},
    { VERSION_RAW_REQUEST,                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           ADMINSAFE_SAFE,     command_version, NULL},
    { VERSION_HTML_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_version, NULL},
    { DEFAULT_HTML_REQUEST,                 ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_default_selector, NULL},
    { DEFAULT_RAW_REQUEST,                  ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          ADMINSAFE_SAFE,     command_default_selector, NULL}
};

static void ui_command(client_t * client, source_t * source, admin_format_t format, resourcematch_extract_t *parameters);

static const admin_command_handler_t ui_handlers[] = {
    { "%s",                                 ADMINTYPE_HYBRID,       ADMIN_FORMAT_AUTO,          ADMINSAFE_SAFE,     NULL, ui_command}
};

static admin_command_table_t command_tables[ADMIN_MAX_COMMAND_TABLES] = {
    {.prefix = NULL, .length = (sizeof(handlers)/sizeof(*handlers)), .handlers = handlers},
    {.prefix = "ui", .length = (sizeof(ui_handlers)/sizeof(*ui_handlers)), .handlers = ui_handlers},
};

static inline int __is_command_table_valid(const admin_command_table_t * table)
{
    if (table == NULL)
        return 0;

    if (table->length == 0 || table->handlers == NULL)
        return 0;

    return 1;
}

static inline const admin_command_table_t * admin_get_table(admin_command_id_t command)
{
    size_t t = (command & 0x00FF0000) >> 16;

    if (t >= (sizeof(command_tables)/sizeof(*command_tables)))
        return NULL;

    if (!__is_command_table_valid(&(command_tables[t])))
        return NULL;

    return &(command_tables[t]);
}

static inline const admin_command_table_t * admin_get_table_by_prefix(const char *command)
{
    const char *end;
    size_t i;
    size_t len;

    end = strchr(command, '/');

    if (end == NULL) {
        for (i = 0; i < (sizeof(command_tables)/sizeof(*command_tables)); i++)
            if (command_tables[i].prefix == NULL && __is_command_table_valid(&(command_tables[i])))
                return &(command_tables[i]);

        return NULL;
    }

    len = end - command;

    for (i = 0; i < (sizeof(command_tables)/sizeof(*command_tables)); i++) {
        if (!__is_command_table_valid(&(command_tables[i])))
            continue;

        if (command_tables[i].prefix != NULL && strlen(command_tables[i].prefix) == len && strncmp(command_tables[i].prefix, command, len) == 0) {
            return &(command_tables[i]);
        }
    }

    return NULL;
}

static inline admin_command_id_t admin_get_command_by_table_and_index(const admin_command_table_t *table, size_t index)
{
    size_t t = table - command_tables;

    if (t >= (sizeof(command_tables)/sizeof(*command_tables)))
        return ADMIN_COMMAND_ERROR;

    if (index > 0x0FFFF)
        return ADMIN_COMMAND_ERROR;

    if (!__is_command_table_valid(table))
        return ADMIN_COMMAND_ERROR;

    return (t << 16) | index;
}

static inline size_t admin_get_index_by_command(admin_command_id_t command)
{
    return command & 0x0FFFF;
}

admin_command_id_t admin_get_command(const char *command)
{
    size_t i;
    const admin_command_table_t *table = admin_get_table_by_prefix(command);
    const char *suffix;

    if (table == NULL)
        return ADMIN_COMMAND_ERROR;

    suffix = strchr(command, '/');
    if (suffix != NULL) {
        suffix++;
    } else {
        suffix = command;
    }

    for (i = 0; i < table->length; i++)
        if (resourcematch_match(table->handlers[i].route, suffix, NULL) == RESOURCEMATCH_MATCH)
            return admin_get_command_by_table_and_index(table, i);

    return ADMIN_COMMAND_ERROR;
}

/* Get the command handler for command or NULL
 */
const admin_command_handler_t* admin_get_handler(admin_command_id_t command)
{
    const admin_command_table_t *table = admin_get_table(command);
    size_t index = admin_get_index_by_command(command);

    if (table == NULL)
        return NULL;

    if (index >= table->length)
        return NULL;

    return &(table->handlers[index]);
}

/* Get the command type for command
 * If the command is invalid, ADMINTYPE_ERROR is returned.
 */
int admin_get_command_type(admin_command_id_t command)
{
    const admin_command_handler_t* handler = admin_get_handler(command);

    if (handler != NULL)
        return handler->type;

    return ADMINTYPE_ERROR;
}

int admin_command_table_register(const char *prefix, size_t handlers_length, const admin_command_handler_t *handlers)
{
    size_t i;

    if (prefix == NULL || handlers_length == 0 || handlers == NULL)
        return -1;

    for (i = 0; i < (sizeof(command_tables)/sizeof(*command_tables)); i++) {
        if (__is_command_table_valid(&(command_tables[i])))
            continue;

        command_tables[i].prefix    = prefix;
        command_tables[i].length    = handlers_length;
        command_tables[i].handlers  = handlers;

        return 0;
    }

    return -1;
}

int admin_command_table_unregister(const char *prefix)
{
    size_t i;

    for (i = 0; i < (sizeof(command_tables)/sizeof(*command_tables)); i++) {
        if (command_tables[i].prefix != NULL && strcmp(command_tables[i].prefix, prefix) == 0) {
            memset(&(command_tables[i]), 0, sizeof(command_tables[i]));
            return 0;
        }
    }

    return -1;
}

/* Enforces requests HTTP unsafe (e.g. POST not GET).
 * Returns true if the request has been handled (rejected) and false if the request is still for open for handling (passed).
 */
static int admin_enforce_unsafe(client_t *client)
{
    // check if the client is using an unsafe method, if so just return.
    if (!(httpp_request_info(client->parser->req_type) & HTTPP_REQUEST_IS_SAFE))
        return 0;

    switch (client->mode) {
        case OMODE_LEGACY:
            // no-op
        break;
        case OMODE_STRICT:
            ICECAST_LOG_WARN("Client %p (role=%H, acl=%H, username=%H) rejected for use of safe method %s on %H",
                    client, client->role, acl_get_name(client->acl), client->username, httpp_getvar(client->parser, HTTPP_VAR_REQ_TYPE), client->uri);
            client_send_error_by_id(client, ICECAST_ERROR_GEN_SAFE_METHOD_ON_UNSAFE_CALL);
            return 1;
        break;
        default:
            ICECAST_LOG_WARN("Client %p (role=%H, acl=%H, username=%H) uses safe method %s on %H",
                    client, client->role, acl_get_name(client->acl), client->username, httpp_getvar(client->parser, HTTPP_VAR_REQ_TYPE), client->uri);
        break;
    }

    return 0;
}

/* build an XML root node including some common tags */
xmlNodePtr admin_build_rootnode(xmlDocPtr doc, const char *name)
{
    xmlNodePtr rootnode = xmlNewDocNode(doc, NULL, XMLSTR(name), NULL);
    xmlNodePtr modules = module_container_get_modulelist_as_xml(global.modulecontainer);

    xmlAddChild(rootnode, modules);

    return rootnode;
}

static inline void admin_build_sourcelist__add_flag(xmlNodePtr parent, source_flags_t flags, source_flags_t flag, bool invert, const char *name)
{
    xmlNodePtr node;
    source_flags_t testflags = SOURCE_FLAGS_GOOD|flag;

    if (invert ? (flags & flag) : !(flags & flag))
        return;

    node = xmlNewTextChild(parent, NULL, XMLSTR("flag"), XMLSTR(name));

    if (invert)
        testflags &= ~flag;

    switch (source_get_health_by_flags(testflags)) {
        case HEALTH_OK:
            xmlSetProp(node, XMLSTR("maintenance-level"), XMLSTR("info"));
            break;
        case HEALTH_WARNING:
            xmlSetProp(node, XMLSTR("maintenance-level"), XMLSTR("warning"));
            break;
        case HEALTH_ERROR:
            xmlSetProp(node, XMLSTR("maintenance-level"), XMLSTR("error"));
            break;
    }
}

/* build an XML doc containing information about currently running sources.
 * If a mountpoint is passed then that source will not be added to the XML
 * doc even if the source is running */
xmlDocPtr admin_build_sourcelist(const char *mount, client_t *client, admin_format_t format)
{
    avl_node *node;
    source_t *source;
    xmlNodePtr xmlnode, srcnode;
    xmlDocPtr doc;
    char buf[22];
    time_t now = time(NULL);

    doc = xmlNewDoc (XMLSTR("1.0"));
    xmlnode = admin_build_rootnode(doc, "icestats");
    xmlDocSetRootElement(doc, xmlnode);

    if (mount) {
        xmlNewTextChild (xmlnode, NULL, XMLSTR("current_source"), XMLSTR(mount));
    }

    node = avl_get_first(global.source_tree);
    while(node) {
        source = (source_t *)node->key;
        if (mount && strcmp (mount, source->mount) == 0)
        {
            node = avl_get_next (node);
            continue;
        }

        if (source->running || source->on_demand)
        {
            ice_config_t *config;
            mount_proxy *mountinfo;
            acl_t *acl = NULL;

            srcnode = xmlNewChild(xmlnode, NULL, XMLSTR("source"), NULL);
            xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));

            if (source->fallback_mount) {
                xmlNewTextChild(srcnode, NULL, XMLSTR("fallback"), XMLSTR(source->fallback_mount));
            } else {
                if (format == ADMIN_FORMAT_RAW && client->mode != OMODE_STRICT)
                    xmlNewTextChild(srcnode, NULL, XMLSTR("fallback"), XMLSTR(""));
            }
            snprintf(buf, sizeof(buf), "%lu", source->listeners);
            xmlNewTextChild(srcnode, NULL, XMLSTR("listeners"), XMLSTR(buf));

            config = config_get_config();
            mountinfo = config_find_mount(config, source->mount, MOUNT_TYPE_NORMAL);
            if (mountinfo)
                acl = auth_stack_get_anonymous_acl(mountinfo->authstack, httpp_req_get);
            if (!acl)
                acl = auth_stack_get_anonymous_acl(config->authstack, httpp_req_get);
            if (acl && acl_test_web(acl) == ACL_POLICY_DENY) {
                xmlNewTextChild(srcnode, NULL, XMLSTR("authenticator"), XMLSTR("(dummy)"));
            }
            acl_release(acl);
            config_release_config();

            if (source->running) {
                const source_flags_t flags = source->flags;
                xmlNodePtr maintenancenode;

                if (source->client) {
                    snprintf(buf, sizeof(buf), "%lu",
                        (unsigned long)(now - source->con->con_time));
                    if (format == ADMIN_FORMAT_RAW && client->mode != OMODE_STRICT) {
                        xmlNewTextChild(srcnode, NULL, XMLSTR("Connected"), XMLSTR(buf));
                    } else {
                        xmlNewTextChild(srcnode, NULL, XMLSTR("connected"), XMLSTR(buf));
                    }
                }
                xmlNewTextChild(srcnode, NULL, XMLSTR("content-type"),
                    XMLSTR(source->format->contenttype));

                switch (source_get_health(source)) {
                    case HEALTH_OK:
                        xmlNewTextChild(srcnode, NULL, XMLSTR("health"), XMLSTR("green"));
                        break;
                    case HEALTH_WARNING:
                        xmlNewTextChild(srcnode, NULL, XMLSTR("health"), XMLSTR("yellow"));
                        break;
                    case HEALTH_ERROR:
                        xmlNewTextChild(srcnode, NULL, XMLSTR("health"), XMLSTR("red"));
                        break;
                }
                maintenancenode = xmlNewChild(srcnode, NULL, XMLSTR("maintenance"), NULL);
                xmlSetProp(maintenancenode, XMLSTR("comment"), XMLSTR("This is an experimental node. Do not use!"));

                admin_build_sourcelist__add_flag(maintenancenode, flags, SOURCE_FLAG_GOT_DATA, true, "no-got-data");
                admin_build_sourcelist__add_flag(maintenancenode, flags, SOURCE_FLAG_FORMAT_GENERIC, false, "format-generic");
                admin_build_sourcelist__add_flag(maintenancenode, flags, SOURCE_FLAG_LEGACY_METADATA, false, "legacy-metadata");
            }

            snprintf(buf, sizeof(buf), "%"PRIu64, source->dumpfile_written);
            xmlNewTextChild(srcnode, NULL, XMLSTR("dumpfile_written"), XMLSTR(buf));
        }
        node = avl_get_next(node);
    }
    return(doc);
}

void admin_send_response(xmlDocPtr       doc,
                         client_t       *client,
                         admin_format_t  response,
                         const char     *xslt_template)
{
    if (response == ADMIN_FORMAT_RAW || response == ADMIN_FORMAT_JSON) {

        if (response == ADMIN_FORMAT_RAW) {
            xmlChar *buff = NULL;
            int len = 0;
            xmlDocDumpMemory(doc, &buff, &len);
            client_send_buffer(client, 200, "text/xml", "utf-8", (const char *)buff, len, NULL);
            xmlFree(buff);
        } else {
            xmlNodePtr xmlroot = xmlDocGetRootElement(doc);
            const char *ns;
            char *json;

            if (strcmp((const char *)xmlroot->name, "iceresponse") == 0) {
                ns = XMLNS_LEGACY_RESPONSE;
            } else {
                ns = XMLNS_LEGACY_STATS;
            }

            json = xml2json_render_doc_simple(doc, ns);
            client_send_buffer(client, 200, "application/json", "utf-8", json, -1, "Warning: 299 - \"JSON rendering is experimental\"\r\n");
            free(json);
        }
    } else if (response == ADMIN_FORMAT_HTML) {
        char *fullpath_xslt_template;
        size_t fullpath_xslt_template_len;
        ice_config_t *config = config_get_config();
        const char *showall;
        const char *mount;

        fullpath_xslt_template_len = strlen(config->adminroot_dir) + strlen(xslt_template) + strlen(PATH_SEPARATOR) + 1;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s",
            config->adminroot_dir, PATH_SEPARATOR, xslt_template);
        config_release_config();

        ICECAST_LOG_DEBUG("Sending XSLT (%s)", fullpath_xslt_template);

        COMMAND_OPTIONAL(client, "showall", showall);
        COMMAND_OPTIONAL(client, "mount", mount);

        if (showall && util_str_to_bool(showall)) {
            const char *params[] = {"param-has-mount", mount ? "'true'" : NULL, "param-showall", "'true'", NULL};
            xslt_transform(doc, fullpath_xslt_template, client, 200, NULL, params);
        } else {
            const char *params[] = {"param-has-mount", mount ? "'true'" : NULL, "param-showall", NULL, NULL};
            xslt_transform(doc, fullpath_xslt_template, client, 200, NULL, params);
        }
        free(fullpath_xslt_template);
    }
}


static void admin_send_response_simple(client_t *client, source_t *source, admin_format_t response, const char *message, int success)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = admin_build_rootnode(doc, "iceresponse");
    xmlNewTextChild(node, NULL, XMLSTR("message"), XMLSTR(message));
    xmlNewTextChild(node, NULL, XMLSTR("return"), XMLSTR(success ? "1" : "0"));
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response, ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}

void admin_handle_request(client_t *client, const char *uri)
{
    const char *mount;
    const admin_command_handler_t* handler;
    source_t *source = NULL;
    admin_format_t format;

    ICECAST_LOG_DEBUG("Got admin request '%s'", uri);

    handler = admin_get_handler(client->admin_command);

    /* Check if admin command is valid */
    if (handler == NULL || (handler->function == NULL && handler->function_with_parameters == NULL)) {
        ICECAST_LOG_ERROR("Error parsing command string or unrecognised command: %H",
                uri);
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND);
        return;
    }

    /* Check ACL */
    if (acl_test_admin(client->acl, client->admin_command) != ACL_POLICY_ALLOW) {

        /* ACL disallows, check exceptions */
        if ((handler->function == command_metadata && handler->format == ADMIN_FORMAT_RAW) &&
            (acl_test_method(client->acl, httpp_req_source) == ACL_POLICY_ALLOW ||
             acl_test_method(client->acl, httpp_req_put)    == ACL_POLICY_ALLOW)) {
            ICECAST_LOG_DEBUG("Granted right to call COMMAND_RAW_METADATA_UPDATE to "
                "client because it is allowed to do SOURCE or PUT.");
        } else {
            ICECAST_LOG_DEBUG("Client needs to authenticate.");
            auth_reject_client_on_deny(client);
            return;
        }
    }

    COMMAND_OPTIONAL(client, "mount", mount);

    /* Find mountpoint source */
    if(mount != NULL) {

        /* This is a mount request, handle it as such */
        avl_tree_rlock(global.source_tree);
        source = source_find_mount_raw(mount);

        /* No Source found */
        if (source == NULL) {
            avl_tree_unlock(global.source_tree);
            ICECAST_LOG_WARN("Admin command \"%H\" on non-existent source \"%H\"",
                    uri, mount);
            client_send_error_by_id(client, ICECAST_ERROR_ADMIN_SOURCE_DOES_NOT_EXIST);
            return;
        } /* No Source running */
        else if (source->running == 0 && source->on_demand == 0) {
            avl_tree_unlock(global.source_tree);
            ICECAST_LOG_INFO("Received admin command \"%H\" on unavailable mount \"%H\"",
                    uri, mount);
            client_send_error_by_id(client, ICECAST_ERROR_ADMIN_SOURCE_IS_NOT_AVAILABLE);
            return;
        }
        ICECAST_LOG_INFO("Received admin command %H on mount '%s'",
                    uri, mount);
    }

    if (handler->type == ADMINTYPE_MOUNT && !source) {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_MISSING_PARAMETER);
        return;
    }

    if (handler->safeness == ADMINSAFE_UNSAFE) {
        if (admin_enforce_unsafe(client))
            return;
    }

    if (handler->format == ADMIN_FORMAT_AUTO) {
        format = client_get_admin_format_by_content_negotiation(client);
    } else {
        format = handler->format;
    }

    switch (client->parser->req_type) {
        case httpp_req_get:
        case httpp_req_post:
            if (handler->function) {
                handler->function(client, source, format);
            } else {
                resourcematch_extract_t *extract = NULL;
                const char *suffix = strchr(uri, '/');

                if (!suffix) {
                    client_send_error_by_id(client, ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND);
                } else {
                    suffix++;

                    if (resourcematch_match(handler->route, suffix, &extract) == RESOURCEMATCH_MATCH) {
                        handler->function_with_parameters(client, source, format, extract);
                        resourcematch_extract_free(extract);
                    } else {
                        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND);
                    }
                }
            }
        break;
        case httpp_req_options:
            client_send_204(client);
        break;
        default:
            ICECAST_LOG_ERROR("Wrong request type from client");
            client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
        break;
    }

    if (source) {
        avl_tree_unlock(global.source_tree);
    }
    return;
}

static void html_success(client_t *client, source_t *source, admin_format_t response, char *message)
{
    if (client->mode == OMODE_STRICT || (response != ADMIN_FORMAT_RAW && response != ADMIN_FORMAT_HTML)) {
        admin_send_response_simple(client, source, response, message, 1);
    } else {
        char buf[256];
        int ret;

        ret = snprintf(buf, sizeof(buf), "<html><head><title>Admin request successful</title></head><body><p>%s</p></body></html>", message);
        if (ret < 0 || ret >= (ssize_t)sizeof(buf)) {
            client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
            return;
        }

        client_send_buffer(client, 200, "text/html", "utf-8", buf, ret, NULL);
    }
}


static void command_default_selector    (client_t *client, source_t *source, admin_format_t response)
{
    if (client->mode == OMODE_LEGACY) {
        command_stats(client, source, response);
    } else {
        command_dashboard(client, source, response);
    }
}


static void command_move_clients(client_t   *client,
                                 source_t   *source,
                                 admin_format_t response)
{
    const char *dest_source;
    const char *idtext = NULL;
    const char *directiontext = NULL;
    connection_id_t id;
    source_t *dest;
    char buf[255];
    int parameters_passed = 0;

    ICECAST_LOG_DEBUG("Doing optional check");
    if((COMMAND_OPTIONAL(client, "destination", dest_source))) {
        parameters_passed = 1;
    }
    if ((COMMAND_OPTIONAL(client, "id", idtext))) {
        id = atoi(idtext);
    } else {
        idtext = NULL;
    }
    COMMAND_OPTIONAL(client, "direction", directiontext);

    ICECAST_LOG_DEBUG("Done optional check (%d)", parameters_passed);
    if (!parameters_passed) {
        xmlDocPtr doc = admin_build_sourcelist(source->mount, client, response);

        if (idtext) {
            xmlNodePtr root = xmlDocGetRootElement(doc);
            xmlNewTextChild(root, NULL, XMLSTR("param-id"), XMLSTR(idtext));
        }
        admin_send_response(doc, client, response,
             MOVECLIENTS_HTML_REQUEST);
        xmlFreeDoc(doc);
        return;
    }

    if (admin_enforce_unsafe(client))
        return;

    dest = source_find_mount(dest_source);

    if (dest == NULL) {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_NO_SUCH_DESTINATION);
        return;
    }

    if (strcmp(dest->mount, source->mount) == 0) {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_SUPPLIED_MOUNTPOINTS_ARE_IDENTICAL);
        return;
    }

    if (dest->running == 0 && dest->on_demand == 0) {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_DEST_NOT_RUNNING);
        return;
    }

    ICECAST_LOG_INFO("source is \"%s\", destination is \"%s\"", source->mount, dest->mount);

    source_move_clients(source, dest, idtext ? &id : NULL, navigation_str_to_direction(directiontext, NAVIGATION_DIRECTION_REPLACE_ALL));

    snprintf(buf, sizeof(buf), "Clients moved from %s to %s",
        source->mount, dest_source);

    admin_send_response_simple(client, source, response, buf, 1);
}

static inline xmlNodePtr __add_listener(client_t        *client,
                                        xmlNodePtr      parent,
                                        time_t          now,
                                        operation_mode  mode)
{
    const char *tmp;
    xmlNodePtr node;
    char buf[22];

    /* TODO: kh has support for a child node "lag". We should add that.
     * BEFORE RELEASE NEXT DOCUMENT #2097: Changed case of child nodes to lower case.
     * The case of <ID>, <IP>, <UserAgent> and <Connected> got changed to lower case.
     */

    node = xmlNewChild(parent, NULL, XMLSTR("listener"), NULL);
    if (!node)
        return NULL;

    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf)-1, "%lu", client->con->id);
    xmlSetProp(node, XMLSTR("id"), XMLSTR(buf));
    xmlNewTextChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "ID" : "id"), XMLSTR(buf));

    xmlNewTextChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "IP" : "ip"), XMLSTR(client->con->ip));

    tmp = httpp_getvar(client->parser, "user-agent");
    if (tmp)
        xmlNewTextChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "UserAgent" : "useragent"), XMLSTR(tmp));

    tmp = httpp_getvar(client->parser, "referer");
    if (tmp)
        xmlNewTextChild(node, NULL, XMLSTR("referer"), XMLSTR(tmp));

    tmp = httpp_getvar(client->parser, "host");
    if (tmp)
        xmlNewTextChild(node, NULL, XMLSTR("host"), XMLSTR(tmp));

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(now - client->con->con_time));
    xmlNewTextChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "Connected" : "connected"), XMLSTR(buf));

    if (client->username)
        xmlNewTextChild(node, NULL, XMLSTR("username"), XMLSTR(client->username));

    if (client->role)
        xmlNewTextChild(node, NULL, XMLSTR("role"), XMLSTR(client->role));

    if (client->acl && acl_get_name(client->acl))
        xmlNewTextChild(node, NULL, XMLSTR("acl"), XMLSTR(acl_get_name(client->acl)));

    xmlNewTextChild(node, NULL, XMLSTR("tls"), XMLSTR(client->con->tls ? "true" : "false"));

    xmlNewTextChild(node, NULL, XMLSTR("protocol"), XMLSTR(client_protocol_to_string(client->protocol)));

    do {
        xmlNodePtr history = xmlNewChild(node, NULL, XMLSTR("history"), NULL);
        size_t i;

        for (i = 0; i < client->history.fill; i++) {
            xmlNewTextChild(history, NULL, XMLSTR("mount"), XMLSTR(mount_identifier_get_mount(client->history.history[i])));
        }
    } while (0);

    return node;
}

void admin_add_listeners_to_mount(source_t          *source,
                                  xmlNodePtr        parent,
                                  operation_mode    mode)
{
    time_t now = time(NULL);
    avl_node *client_node;

    avl_tree_rlock(source->client_tree);
    client_node = avl_get_first(source->client_tree);
    while(client_node) {
        __add_listener((client_t *)client_node->key, parent, now, mode);
        client_node = avl_get_next(client_node);
    }
    avl_tree_unlock(source->client_tree);
}

static void command_show_listeners(client_t *client,
                                   source_t *source,
                                   admin_format_t response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;
    char buf[22];

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = admin_build_rootnode(doc, "icestats");
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    xmlDocSetRootElement(doc, node);

    memset(buf, '\000', sizeof(buf));
    snprintf (buf, sizeof(buf), "%lu", source->listeners);
    /* BEFORE RELEASE NEXT DOCUMENT #2097: Changed "Listeners" to lower case. */
    xmlNewTextChild(srcnode, NULL, XMLSTR(client->mode == OMODE_LEGACY ? "Listeners" : "listeners"), XMLSTR(buf));

    admin_add_listeners_to_mount(source, srcnode, client->mode);

    admin_send_response(doc, client, response,
        LISTCLIENTS_HTML_REQUEST);
    xmlFreeDoc(doc);
}

static void command_buildm3u(client_t *client, source_t *source, admin_format_t format)
{
    const char *mount = source->mount;
    const char *username = NULL;
    const char *password = NULL;
    char buffer[512];

    COMMAND_REQUIRE(client, "username", username);
    COMMAND_REQUIRE(client, "password", password);

    client_get_baseurl(client, NULL, buffer, sizeof(buffer), username, password, NULL, mount, "\r\n");
    client_send_buffer(client, 200, "audio/x-mpegurl", NULL, buffer, -1, "Content-Disposition: attachment; filename=listen.m3u\r\n");
}

xmlNodePtr admin_add_role_to_authentication(auth_t *auth, xmlNodePtr parent)
{
    xmlNodePtr rolenode = xmlNewChild(parent, NULL, XMLSTR("role"), NULL);
    char idbuf[32];

    snprintf(idbuf, sizeof(idbuf), "%lu", auth->id);
    xmlSetProp(rolenode, XMLSTR("id"), XMLSTR(idbuf));

    if (auth->type)
        xmlSetProp(rolenode, XMLSTR("type"), XMLSTR(auth->type));
    if (auth->role)
        xmlSetProp(rolenode, XMLSTR("name"), XMLSTR(auth->role));
    if (auth->management_url)
        xmlSetProp(rolenode, XMLSTR("management-url"), XMLSTR(auth->management_url));

    xmlSetProp(rolenode, XMLSTR("can-adduser"), XMLSTR(auth->adduser ? "true" : "false"));
    xmlSetProp(rolenode, XMLSTR("can-deleteuser"), XMLSTR(auth->deleteuser ? "true" : "false"));
    xmlSetProp(rolenode, XMLSTR("can-listuser"), XMLSTR(auth->listuser ? "true" : "false"));

    return rolenode;
}

static void command_manageauth(client_t *client, source_t *source, admin_format_t response)
{
    xmlDocPtr doc;
    xmlNodePtr node, rolenode, usersnode;
    const char *action = NULL;
    const char *username = NULL;
    const char *idstring = NULL;
    int ret = AUTH_OK;
    int error_id = ICECAST_ERROR_ADMIN_missing_parameter;
    long unsigned int id;
    ice_config_t *config = config_get_config();
    auth_t *auth;

    do {
        /* get id */
        COMMAND_REQUIRE(client, "id", idstring);
        id = atol(idstring);

        /* no find a auth_t for that id by looking up the config */
        /* globals first */
        auth = auth_stack_getbyid(config->authstack, id);
        /* now mounts */
        if (!auth) {
            mount_proxy *mount = config->mounts;
            while (mount) {
                auth = auth_stack_getbyid(mount->authstack, id);
                if (auth)
                    break;
                mount = mount->next;
            }
        }

        /* check if we found one */
        if (auth == NULL) {
            ICECAST_LOG_WARN("Client requested management for unknown role %lu", id);
            error_id = ICECAST_ERROR_ADMIN_ROLEMGN_ROLE_NOT_FOUND;
            break;
        }

        COMMAND_OPTIONAL(client, "action", action);
        COMMAND_OPTIONAL(client, "username", username);

        if (action == NULL)
            action = "list";

        if (!strcmp(action, "add")) {
            const char *password = NULL;

            if (admin_enforce_unsafe(client))
                return;

            COMMAND_OPTIONAL(client, "password", password);

            if (username == NULL || password == NULL) {
                ICECAST_LOG_WARN("manage auth request add for %lu but no user/pass", id);
                break;
            }

            if (!auth->adduser) {
                error_id = ICECAST_ERROR_ADMIN_ROLEMGN_ADD_NOSYS;
                break;
            }

            ret = auth->adduser(auth, username, password);
            if (response == ADMIN_FORMAT_JSON || client->mode == OMODE_STRICT) {
                if (ret == AUTH_FAILED) {
                    admin_send_response_simple(client, source, response, "User add failed - check the icecast error log", 0);
                } else if (ret == AUTH_USERADDED) {
                    admin_send_response_simple(client, source, response, "User added", 1);
                } else if (ret == AUTH_USEREXISTS) {
                    admin_send_response_simple(client, source, response, "User already exists - not added", 0);
                }
                config_release_config();
                auth_release(auth);
                return;
            }
        } else if (!strcmp(action, "delete")) {
            if (admin_enforce_unsafe(client))
                return;

            if (username == NULL) {
                ICECAST_LOG_WARN("manage auth request delete for %lu but no username", id);
                break;
            }

            if (!auth->deleteuser) {
                error_id = ICECAST_ERROR_ADMIN_ROLEMGN_DELETE_NOSYS;
                break;
            }

            ret = auth->deleteuser(auth, username);
            if (response == ADMIN_FORMAT_JSON || client->mode == OMODE_STRICT) {
                if (ret == AUTH_FAILED) {
                    admin_send_response_simple(client, source, response, "User delete failed - check the icecast error log", 0);
                } else if (ret == AUTH_USERDELETED) {
                    admin_send_response_simple(client, source, response, "User deleted", 1);
                }
                config_release_config();
                auth_release(auth);
                return;
            }
        }

        doc = xmlNewDoc(XMLSTR("1.0"));
        node = admin_build_rootnode(doc, "icestats");

        rolenode = admin_add_role_to_authentication(auth, node);

        xmlDocSetRootElement(doc, node);

        if (auth->listuser) {
            usersnode = xmlNewChild(rolenode, NULL, XMLSTR("users"), NULL);
            auth->listuser(auth, usersnode);
        }

        config_release_config();
        auth_release(auth);

        admin_send_response(doc, client, response,
            MANAGEAUTH_HTML_REQUEST);
        xmlFreeDoc(doc);
        return;
    } while (0);

    config_release_config();
    auth_release(auth);
    client_send_error_by_id(client, error_id);
}

static void command_kill_source(client_t *client,
                                source_t *source,
                                admin_format_t response)
{
    source->running = 0;

    admin_send_response_simple(client, source, response, "Source Removed", 1);
}

static void command_dumpfile_control    (client_t *client, source_t *source, admin_format_t response)
{
    const char *action;
    COMMAND_REQUIRE(client, "action", action);

    if (strcmp(action, "kill") == 0) {
        source_kill_dumpfile(source);
        admin_send_response_simple(client, source, response, "Dumpfile killed.", 1);
    } else {
        admin_send_response_simple(client, source, response, "No such action", 0);
    }
}

static void command_kill_client(client_t *client,
                                source_t *source,
                                admin_format_t response)
{
    const char *idtext;
    int id;
    client_t *listener;
    char buf[50] = "";

    COMMAND_REQUIRE(client, "id", idtext);

    id = atoi(idtext);

    listener = source_find_client(source, id);

    ICECAST_LOG_DEBUG("Response is %d", response);

    if (listener != NULL) {
        ICECAST_LOG_INFO("Admin request: client %d removed", id);

        /* This tags it for removal on the next iteration of the main source
         * loop
         */
        listener->con->error = 1;
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d removed", id);
        admin_send_response_simple(client, source, response, buf, 1);
    } else {
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d not found", id);
        admin_send_response_simple(client, source, response, buf, 0);
    }
}

static void command_fallback(client_t *client,
                             source_t *source,
                             admin_format_t response)
{
    const char *fallback;

    ICECAST_LOG_DEBUG("Got fallback request");

    if (client->mode == OMODE_STRICT) {
        if (!(COMMAND_OPTIONAL(client, "fallback", fallback))) {
            xmlDocPtr doc = admin_build_sourcelist(source->mount, client, response);
            admin_send_response(doc, client, response, FALLBACK_HTML_REQUEST);
            xmlFreeDoc(doc);
            return;
        }
    }

    if (admin_enforce_unsafe(client))
        return;

    COMMAND_REQUIRE(client, "fallback", fallback);

    util_replace_string(&(source->fallback_mount), fallback);

    html_success(client, source, response, "Fallback configured");
}

static void command_metadata(client_t *client,
                             source_t *source,
                             admin_format_t response)
{
    const char *action;
    const char *song, *title, *artist, *charset, *url;
    format_plugin_t *plugin;
    int same_ip = 1;

    ICECAST_LOG_DEBUG("Got metadata update request");

    if (source->parser && source->parser->req_type == httpp_req_put) {
        ICECAST_LOG_ERROR("Got legacy SOURCE-style metadata update command on "
            "source connected with PUT at mountpoint %H", source->mount);
        source_set_flags(source, SOURCE_FLAG_LEGACY_METADATA);
    }

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_OPTIONAL(client, "song", song);
    COMMAND_OPTIONAL(client, "title", title);
    COMMAND_OPTIONAL(client, "artist", artist);
    COMMAND_OPTIONAL(client, "charset", charset);
    COMMAND_OPTIONAL(client, "url", url);

    if (strcmp (action, "updinfo") != 0) {
        admin_send_response_simple(client, source, response, "No such action", 0);
        return;
    }

    plugin = source->format;
    if (source->client && strcmp(client->con->ip, source->client->con->ip) != 0)
        if (response == ADMIN_FORMAT_RAW && acl_test_admin(client->acl, client->admin_command) != ACL_POLICY_ALLOW)
            same_ip = 0;

    if (same_ip && plugin && plugin->set_tag) {
        if (song) {
            if (artist || title) {
                ICECAST_LOG_WARN("Metadata request mountpoint %H contains \"song\" but also \"artist\" and/or \"title\"", source->mount);
            }

            plugin->set_tag (plugin, "song", song, charset);
            ICECAST_LOG_INFO("Metadata on mountpoint %H changed to \"%H\"", source->mount, song);
        } else {
            if (artist && title) {
                plugin->set_tag(plugin, "title", title, charset);
                plugin->set_tag(plugin, "artist", artist, charset);
                ICECAST_LOG_INFO("Metadata on mountpoint %H changed to \"%H - %H\"",
                    source->mount, artist, title);
            }
        }
        if (url) {
            plugin->set_tag(plugin, "url", url, charset);
            ICECAST_LOG_INFO("Metadata (url) on mountpoint %H changed to \"%H\"", source->mount, url);
        }
        /* updates are now done, let them be pushed into the stream */
        plugin->set_tag (plugin, NULL, NULL, NULL);
    } else {
        ICECAST_LOG_ERROR("Got legacy shoutcast-style metadata update command "
            "on source that does not accept it at mountpoint %H", source->mount);
        source_set_flags(source, SOURCE_FLAG_LEGACY_METADATA);

        admin_send_response_simple(client, source, response, "Mountpoint will not accept URL updates", 1);
        return;
    }

    admin_send_response_simple(client, source, response, "Metadata update successful", 1);
}

static void command_shoutcast_metadata(client_t *client,
                                       source_t *source,
                                       admin_format_t response)
{
    const char *action;
    const char *value;
    int same_ip = 1;

    ICECAST_LOG_DEBUG("Got shoutcast metadata update request");

    if (source->shoutcast_compat == 0) {
        ICECAST_LOG_ERROR("illegal change of metadata on non-shoutcast "
                        "compatible stream");
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_METADAT_BADCALL);
        return;
    }

    if (source->parser->req_type == httpp_req_put) {
        ICECAST_LOG_ERROR("Got legacy shoutcast-style metadata update command "
            "on source connected with PUT at mountpoint %s", source->mount);
    }

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_REQUIRE(client, "song", value);

    if (strcmp (action, "updinfo") != 0) {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_METADAT_NO_SUCH_ACTION);
        return;
    }
    if (source->client && strcmp (client->con->ip, source->client->con->ip) != 0)
        if (acl_test_admin(client->acl, client->admin_command) != ACL_POLICY_ALLOW)
            same_ip = 0;

    if (same_ip && source->format && source->format->set_tag) {
        source->format->set_tag (source->format, "title", value, NULL);
        source->format->set_tag (source->format, NULL, NULL, NULL);

        ICECAST_LOG_DEBUG("Metadata on mountpoint %s changed to \"%s\"",
                source->mount, value);
        html_success(client, source, response, "Metadata update successful");
    } else {
        ICECAST_LOG_ERROR("Got legacy shoutcast-style metadata update command "
            "on source that does not accept it at mountpoint %s", source->mount);

        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_MOUNT_NOT_ACCEPT_URL_UPDATES);
    }
}

static void command_stats(client_t *client, source_t *source, admin_format_t response)
{
    unsigned int flags = (source) ? STATS_XML_FLAG_SHOW_HIDDEN|STATS_XML_FLAG_SHOW_LISTENERS : STATS_XML_FLAG_SHOW_HIDDEN;
    const char *mount = (source) ? source->mount : NULL;
    xmlDocPtr doc;

    ICECAST_LOG_DEBUG("Stats request, sending xml stats");

    doc = stats_get_xml(flags, mount, client);
    admin_send_response(doc, client, response, STATS_HTML_REQUEST);
    xmlFreeDoc(doc);
    return;
}

static void command_public_stats        (client_t *client, source_t *source, admin_format_t response)
{
    const char *mount = (source) ? source->mount : NULL;
    xmlDocPtr doc = stats_get_xml(STATS_XML_FLAG_PUBLIC_VIEW, mount, client);
    admin_send_response(doc, client, response, STATS_HTML_REQUEST);
    xmlFreeDoc(doc);
    return;
}

static void command_queue_reload(client_t *client, source_t *source, admin_format_t response)
{
    global_lock();
    global.schedule_config_reread = 1;
    global_unlock();

    admin_send_response_simple(client, source, response, "Config reload queued", 1);
}


static void command_list_mounts(client_t *client, source_t *source, admin_format_t response)
{
    ICECAST_LOG_DEBUG("List mounts request");

    if (response == ADMIN_FORMAT_PLAINTEXT) {
        ssize_t ret = util_http_build_header(client->refbuf->data,
                                             PER_CLIENT_REFBUF_SIZE, 0,
                                             0, 200, NULL,
                                             "text/plain", "utf-8",
                                             "", NULL, client);

        if (ret == -1 || ret >= PER_CLIENT_REFBUF_SIZE) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
            return;
        }

        client->refbuf->len = strlen (client->refbuf->data);
        client->respcode = 200;

        client->refbuf->next = stats_get_streams ();
        fserve_add_client (client, NULL);
    } else {
        xmlDocPtr doc;
        avl_tree_rlock(global.source_tree);
        doc = admin_build_sourcelist(NULL, client, response);
        avl_tree_unlock(global.source_tree);

        admin_send_response(doc, client, response,
            LISTMOUNTS_HTML_REQUEST);
        xmlFreeDoc(doc);
    }
}

static void command_list_listen_sockets(client_t *client, source_t *source, admin_format_t response)
{
    reportxml_t *report = client_get_empty_reportxml();
    listensocket_t ** sockets;
    size_t i;

    global_lock();
    sockets = listensocket_container_list_sockets(global.listensockets);
    global_unlock();

    for (i = 0; sockets[i]; i++) {
        const listener_t * listener = listensocket_get_listener(sockets[i]);
        reportxml_node_t * incident = client_add_empty_incident(report, "ee231290-81c6-484a-836c-20a00ad09898", NULL, NULL);
        reportxml_node_t * resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
        reportxml_node_t * config = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);

        reportxml_node_set_attribute(resource, "type", "result");
        reportxml_node_set_attribute(config, "type", "structure");
        reportxml_node_set_attribute(config, "member", "config");

        reportxml_node_add_child(resource, config);
        reportxml_node_add_child(incident, resource);
        refobject_unref(incident);

        reportxml_helper_add_value_enum(resource, "type", listensocket_type_to_string(listener->type));
        reportxml_helper_add_value_enum(resource, "family", sock_family_to_string(listensocket_get_family(sockets[i])));
        reportxml_helper_add_value_string(resource, "id", listener->id);
        reportxml_helper_add_value_string(resource, "on_behalf_of", listener->on_behalf_of);

        if (listener->port > 0) {
            reportxml_helper_add_value_int(config, "port", listener->port);
        } else {
            reportxml_helper_add_value(config, "int", "port", NULL);
        }

        if (listener->so_sndbuf) {
            reportxml_helper_add_value_int(config, "so_sndbuf", listener->so_sndbuf);
        } else {
            reportxml_helper_add_value(config, "int", "so_sndbuf", NULL);
        }

        if (listener->listen_backlog > 0) {
            reportxml_helper_add_value_int(config, "listen_backlog", listener->listen_backlog);
        } else {
            reportxml_helper_add_value(config, "int", "listen_backlog", NULL);
        }

        reportxml_helper_add_value_string(config, "bind_address", listener->bind_address);
        reportxml_helper_add_value_boolean(config, "shoutcast_compat", listener->shoutcast_compat);
        reportxml_helper_add_value_string(config, "shoutcast_mount", listener->shoutcast_mount);
        reportxml_helper_add_value_enum(config, "tlsmode", listensocket_tlsmode_to_string(listener->tls));

        if (listener->authstack) {
            reportxml_node_t * extension = reportxml_node_new(REPORTXML_NODE_TYPE_EXTENSION, NULL, NULL, NULL);
            xmlNodePtr xmlroot = xmlNewNode(NULL, XMLSTR("icestats"));

            reportxml_node_set_attribute(extension, "application", ADMIN_ICESTATS_LEGACY_EXTENSION_APPLICATION);
            reportxml_node_add_child(resource, extension);

            xmlSetProp(xmlroot, XMLSTR("xmlns"), XMLSTR(XMLNS_LEGACY_STATS));

            stats_add_authstack(listener->authstack, xmlroot);

            reportxml_node_add_xml_child(extension, xmlroot);
            refobject_unref(extension);
            xmlFreeNode(xmlroot);
        }

        if (listener->http_headers) {
            reportxml_node_t * headers = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
            ice_config_http_header_t *cur;

            reportxml_node_set_attribute(headers, "member", "headers");
            reportxml_node_set_attribute(headers, "type", "unordered-list");
            reportxml_node_add_child(resource, headers);

            for (cur = listener->http_headers; cur; cur = cur->next) {
                reportxml_node_t * header = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
                reportxml_node_set_attribute(header, "type", "structure");
                reportxml_node_add_child(headers, header);

                switch (cur->type) {
                    case HTTP_HEADER_TYPE_STATIC:
                        reportxml_helper_add_value_enum(header, "type", "static");
                        break;
                    case HTTP_HEADER_TYPE_CORS:
                        reportxml_helper_add_value_enum(header, "type", "cors");
                        break;
                }

                reportxml_helper_add_value_string(header, "name", cur->name);
                reportxml_helper_add_value_string(header, "value", cur->value);

                if (cur->status > 100) {
                    reportxml_helper_add_value_int(header, "status", cur->status > 100);
                } else {
                    reportxml_helper_add_value(header, "int", "status", NULL);
                }

                reportxml_helper_add_value_string(config, "shoutcast_mount", listener->shoutcast_mount);
                refobject_unref(header);
            }

            refobject_unref(headers);
        }

        refobject_unref(config);
        refobject_unref(resource);
        listensocket_release_listener(sockets[i]);
        refobject_unref(sockets[i]);
    }

    free(sockets);

    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, LISTENSOCKETLIST_HTML_REQUEST, response, 200, NULL);
    refobject_unref(report);
}

static void command_updatemetadata(client_t *client,
                                   source_t *source,
                                   admin_format_t response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = admin_build_rootnode(doc, "icestats");
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    if (source->running) {
        xmlNewTextChild(srcnode, NULL, XMLSTR("content-type"), XMLSTR(source->format->contenttype));
    }
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response,
        UPDATEMETADATA_HTML_REQUEST);
    xmlFreeDoc(doc);
}

static void command_show_log            (client_t *client, source_t *source, admin_format_t response)
{
    static const char *logs[] = {"error", "access", "playlist"};
    reportxml_t *report;
    reportxml_node_t *incident;
    reportxml_node_t *resource;
    reportxml_node_t *loglist_value_list;
    reportxml_node_t *logfile;
    reportxml_node_t *parent;
    const char *logfilestring;
    char **loglines;
    int logid;
    size_t i;

    COMMAND_OPTIONAL(client, "logfile", logfilestring);

    if (!logfilestring) {
        logfilestring = "error";
        logid = errorlog;
    } else {
        logid = logging_str2logid(logfilestring);
    }

    if (logid < 0) {
        client_send_error_by_id(client, ICECAST_ERROR_FSERV_FILE_NOT_FOUND);
        return;
    }

    report = client_get_reportxml("b20a2bf2-1278-448c-81f3-58183d837a86", NULL, NULL);

    incident = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);


    resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(resource, "type", "related");
    reportxml_node_set_attribute(resource, "name", "logfiles");

    loglist_value_list = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(loglist_value_list, "type", "list");

    for (i = 0; i < (sizeof(logs)/sizeof(*logs)); i++) {
        if (logging_str2logid(logs[i]) >= 0)
            reportxml_helper_add_value_string(loglist_value_list, NULL, logs[i]);
    }

    reportxml_node_add_child(resource, loglist_value_list);
    refobject_unref(loglist_value_list);
    reportxml_node_add_child(incident, resource);
    refobject_unref(resource);


    resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(resource, "type", "result");
    reportxml_node_set_attribute(resource, "name", "logcontent");

    logfile = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(logfile, "type", "structure");
    reportxml_helper_add_value_int(logfile, "logid", logid);

    reportxml_helper_add_value_string(logfile, "logfile", logfilestring);

    parent = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(parent, "type", "list");
    reportxml_node_set_attribute(parent, "member", "lines");
    loglines = log_contents_array(logid);
    if (loglines) {
        for (i = 0; loglines[i]; i++) {
            reportxml_helper_add_value_string(parent, NULL, loglines[i]);
            free(loglines[i]);
        }
        free(loglines);
    }
    reportxml_node_add_child(logfile, parent);
    refobject_unref(parent);

    reportxml_node_add_child(resource, logfile);
    refobject_unref(logfile);

    reportxml_node_add_child(incident, resource);
    refobject_unref(resource);

    refobject_unref(incident);

    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, SHOWLOG_HTML_REQUEST, response, 200, NULL);
    refobject_unref(report);
}

static void command_mark_log            (client_t *client, source_t *source, admin_format_t response)
{
    logging_mark(client->username, client->role);

    admin_send_response_simple(client, source, response, "Logfiles marked", 1);
}

static void __reportxml_add_maintenance(reportxml_node_t *parent, reportxml_database_t *db, const char *state, const char *type, const char *text, const char *docs)
{
    reportxml_node_t *incident = reportxml_helper_add_incident(state, text, docs, db);
    reportxml_node_t *resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);

    reportxml_node_add_child(parent, incident);
    reportxml_node_add_child(incident, resource);

    reportxml_node_set_attribute(resource, "type", "result");
    reportxml_node_set_attribute(resource, "name", "maintenance");

    reportxml_helper_add_value_enum(resource, "type", type);

    refobject_unref(resource);
    refobject_unref(incident);
}

#if HAVE_GETRLIMIT && HAVE_SYS_RESOURCE_H
static health_t command_dashboard__getrlimit(ice_config_t *config, reportxml_node_t *parent, reportxml_database_t *db)
{
    health_t ret = HEALTH_OK;
    struct rlimit limit;

    if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
        if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur != RLIM_SAVED_MAX && limit.rlim_cur != RLIM_SAVED_CUR) {
            ICECAST_LOG_WARN("rlimit for NOFILE is %u", (unsigned int)limit.rlim_cur);
            if (limit.rlim_cur < (unsigned int)(config->client_limit + config->source_limit * 3 + 24)) {
                // We assume that we need one FD per client, at max three per source (e.g. for auth), and at max 24 additional for logfiles and similar.
                // This is just an estimation.
                __reportxml_add_maintenance(parent, db, "a93a842a-9664-43a9-b707-7f358066fe2b", "error", "Global client, and source limit is bigger than suitable for current open file limit.", NULL);
                ret = health_atbest(ret, HEALTH_ERROR);
            }
        }
    }

    if (getrlimit(RLIMIT_CORE, &limit) == 0) {
        if (limit.rlim_cur == 0) {
            __reportxml_add_maintenance(parent, db, "fdbacc56-1150-4c79-b53a-43cc79046f40", "info", "Core files are disabled.", NULL);
        } else {
            __reportxml_add_maintenance(parent, db, "688bdebc-c190-4b5d-8764-3a050c48387a", "info", "Core files are enabled.", NULL);
        }
    }

    return ret;
}
#endif

static void command_dashboard           (client_t *client, source_t *source, admin_format_t response)
{
    ice_config_t *config = config_get_config();
    reportxml_t *report = client_get_reportxml("0aa76ea1-bf42-49d1-887e-ca95fb307dc4", NULL, NULL);
    reportxml_node_t *reportnode = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_REPORT, 0);
    reportxml_node_t *incident = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);
    health_t health = HEALTH_OK;
    reportxml_node_t *resource;
    reportxml_node_t *node;
    bool has_sources;
    bool has_many_clients;
    bool has_too_many_clients;
    bool has_legacy_sources;
    bool inet6_enabled;


    resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(resource, "type", "result");
    reportxml_node_set_attribute(resource, "name", "overall-status");

    node = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(node, "type", "structure");
    reportxml_node_set_attribute(node, "member", "global-config");
    reportxml_helper_add_value_string(node, "hostname", config->hostname);
    reportxml_helper_add_value_int(node, "clients", config->client_limit);
    reportxml_helper_add_value_int(node, "sources", config->source_limit);
    reportxml_node_add_child(resource, node);
    refobject_unref(node);

    node = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(node, "type", "structure");
    reportxml_node_set_attribute(node, "member", "global-current");
    global_lock();
    reportxml_helper_add_value_int(node, "clients", global.clients);
    reportxml_helper_add_value_int(node, "sources", global.sources);
    has_sources = global.sources > 0;
    has_many_clients = global.clients > ((75 * config->client_limit) / 100);
    has_too_many_clients = global.clients > ((90 * config->client_limit) / 100);
    has_legacy_sources = global.sources_legacy > 0;
    inet6_enabled = listensocket_container_is_family_included(global.listensockets, SOCK_FAMILY_INET6);
    global_unlock();
    reportxml_node_add_child(resource, node);
    refobject_unref(node);

    if (config->config_problems || has_too_many_clients || !inet6_enabled) {
        health = health_atbest(health, HEALTH_ERROR);
    } else if (!has_sources || has_many_clients) {
        health = health_atbest(health, HEALTH_WARNING);
    }

#ifdef DEVEL_LOGGING
    health = health_atbest(health, HEALTH_WARNING);
    __reportxml_add_maintenance(reportnode, config->reportxml_db, "c704804e-d3b9-4544-898b-d477078135de", "warning", "Developer logging is active. This mode is not for production.", NULL);
#endif

    if (!inet6_enabled) {
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "f90219e1-bd07-4b54-b1ee-0ba6a0289a15", "error", "IPv6 not enabled.", NULL);
        if (sock_is_ipv4_mapped_supported()) {
            __reportxml_add_maintenance(reportnode, config->reportxml_db, "709ab43b-251d-49a5-a4fe-c749eaabf17c", "info", "IPv4-mapped IPv6 is available on this system.", NULL);
        }
    }

    if (config->config_problems & CONFIG_PROBLEM_HOSTNAME)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "c4f25c51-2720-4b38-a806-19ef024b5289", "warning", "Hostname is not set to anything useful in <hostname>.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_LOCATION)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "8defae31-a52e-4bba-b904-76db5362860f", "warning", "No useful location is given in <location>.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_ADMIN)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "cf86d88e-dc20-4359-b446-110e7065d17a", "warning", "No admin contact given in <admin>. YP directory support is disabled.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_PRNG)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "e2ba5a8b-4e4f-41ca-b455-68ae5fb6cae0", "error", "No PRNG seed configured. PRNG is insecure.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_UNKNOWN_NODE)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "6620ef7b-46ef-4781-9a5e-8ee7f0f9d44e", "error", "Unknown tags are used in the config file. See the error.log for details.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_OBSOLETE_NODE)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "b6224fc4-53a1-433f-a6cd-d5b85c60f1c9", "error", "Obsolete tags are used in the config file. See the error.log for details and update your configuration accordingly.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_INVALID_NODE)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "0f6f757d-52d8-4b9a-8e57-9bcd528fffba", "error", "Invalid tags are used in the config file. See the error.log for details and update your configuration accordingly.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_VALIDATION)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "8fc33086-274d-4ccb-b32f-599b3fa0f41a", "error", "The configuration did not validate. See the error.log for details and update your configuration accordingly.", NULL);

    if (!has_sources)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "f68dd8a3-22b1-4118-aba6-b039f2c5b51e", "info", "Currently no sources are connected to this server.", NULL);

    if (has_legacy_sources)
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "a3a51986-3bba-42b9-ad5c-d9ecc9967320", "warning", "Legacy sources are connected. See mount list for details.", NULL);

    if (has_too_many_clients) {
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "08676614-50b4-4ea7-ba99-7c2ffcecf705", "warning", "More than 90% of the server's configured maximum clients are connected", NULL);
    } else if (has_many_clients) {
        __reportxml_add_maintenance(reportnode, config->reportxml_db, "417ae59c-de19-4ed1-ade1-429c689f1152", "info", "More than 75% of the server's configured maximum clients are connected", NULL);
    }

#if HAVE_GETRLIMIT && HAVE_SYS_RESOURCE_H
    if (true) {
        health_t limits = command_dashboard__getrlimit(config, reportnode, config->reportxml_db);
        health = health_atbest(health, limits);
    }
#endif

    reportxml_helper_add_value_health(resource, "status", health);

    reportxml_node_add_child(incident, resource);
    refobject_unref(resource);

    refobject_unref(incident);
    refobject_unref(reportnode);

    config_release_config();
    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, DASHBOARD_HTML_REQUEST, response, 200, NULL);
    refobject_unref(report);
}

#ifdef HAVE_SPEEX
static inline const char *get_speex_version(void)
{
    const char *version;
    if (speex_lib_ctl(SPEEX_LIB_GET_VERSION_STRING, &version) != 0)
        return NULL;
    return version;
}
#endif

static inline const char *get_igloo_version(void)
{
    const char *version;
    if (igloo_version_get(&version, NULL, NULL, NULL) != igloo_ERROR_NONE)
        return NULL;
    return version;
}

static void command_version             (client_t *client, source_t *source, admin_format_t response)
{
    reportxml_t      *report        = client_get_reportxml("8cdfc150-094d-42f7-9c61-f9fb9a6e07e7", NULL, NULL);
    reportxml_node_t *incident      = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);
    reportxml_node_t *resource      = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_t *config        = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_t *dependencies  = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_t *flags         = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_t *cflags        = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_t *rflags        = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    ice_config_t *icecast_config;
#ifdef HAVE_CURL
    const curl_version_info_data * curl_runtime_version = curl_version_info(CURLVERSION_NOW);
#endif
    struct {
        const char *name;
        const char *compiletime;
        const char *runtime;
    } dependency_versions[] = {
        {"libigloo", NULL, get_igloo_version()},
        {"libxml2", LIBXML_DOTTED_VERSION, NULL},
#if defined(HAVE_OPENSSL) && defined(OPENSSL_VERSION_TEXT)
        {"OpenSSL", OPENSSL_VERSION_TEXT, NULL},
#endif
        {"libvorbis", NULL, vorbis_version_string()},
#ifdef HAVE_THEORA
        {"libtheora", NULL, theora_version_string()},
#endif
#ifdef HAVE_SPEEX
        {"libspeex", NULL, get_speex_version()},
#endif
#ifdef HAVE_CURL
        {"libcurl", LIBCURL_VERSION, curl_runtime_version->version},
#endif
        {NULL, NULL, NULL}
    };
    const char *compiletime_flags[] = {
#ifdef HAVE_POLL
        "poll",
#endif
#ifdef HAVE_SYS_SELECT_H
        "select",
#endif
#ifdef HAVE_UNAME
        "uname",
#endif
#ifdef HAVE_GETHOSTNAME
        "gethostname",
#endif
#ifdef HAVE_GETADDRINFO
        "getaddrinfo",
#endif
#ifdef WIN32
        "win32",
#endif
#ifdef DEVEL_LOGGING
        "developer-logging",
#endif
        NULL,
    };
    size_t i;

    reportxml_node_set_attribute(resource, "type", "result");
    reportxml_node_add_child(incident, resource);

    reportxml_helper_add_value_string(resource, "version", ICECAST_VERSION_STRING);
    reportxml_helper_add_value_int(resource, "address-bits", sizeof(void*)*8);

#ifdef HAVE_SYS_SELECT_H
    reportxml_helper_add_value_int(resource, "fd-set-size", FD_SETSIZE);
#endif

#ifdef HAVE_GETHOSTNAME
    if (true) {
        char hostname[80];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            reportxml_helper_add_value_string(resource, "gethostname", hostname);
        } else {
            reportxml_helper_add_value_string(resource, "gethostname", NULL);
        }
    }
#endif

#ifdef HAVE_UNAME
    if (true) {
        reportxml_node_t *res = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
        struct utsname utsname;

        reportxml_node_set_attribute(res, "type", "structure");
        reportxml_node_set_attribute(res, "member", "uname");
        reportxml_node_add_child(resource, res);

        if(uname(&utsname) == 0) {
            reportxml_helper_add_value_string(res, "sysname", utsname.sysname);
            reportxml_helper_add_value_string(res, "release", utsname.release);
            reportxml_helper_add_value_string(res, "nodename", utsname.nodename);
            reportxml_helper_add_value_string(res, "version", utsname.version);
            reportxml_helper_add_value_string(res, "machine", utsname.machine);
        } else {
            reportxml_node_set_attribute(res, "state", "unset");
        }

        refobject_unref(res);
    }
#endif

    reportxml_node_set_attribute(config, "type", "structure");
    reportxml_node_set_attribute(config, "member", "config");
    reportxml_node_add_child(resource, config);

    icecast_config = config_get_config();
    reportxml_helper_add_value_string(config, "hostname", icecast_config->hostname);
    reportxml_helper_add_value_string(config, "location", icecast_config->location);
    reportxml_helper_add_value_string(config, "admin", icecast_config->admin);
    reportxml_helper_add_value_string(config, "server-id", icecast_config->server_id);

    reportxml_helper_add_value_flag(rflags, "requested-chroot", icecast_config->chroot);
    reportxml_helper_add_value_flag(rflags, "requested-chuid", icecast_config->chuid);

    reportxml_helper_add_value_flag(rflags, "cfgp-hostname", icecast_config->config_problems & CONFIG_PROBLEM_HOSTNAME);
    reportxml_helper_add_value_flag(rflags, "cfgp-location", icecast_config->config_problems & CONFIG_PROBLEM_LOCATION);
    reportxml_helper_add_value_flag(rflags, "cfgp-admin", icecast_config->config_problems & CONFIG_PROBLEM_ADMIN);
    reportxml_helper_add_value_flag(rflags, "cfgp-prng", icecast_config->config_problems & CONFIG_PROBLEM_PRNG);
    reportxml_helper_add_value_flag(rflags, "cfgp-node-unknown", icecast_config->config_problems & CONFIG_PROBLEM_UNKNOWN_NODE);
    reportxml_helper_add_value_flag(rflags, "cfgp-node-obsolete", icecast_config->config_problems & CONFIG_PROBLEM_OBSOLETE_NODE);
    reportxml_helper_add_value_flag(rflags, "cfgp-node-invalid", icecast_config->config_problems & CONFIG_PROBLEM_INVALID_NODE);
    reportxml_helper_add_value_flag(rflags, "cfgp-validation", icecast_config->config_problems & CONFIG_PROBLEM_VALIDATION);
    config_release_config();

    reportxml_node_set_attribute(dependencies, "type", "structure");
    reportxml_node_set_attribute(dependencies, "member", "dependencies");
    reportxml_node_add_child(resource, dependencies);

    for (i = 0; dependency_versions[i].name; i++) {
        reportxml_node_t *dependency = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
        reportxml_node_set_attribute(dependency, "type", "structure");
        reportxml_node_set_attribute(dependency, "member", dependency_versions[i].name);
        reportxml_helper_add_value_string(dependency, "compiletime",  dependency_versions[i].compiletime);
        reportxml_helper_add_value_string(dependency, "runtime",  dependency_versions[i].runtime);
        reportxml_node_add_child(dependencies, dependency);
        refobject_unref(dependency);
    }

    reportxml_node_set_attribute(flags, "type", "structure");
    reportxml_node_set_attribute(flags, "member", "flags");
    reportxml_node_add_child(resource, flags);

    reportxml_node_set_attribute(cflags, "type", "unordered-list");
    reportxml_node_set_attribute(cflags, "member", "compiletime");
    reportxml_node_add_child(flags, cflags);
    for (i = 0; compiletime_flags[i]; i++) {
        reportxml_helper_add_value_flag(cflags, compiletime_flags[i], true);
    }

    reportxml_node_set_attribute(rflags, "type", "unordered-list");
    reportxml_node_set_attribute(rflags, "member", "runtime");
    reportxml_node_add_child(flags, rflags);
    reportxml_helper_add_value_flag(rflags, "ipv4-mapped", sock_is_ipv4_mapped_supported());

    global_lock();
    reportxml_helper_add_value_flag(rflags, "bound-unix", listensocket_container_is_family_included(global.listensockets, SOCK_FAMILY_UNIX));
    reportxml_helper_add_value_flag(rflags, "bound-inet4", listensocket_container_is_family_included(global.listensockets, SOCK_FAMILY_INET4));
    reportxml_helper_add_value_flag(rflags, "bound-inet6", listensocket_container_is_family_included(global.listensockets, SOCK_FAMILY_INET6));
    global_unlock();

    refobject_unref(config);
    refobject_unref(dependencies);
    refobject_unref(cflags);
    refobject_unref(rflags);
    refobject_unref(flags);
    refobject_unref(resource);
    refobject_unref(incident);
    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, VERSION_HTML_REQUEST, response, 200, NULL);
    refobject_unref(report);
}

static void ui_command(client_t * client, source_t * source, admin_format_t format, resourcematch_extract_t *parameters)
{
    int is_valid = 0;

    if (parameters->groups == 1) {
        const char *fn = parameters->group[0].result.string;

        if (strlen(fn) < 32) {
            for (; *fn; fn++) {
                if (*fn == '.' && strcmp(fn, ".xsl") == 0) {
                    is_valid = 1;
                    break;
                }
                if (*fn < 'a' || *fn > 'z')
                    break;
            }
        }
    }

    if (is_valid) {
        reportxml_t *report = client_get_reportxml("ddb7da7a-7273-4dfb-8090-867cff82bfd2", NULL, NULL);
        reportxml_node_t *incident = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);
        reportxml_node_t *resource;
        reportxml_node_t *param;
        char **params;
        char buffer[80];
        size_t i;

        snprintf(buffer, sizeof(buffer), "ui/%s", parameters->group[0].result.string);

        resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
        reportxml_node_set_attribute(resource, "type", "parameter");
        reportxml_node_add_child(incident, resource);

        param = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
        reportxml_node_set_attribute(param, "type", "structure");
        reportxml_node_set_attribute(param, "member", "get-parameters");
        reportxml_node_add_child(resource, param);

        params = httpp_get_any_key(client->parser, HTTPP_NS_QUERY_STRING);
        if (params) {
            for (i = 0; params[i]; i++) {
                const char *value = httpp_get_query_param(client->parser, params[i]);
                if (value) {
                    reportxml_helper_add_value_string(param, params[i], value);
                }
            }
            httpp_free_any_key(params);
        }

        refobject_unref(param);
        refobject_unref(resource);
        refobject_unref(incident);

        client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, buffer, format, 200, NULL);
        refobject_unref(report);
    } else {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND);
    }
}
