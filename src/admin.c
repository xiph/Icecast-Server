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
 * Copyright 2012-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "admin.h"
#include "compat.h"
#include "cfgfile.h"
#include "connection.h"
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
#define STATS_RAW_REQUEST                   "stats"
#define STATS_HTML_REQUEST                  "stats.xsl"
#define QUEUE_RELOAD_RAW_REQUEST            "reloadconfig"
#define QUEUE_RELOAD_HTML_REQUEST           "reloadconfig.xsl"
#define QUEUE_RELOAD_JSON_REQUEST           "reloadconfig.json"
#define LISTMOUNTS_RAW_REQUEST              "listmounts"
#define LISTMOUNTS_HTML_REQUEST             "listmounts.xsl"
#define STREAMLIST_RAW_REQUEST              "streamlist"
#define STREAMLIST_HTML_REQUEST             "streamlist.xsl"
#define STREAMLIST_PLAINTEXT_REQUEST        "streamlist.txt"
#define MOVECLIENTS_RAW_REQUEST             "moveclients"
#define MOVECLIENTS_HTML_REQUEST            "moveclients.xsl"
#define KILLCLIENT_RAW_REQUEST              "killclient"
#define KILLCLIENT_HTML_REQUEST             "killclient.xsl"
#define KILLCLIENT_JSON_REQUEST             "killclient.json"
#define KILLSOURCE_RAW_REQUEST              "killsource"
#define KILLSOURCE_HTML_REQUEST             "killsource.xsl"
#define KILLSOURCE_JSON_REQUEST             "killsource.json"
#define ADMIN_XSL_RESPONSE                  "response.xsl"
#define MANAGEAUTH_RAW_REQUEST              "manageauth"
#define MANAGEAUTH_HTML_REQUEST             "manageauth.xsl"
#define UPDATEMETADATA_RAW_REQUEST          "updatemetadata"
#define UPDATEMETADATA_HTML_REQUEST         "updatemetadata.xsl"
#define SHOWLOG_RAW_REQUEST                 "showlog"
#define SHOWLOG_HTML_REQUEST                "showlog.xsl"
#define MARKLOG_RAW_REQUEST                 "marklog"
#define MARKLOG_HTML_REQUEST                "marklog.xsl"
#define MARKLOG_JSON_REQUEST                "marklog.json"
#define DASHBOARD_RAW_REQUEST               "dashboard"
#define DASHBOARD_HTML_REQUEST              "dashboard.xsl"
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
static void command_queue_reload        (client_t *client, source_t *source, admin_format_t response);
static void command_list_mounts         (client_t *client, source_t *source, admin_format_t response);
static void command_move_clients        (client_t *client, source_t *source, admin_format_t response);
static void command_kill_client         (client_t *client, source_t *source, admin_format_t response);
static void command_kill_source         (client_t *client, source_t *source, admin_format_t response);
static void command_manageauth          (client_t *client, source_t *source, admin_format_t response);
static void command_updatemetadata      (client_t *client, source_t *source, admin_format_t response);
static void command_buildm3u            (client_t *client, source_t *source, admin_format_t response);
static void command_show_log            (client_t *client, source_t *source, admin_format_t response);
static void command_mark_log            (client_t *client, source_t *source, admin_format_t response);
static void command_dashboard           (client_t *client, source_t *source, admin_format_t response);

static const admin_command_handler_t handlers[] = {
    { "*",                                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          NULL, NULL}, /* for ACL framework */
    { FALLBACK_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_fallback, NULL},
    { FALLBACK_HTML_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_fallback, NULL},
    { FALLBACK_JSON_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          command_fallback, NULL},
    { METADATA_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_metadata, NULL},
    { METADATA_HTML_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_metadata, NULL},
    { METADATA_JSON_REQUEST,                ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          command_metadata, NULL},
    { SHOUTCAST_METADATA_REQUEST,           ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_shoutcast_metadata, NULL},
    { LISTCLIENTS_RAW_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_show_listeners, NULL},
    { LISTCLIENTS_HTML_REQUEST,             ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_show_listeners, NULL},
    { STATS_RAW_REQUEST,                    ADMINTYPE_HYBRID,       ADMIN_FORMAT_RAW,           command_stats, NULL},
    { STATS_HTML_REQUEST,                   ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          command_stats, NULL},
    { "stats.xml",                          ADMINTYPE_HYBRID,       ADMIN_FORMAT_RAW,           command_stats, NULL},
    { QUEUE_RELOAD_RAW_REQUEST,             ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_queue_reload, NULL},
    { QUEUE_RELOAD_HTML_REQUEST,            ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_queue_reload, NULL},
    { QUEUE_RELOAD_JSON_REQUEST,            ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          command_queue_reload, NULL},
    { LISTMOUNTS_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_list_mounts, NULL},
    { LISTMOUNTS_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_list_mounts, NULL},
    { STREAMLIST_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_list_mounts, NULL},
    { STREAMLIST_PLAINTEXT_REQUEST,         ADMINTYPE_GENERAL,      ADMIN_FORMAT_PLAINTEXT,     command_list_mounts, NULL},
    { STREAMLIST_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_list_mounts, NULL},
    { MOVECLIENTS_RAW_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_move_clients, NULL},
    { MOVECLIENTS_HTML_REQUEST,             ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          command_move_clients, NULL},
    { KILLCLIENT_RAW_REQUEST,               ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_kill_client, NULL},
    { KILLCLIENT_HTML_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_kill_client, NULL},
    { KILLCLIENT_JSON_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          command_kill_client, NULL},
    { KILLSOURCE_RAW_REQUEST,               ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_kill_source, NULL},
    { KILLSOURCE_HTML_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_kill_source, NULL},
    { KILLSOURCE_JSON_REQUEST,              ADMINTYPE_MOUNT,        ADMIN_FORMAT_JSON,          command_kill_source, NULL},
    { MANAGEAUTH_RAW_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_manageauth, NULL},
    { MANAGEAUTH_HTML_REQUEST,              ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_manageauth, NULL},
    { UPDATEMETADATA_RAW_REQUEST,           ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_updatemetadata, NULL},
    { UPDATEMETADATA_HTML_REQUEST,          ADMINTYPE_MOUNT,        ADMIN_FORMAT_HTML,          command_updatemetadata, NULL},
    { BUILDM3U_RAW_REQUEST,                 ADMINTYPE_MOUNT,        ADMIN_FORMAT_RAW,           command_buildm3u, NULL},
    { SHOWLOG_RAW_REQUEST,                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_show_log, NULL},
    { SHOWLOG_HTML_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_show_log, NULL},
    { MARKLOG_RAW_REQUEST,                  ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_mark_log, NULL},
    { MARKLOG_HTML_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_mark_log, NULL},
    { MARKLOG_JSON_REQUEST,                 ADMINTYPE_GENERAL,      ADMIN_FORMAT_JSON,          command_mark_log, NULL},
    { DASHBOARD_RAW_REQUEST,                ADMINTYPE_GENERAL,      ADMIN_FORMAT_RAW,           command_dashboard, NULL},
    { DASHBOARD_HTML_REQUEST,               ADMINTYPE_GENERAL,      ADMIN_FORMAT_HTML,          command_dashboard, NULL},
    { DEFAULT_HTML_REQUEST,                 ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          command_default_selector, NULL},
    { DEFAULT_RAW_REQUEST,                  ADMINTYPE_HYBRID,       ADMIN_FORMAT_HTML,          command_default_selector, NULL}
};

static void ui_command(client_t * client, source_t * source, admin_format_t format, resourcematch_extract_t *parameters);

static const admin_command_handler_t ui_handlers[] = {
    { "%s",                                 ADMINTYPE_HYBRID,       ADMIN_FORMAT_AUTO,          NULL, ui_command}
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

/* build an XML root node including some common tags */
xmlNodePtr admin_build_rootnode(xmlDocPtr doc, const char *name)
{
    xmlNodePtr rootnode = xmlNewDocNode(doc, NULL, XMLSTR(name), NULL);
    xmlNodePtr modules = module_container_get_modulelist_as_xml(global.modulecontainer);

    xmlAddChild(rootnode, modules);

    return rootnode;
}

/* build an XML doc containing information about currently running sources.
 * If a mountpoint is passed then that source will not be added to the XML
 * doc even if the source is running */
xmlDocPtr admin_build_sourcelist(const char *mount)
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

            xmlNewTextChild(srcnode, NULL, XMLSTR("fallback"),
                    (source->fallback_mount != NULL)?
                    XMLSTR(source->fallback_mount):XMLSTR(""));
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
                if (source->client) {
                    snprintf(buf, sizeof(buf), "%lu",
                        (unsigned long)(now - source->con->con_time));
                    xmlNewTextChild(srcnode, NULL, XMLSTR("Connected"), XMLSTR(buf));
                }
                xmlNewTextChild(srcnode, NULL, XMLSTR("content-type"),
                    XMLSTR(source->format->contenttype));
            }
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
        xmlChar *buff = NULL;
        int len = 0;
        size_t buf_len;
        ssize_t ret;
        const char *content_type;

        if (response == ADMIN_FORMAT_RAW) {
            xmlDocDumpMemory(doc, &buff, &len);
            content_type = "text/xml";
        } else {
            xmlNodePtr xmlroot = xmlDocGetRootElement(doc);
            const char *ns;
            char *json;

            if (strcmp((const char *)xmlroot->name, "iceresponse") == 0) {
                ns = "http://icecast.org/specs/legacyresponse-0.0.1";
            } else {
                ns = "http://icecast.org/specs/legacystats-0.0.1";
            }

            json = xml2json_render_doc_simple(doc, ns);
            buff = xmlStrdup(XMLSTR(json));
            len = strlen(json);
            free(json);
            content_type = "application/json";
        }


        buf_len = len + 1024;
        if (buf_len < 4096)
            buf_len = 4096;

        client_set_queue(client, NULL);
        client->refbuf = refbuf_new(buf_len);

        ret = util_http_build_header(client->refbuf->data, buf_len, 0,
                                     0, 200, NULL,
                                     content_type, "utf-8",
                                     NULL, NULL, client);
        if (ret < 0) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
            xmlFree(buff);
            return;
        } else if (buf_len < (size_t)(len + ret + 64)) {
            void *new_data;
            buf_len = ret + len + 64;
            new_data = realloc(client->refbuf->data, buf_len);
            if (new_data) {
                ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
                client->refbuf->data = new_data;
                client->refbuf->len = buf_len;
                ret = util_http_build_header(client->refbuf->data, buf_len, 0,
                                             0, 200, NULL,
                                             content_type, "utf-8",
                                             NULL, NULL, client);
                if (ret == -1) {
                    ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                    client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
                    xmlFree(buff);
                    return;
                }
            } else {
                ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
                client_send_error_by_id(client, ICECAST_ERROR_GEN_BUFFER_REALLOC);
                xmlFree(buff);
                return;
            }
        }

        /* FIXME: in this section we hope no function will ever return -1 */
        ret += snprintf (client->refbuf->data + ret, buf_len - ret, "Content-Length: %d\r\n\r\n%s", xmlStrlen(buff), buff);

        client->refbuf->len = ret;
        xmlFree(buff);
        client->respcode = 200;
        fserve_add_client (client, NULL);
    }
    if (response == ADMIN_FORMAT_HTML) {
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
            client_send_error_by_id(client, ICECAST_ERROR_GEN_CLIENT_NEEDS_TO_AUTHENTICATE);
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
        ssize_t ret;

        ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
                0, 0, 200, NULL,
                "text/html", "utf-8",
                "", NULL, client);

        if (ret == -1 || ret >= PER_CLIENT_REFBUF_SIZE) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
            return;
        }

        snprintf(client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret,
                "<html><head><title>Admin request successful</title></head>"
                "<body><p>%s</p></body></html>", message);

        client->respcode = 200;
        client->refbuf->len = strlen(client->refbuf->data);
        fserve_add_client(client, NULL);
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
    ICECAST_LOG_DEBUG("Done optional check (%d)", parameters_passed);
    if (!parameters_passed) {
        xmlDocPtr doc = admin_build_sourcelist(source->mount);

        if (idtext) {
            xmlNodePtr root = xmlDocGetRootElement(doc);
            xmlNewTextChild(root, NULL, XMLSTR("param-id"), XMLSTR(idtext));
        }
        admin_send_response(doc, client, response,
             MOVECLIENTS_HTML_REQUEST);
        xmlFreeDoc(doc);
        return;
    }

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

    source_move_clients(source, dest, idtext ? &id : NULL);

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

    xmlNewTextChild(node, NULL, XMLSTR("tls"), XMLSTR(client->con->tls ? "true" : "false"));

    xmlNewTextChild(node, NULL, XMLSTR("protocol"), XMLSTR(client_protocol_to_string(client->protocol)));

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
    ssize_t ret;

    COMMAND_REQUIRE(client, "username", username);
    COMMAND_REQUIRE(client, "password", password);

    ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
                                 0, 0, 200, NULL,
                                 "audio/x-mpegurl", NULL,
                                 NULL, NULL, client);

    if (ret == -1 || ret >= (PER_CLIENT_REFBUF_SIZE - 512)) {
        /* we want at least 512 Byte left for data */
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_error_by_id(client, ICECAST_ERROR_GEN_HEADER_GEN_FAILED);
        return;
    }


    client_get_baseurl(client, NULL, client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret, username, password, "Content-Disposition: attachment; filename=listen.m3u\r\n\r\n", mount, "\r\n");

    client->respcode = 200;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
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
    xmlNodePtr node, rolenode, usersnode, msgnode;
    const char *action = NULL;
    const char *username = NULL;
    const char *idstring = NULL;
    char *message = NULL;
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
            ICECAST_LOG_WARN("Client requested mangement for unknown role %lu", id);
            error_id = ICECAST_ERROR_ADMIN_ROLEMGN_ROLE_NOT_FOUND;
            break;
        }

        COMMAND_OPTIONAL(client, "action", action);
        COMMAND_OPTIONAL(client, "username", username);

        if (action == NULL)
            action = "list";

        if (!strcmp(action, "add")) {
            const char *password = NULL;
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
            if (ret == AUTH_FAILED) {
                message = strdup("User add failed - check the icecast error log");
            } else if (ret == AUTH_USERADDED) {
                message = strdup("User added");
            } else if (ret == AUTH_USEREXISTS) {
                message = strdup("User already exists - not added");
            }
        }
        if (!strcmp(action, "delete")) {
            if (username == NULL) {
                ICECAST_LOG_WARN("manage auth request delete for %lu but no username", id);
                break;
            }

            if (!auth->deleteuser) {
                error_id = ICECAST_ERROR_ADMIN_ROLEMGN_DELETE_NOSYS;
                break;
            }

            ret = auth->deleteuser(auth, username);
            if (ret == AUTH_FAILED) {
                message = strdup("User delete failed - check the icecast error log");
            } else if (ret == AUTH_USERDELETED) {
                message = strdup("User deleted");
            }
        }

        doc = xmlNewDoc(XMLSTR("1.0"));
        node = admin_build_rootnode(doc, "icestats");

        rolenode = admin_add_role_to_authentication(auth, node);

        if (message) {
            msgnode = admin_build_rootnode(doc, "iceresponse");
            xmlNewTextChild(msgnode, NULL, XMLSTR("message"), XMLSTR(message));
        }

        xmlDocSetRootElement(doc, node);

        if (auth->listuser) {
            usersnode = xmlNewChild(rolenode, NULL, XMLSTR("users"), NULL);
            auth->listuser(auth, usersnode);
        }

        config_release_config();
        auth_release(auth);

        admin_send_response(doc, client, response,
            MANAGEAUTH_HTML_REQUEST);
        free(message);
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
            xmlDocPtr doc = admin_build_sourcelist(source->mount);
            admin_send_response(doc, client, response, FALLBACK_HTML_REQUEST);
            xmlFreeDoc(doc);
            return;
        }
    }

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
        doc = admin_build_sourcelist(NULL);
        avl_tree_unlock(global.source_tree);

        admin_send_response(doc, client, response,
            LISTMOUNTS_HTML_REQUEST);
        xmlFreeDoc(doc);
    }
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

    if (!logfilestring || !strcmp(logfilestring, "error")) {
        logfilestring = "error";
        logid = errorlog;
    } else if (!strcmp(logfilestring, "access")) {
        logid = accesslog;
    } else if (!strcmp(logfilestring, "playlist")) {
        logid = playlistlog;
    } else {
        logfilestring = "error";
        logid = errorlog;
    }

    report = client_get_reportxml("b20a2bf2-1278-448c-81f3-58183d837a86", NULL, NULL);

    incident = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);


    resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(resource, "type", "related");
    reportxml_node_set_attribute(resource, "name", "logfiles");

    loglist_value_list = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(loglist_value_list, "type", "list");

    for (i = 0; i < (sizeof(logs)/sizeof(*logs)); i++) {
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

    reportxml_helper_add_value_string(logfile, "logfile", logfilestring);

    parent = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(parent, "type", "list");
    reportxml_node_set_attribute(parent, "member", "lines");
    loglines = log_contents_array(logid);
    for (i = 0; loglines[i]; i++) {
        reportxml_helper_add_value_string(parent, NULL, loglines[i]);
        free(loglines[i]);
    }
    free(loglines);
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

static void __reportxml_add_maintenance(reportxml_node_t *parent, const char *type, const char *text, const char *docs)
{
    reportxml_node_t *maintenance = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);

    reportxml_node_set_attribute(maintenance, "type", "structure");
    reportxml_node_add_child(parent, maintenance);

    reportxml_helper_add_value_enum(maintenance, "type", type);

    if (text)
        reportxml_helper_add_text(maintenance, NULL, text);

    if (docs)
        reportxml_helper_add_reference(maintenance, "documentation", docs);

    refobject_unref(maintenance);
}

static void command_dashboard           (client_t *client, source_t *source, admin_format_t response)
{
    ice_config_t *config = config_get_config();
    reportxml_t *report = client_get_reportxml("0aa76ea1-bf42-49d1-887e-ca95fb307dc4", NULL, NULL);
    reportxml_node_t *incident = reportxml_get_node_by_type(report, REPORTXML_NODE_TYPE_INCIDENT, 0);
    reportxml_node_t *resource;
    reportxml_node_t *node;
    int has_sources;
    int has_many_clients;
    int has_too_many_clients;


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
    global_unlock();
    reportxml_node_add_child(resource, node);
    refobject_unref(node);

    if (config->config_problems || has_too_many_clients) {
        reportxml_helper_add_value_enum(resource, "status", "red");
    } else if (!has_sources || has_many_clients) {
        reportxml_helper_add_value_enum(resource, "status", "yellow");
    } else {
        reportxml_helper_add_value_enum(resource, "status", "green");
    }

    reportxml_node_add_child(incident, resource);
    refobject_unref(resource);


    resource = reportxml_node_new(REPORTXML_NODE_TYPE_RESOURCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(resource, "type", "result");
    reportxml_node_set_attribute(resource, "name", "maintenance");

    if (config->config_problems & CONFIG_PROBLEM_HOSTNAME)
        __reportxml_add_maintenance(resource, "warning", "Hostname is not set to anything useful in <hostname>.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_LOCATION)
        __reportxml_add_maintenance(resource, "warning", "No useful location is given in <location>.", NULL);
    if (config->config_problems & CONFIG_PROBLEM_ADMIN)
        __reportxml_add_maintenance(resource, "warning", "No admin contact given in <admin>. YP directory support will is disabled.", NULL);

    if (!has_sources)
        __reportxml_add_maintenance(resource, "info", "Currently no sources are connected to this server.", NULL);

    if (has_too_many_clients) {
        __reportxml_add_maintenance(resource, "warning", "More than 90% of the server's configured maximum clients are connected", NULL);
    } else if (has_many_clients) {
        __reportxml_add_maintenance(resource, "info", "More than 75% of the server's configured maximum clients are connected", NULL);
    }

    reportxml_node_add_child(incident, resource);
    refobject_unref(resource);


    refobject_unref(incident);

    config_release_config();
    client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, DASHBOARD_HTML_REQUEST, response, 200, NULL);
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

        client_send_reportxml(client, report, DOCUMENT_DOMAIN_ADMIN, buffer, format, 200, NULL);
        refobject_unref(report);
    } else {
        client_send_error_by_id(client, ICECAST_ERROR_ADMIN_UNRECOGNISED_COMMAND);
    }
}
