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
 * Copyright 2012-2014, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#include "cfgfile.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "source.h"
#include "global.h"
#include "stats.h"
#include "compat.h"
#include "xslt.h"
#include "fserve.h"
#include "admin.h"

#include "format.h"

#include "logging.h"
#include "auth.h"
#ifdef _WIN32
#define snprintf _snprintf
#endif

#define CATMODULE "admin"

/* special commands */
#define COMMAND_ERROR                      ADMIN_COMMAND_ERROR
#define COMMAND_ANY                        ADMIN_COMMAND_ANY

/* Mount-specific commands */
#define COMMAND_RAW_FALLBACK               1
#define COMMAND_RAW_METADATA_UPDATE        2
#define COMMAND_RAW_SHOW_LISTENERS         3
#define COMMAND_RAW_MOVE_CLIENTS           4
#define COMMAND_RAW_MANAGEAUTH             5
#define COMMAND_SHOUTCAST_METADATA_UPDATE  6
#define COMMAND_RAW_UPDATEMETADATA         7

#define COMMAND_TRANSFORMED_FALLBACK        50
#define COMMAND_TRANSFORMED_SHOW_LISTENERS  53
#define COMMAND_TRANSFORMED_MOVE_CLIENTS    54
#define COMMAND_TRANSFORMED_MANAGEAUTH      55
#define COMMAND_TRANSFORMED_UPDATEMETADATA  56
#define COMMAND_TRANSFORMED_METADATA_UPDATE 57

/* Global commands */
#define COMMAND_RAW_LIST_MOUNTS             101
#define COMMAND_RAW_STATS                   102
#define COMMAND_RAW_LISTSTREAM              103
#define COMMAND_PLAINTEXT_LISTSTREAM        104
#define COMMAND_RAW_QUEUE_RELOAD            105
#define COMMAND_TRANSFORMED_LIST_MOUNTS     201
#define COMMAND_TRANSFORMED_STATS           202
#define COMMAND_TRANSFORMED_LISTSTREAM      203
#define COMMAND_TRANSFORMED_QUEUE_RELOAD    205

/* Client management commands */
#define COMMAND_RAW_KILL_CLIENT             301
#define COMMAND_RAW_KILL_SOURCE             302
#define COMMAND_TRANSFORMED_KILL_CLIENT     401
#define COMMAND_TRANSFORMED_KILL_SOURCE     402

/* Admin commands requiring no auth */
#define COMMAND_BUILDM3U                    501

#define FALLBACK_RAW_REQUEST "fallbacks"
#define FALLBACK_TRANSFORMED_REQUEST "fallbacks.xsl"
#define SHOUTCAST_METADATA_REQUEST "admin.cgi"
#define METADATA_RAW_REQUEST "metadata"
#define METADATA_TRANSFORMED_REQUEST "metadata.xsl"
#define LISTCLIENTS_RAW_REQUEST "listclients"
#define LISTCLIENTS_TRANSFORMED_REQUEST "listclients.xsl"
#define STATS_RAW_REQUEST "stats"
#define STATS_TRANSFORMED_REQUEST "stats.xsl"
#define QUEUE_RELOAD_RAW_REQUEST "reloadconfig"
#define QUEUE_RELOAD_TRANSFORMED_REQUEST "reloadconfig.xsl"
#define LISTMOUNTS_RAW_REQUEST "listmounts"
#define LISTMOUNTS_TRANSFORMED_REQUEST "listmounts.xsl"
#define STREAMLIST_RAW_REQUEST "streamlist"
#define STREAMLIST_TRANSFORMED_REQUEST "streamlist.xsl"
#define STREAMLIST_PLAINTEXT_REQUEST "streamlist.txt"
#define MOVECLIENTS_RAW_REQUEST "moveclients"
#define MOVECLIENTS_TRANSFORMED_REQUEST "moveclients.xsl"
#define KILLCLIENT_RAW_REQUEST "killclient"
#define KILLCLIENT_TRANSFORMED_REQUEST "killclient.xsl"
#define KILLSOURCE_RAW_REQUEST "killsource"
#define KILLSOURCE_TRANSFORMED_REQUEST "killsource.xsl"
#define ADMIN_XSL_RESPONSE "response.xsl"
#define MANAGEAUTH_RAW_REQUEST "manageauth"
#define MANAGEAUTH_TRANSFORMED_REQUEST "manageauth.xsl"
#define UPDATEMETADATA_RAW_REQUEST "updatemetadata"
#define UPDATEMETADATA_TRANSFORMED_REQUEST "updatemetadata.xsl"
#define DEFAULT_RAW_REQUEST ""
#define DEFAULT_TRANSFORMED_REQUEST ""
#define BUILDM3U_RAW_REQUEST "buildm3u"

typedef struct admin_command_tag {
    const int   id;
    const char *name;
    const int   type;
    const int   format;
} admin_command_t;

/*
COMMAND_TRANSFORMED_METADATA_UPDATE -> METADATA_TRANSFORMED_REQUEST
COMMAND_TRANSFORMED_UPDATEMETADATA  -> UPDATEMETADATA_TRANSFORMED_REQUEST
*/

static const admin_command_t commands[] = {
 {COMMAND_RAW_FALLBACK,                FALLBACK_RAW_REQUEST,               ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_FALLBACK,        FALLBACK_TRANSFORMED_REQUEST,       ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_RAW_METADATA_UPDATE,         METADATA_RAW_REQUEST,               ADMINTYPE_MOUNT,   RAW},
 {COMMAND_SHOUTCAST_METADATA_UPDATE,   SHOUTCAST_METADATA_REQUEST,         ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_TRANSFORMED_METADATA_UPDATE, METADATA_TRANSFORMED_REQUEST,       ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_RAW_SHOW_LISTENERS,          LISTCLIENTS_RAW_REQUEST,            ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_SHOW_LISTENERS,  LISTCLIENTS_TRANSFORMED_REQUEST,    ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_RAW_STATS,                   STATS_RAW_REQUEST,                  ADMINTYPE_HYBRID,  RAW},
 {COMMAND_TRANSFORMED_STATS,           STATS_TRANSFORMED_REQUEST,          ADMINTYPE_HYBRID,  TRANSFORMED},
 {COMMAND_RAW_STATS,                   "stats.xml",                        ADMINTYPE_HYBRID,  RAW}, /* The old way */
 {COMMAND_RAW_QUEUE_RELOAD,            QUEUE_RELOAD_RAW_REQUEST,           ADMINTYPE_GENERAL, RAW},
 {COMMAND_TRANSFORMED_QUEUE_RELOAD,    QUEUE_RELOAD_TRANSFORMED_REQUEST,   ADMINTYPE_GENERAL, TRANSFORMED},
 {COMMAND_RAW_LIST_MOUNTS,             LISTMOUNTS_RAW_REQUEST,             ADMINTYPE_GENERAL, RAW},
 {COMMAND_TRANSFORMED_LIST_MOUNTS,     LISTMOUNTS_TRANSFORMED_REQUEST,     ADMINTYPE_GENERAL, TRANSFORMED},
 {COMMAND_RAW_LISTSTREAM,              STREAMLIST_RAW_REQUEST,             ADMINTYPE_GENERAL, RAW},
 {COMMAND_PLAINTEXT_LISTSTREAM,        STREAMLIST_PLAINTEXT_REQUEST,       ADMINTYPE_GENERAL, PLAINTEXT},
 {COMMAND_TRANSFORMED_LISTSTREAM,      STREAMLIST_TRANSFORMED_REQUEST,     ADMINTYPE_GENERAL, TRANSFORMED},
 {COMMAND_RAW_MOVE_CLIENTS,            MOVECLIENTS_RAW_REQUEST,            ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_MOVE_CLIENTS,    MOVECLIENTS_TRANSFORMED_REQUEST,    ADMINTYPE_HYBRID,  TRANSFORMED},
 {COMMAND_RAW_KILL_CLIENT,             KILLCLIENT_RAW_REQUEST,             ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_KILL_CLIENT,     KILLCLIENT_TRANSFORMED_REQUEST,     ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_RAW_KILL_SOURCE,             KILLSOURCE_RAW_REQUEST,             ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_KILL_SOURCE,     KILLSOURCE_TRANSFORMED_REQUEST,     ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_RAW_MANAGEAUTH,              MANAGEAUTH_RAW_REQUEST,             ADMINTYPE_GENERAL, RAW},
 {COMMAND_TRANSFORMED_MANAGEAUTH,      MANAGEAUTH_TRANSFORMED_REQUEST,     ADMINTYPE_GENERAL, TRANSFORMED},
 {COMMAND_RAW_UPDATEMETADATA,          UPDATEMETADATA_RAW_REQUEST,         ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_UPDATEMETADATA,  UPDATEMETADATA_TRANSFORMED_REQUEST, ADMINTYPE_MOUNT,   TRANSFORMED},
 {COMMAND_BUILDM3U,                    BUILDM3U_RAW_REQUEST,               ADMINTYPE_MOUNT,   RAW},
 {COMMAND_TRANSFORMED_STATS,           DEFAULT_TRANSFORMED_REQUEST,        ADMINTYPE_HYBRID,  TRANSFORMED},
 {COMMAND_TRANSFORMED_STATS,           DEFAULT_RAW_REQUEST,                ADMINTYPE_HYBRID,  TRANSFORMED},
 {COMMAND_ANY,                         "*",                                ADMINTYPE_GENERAL, TRANSFORMED} /* for ACL framework */
};

int admin_get_command(const char *command) {
    size_t i;

    for (i = 0; i < (sizeof(commands)/sizeof(*commands)); i++)
        if (strcmp(commands[i].name, command) == 0)
            return commands[i].id;

    return COMMAND_ERROR;
}

int admin_get_command_type(int command) {
    size_t i;

    if (command == ADMIN_COMMAND_ERROR || command == COMMAND_ANY)
        return ADMINTYPE_ERROR;

    for (i = 0; i < (sizeof(commands)/sizeof(*commands)); i++)
        if (commands[i].id == command)
            return commands[i].type;

    return ADMINTYPE_ERROR;
}

static void command_fallback(client_t *client, source_t *source, int response);
static void command_metadata(client_t *client, source_t *source, int response);
static void command_shoutcast_metadata(client_t *client, source_t *source);
static void command_show_listeners(client_t *client, source_t *source,
        int response);
static void command_move_clients(client_t *client, source_t *source,
        int response);
static void command_stats(client_t *client, const char *mount, int response);
static void command_queue_reload(client_t *client, int response);
static void command_list_mounts(client_t *client, int response);
static void command_kill_client(client_t *client, source_t *source,
        int response);
static void command_manageauth(client_t *client, int response);
static void command_buildm3u(client_t *client, const char *mount);
static void command_kill_source(client_t *client, source_t *source,
        int response);
static void command_updatemetadata(client_t *client, source_t *source,
        int response);
static void admin_handle_mount_request(client_t *client, source_t *source);
static void admin_handle_general_request(client_t *client);

/* build an XML doc containing information about currently running sources.
 * If a mountpoint is passed then that source will not be added to the XML
 * doc even if the source is running */
xmlDocPtr admin_build_sourcelist (const char *mount)
{
    avl_node *node;
    source_t *source;
    xmlNodePtr xmlnode, srcnode;
    xmlDocPtr doc;
    char buf[22];
    time_t now = time(NULL);

    doc = xmlNewDoc (XMLSTR("1.0"));
    xmlnode = xmlNewDocNode (doc, NULL, XMLSTR("icestats"), NULL);
    xmlDocSetRootElement(doc, xmlnode);

    if (mount) {
        xmlNewChild (xmlnode, NULL, XMLSTR("current_source"), XMLSTR(mount));
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

            xmlNewChild(srcnode, NULL, XMLSTR("fallback"),
                    (source->fallback_mount != NULL)?
                    XMLSTR(source->fallback_mount):XMLSTR(""));
            snprintf(buf, sizeof(buf), "%lu", source->listeners);
            xmlNewChild(srcnode, NULL, XMLSTR("listeners"), XMLSTR(buf));

            config = config_get_config();
            mountinfo = config_find_mount (config, source->mount, MOUNT_TYPE_NORMAL);
            if (mountinfo)
                acl = auth_stack_get_anonymous_acl(mountinfo->authstack, httpp_req_get);
            if (!acl)
                acl = auth_stack_get_anonymous_acl(config->authstack, httpp_req_get);
            if (acl && acl_test_web(acl) == ACL_POLICY_DENY) {
                xmlNewChild(srcnode, NULL, XMLSTR("authenticator"), XMLSTR("(dummy)"));
            }
            acl_release(acl);
            config_release_config();

            if (source->running)
            {
                if (source->client) 
                {
                    snprintf (buf, sizeof(buf), "%lu",
                            (unsigned long)(now - source->con->con_time));
                    xmlNewChild (srcnode, NULL, XMLSTR("Connected"), XMLSTR(buf));
                }
                xmlNewChild (srcnode, NULL, XMLSTR("content-type"), 
                        XMLSTR(source->format->contenttype));
            }
        }
        node = avl_get_next(node);
    }
    return(doc);
}

void admin_send_response (xmlDocPtr doc, client_t *client,
        int response, const char *xslt_template)
{
    if (response == RAW)
    {
        xmlChar *buff = NULL;
        int len = 0;
        size_t buf_len;
        ssize_t ret;

        xmlDocDumpMemory(doc, &buff, &len);

        buf_len = len + 1024;
        if (buf_len < 4096)
            buf_len = 4096;

        client_set_queue(client, NULL);
        client->refbuf = refbuf_new(buf_len);

        ret = util_http_build_header(client->refbuf->data, buf_len, 0,
                                     0, 200, NULL,
                                     "text/xml", "utf-8",
                                     NULL, NULL);
        if (ret < 0) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error(client, 500, 0, "Header generation failed.");
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
                                             "text/xml", "utf-8",
                                             NULL, NULL);
                if (ret == -1) {
                    ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                    client_send_error(client, 500, 0, "Header generation failed.");
                    xmlFree(buff);
                    return;
                }
            } else {
                ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
                client_send_error(client, 500, 0, "Buffer reallocation failed.");
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
    if (response == TRANSFORMED)
    {
        char *fullpath_xslt_template;
        int fullpath_xslt_template_len;
        ice_config_t *config = config_get_config();

        fullpath_xslt_template_len = strlen (config->adminroot_dir) + 
            strlen (xslt_template) + 2;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s",
            config->adminroot_dir, PATH_SEPARATOR, xslt_template);
        config_release_config();

        ICECAST_LOG_DEBUG("Sending XSLT (%s)", fullpath_xslt_template);
        xslt_transform(doc, fullpath_xslt_template, client);
        free(fullpath_xslt_template);
    }
}


void admin_handle_request(client_t *client, const char *uri)
{
    const char *mount, *command_string;

    ICECAST_LOG_DEBUG("Admin request (%s)", uri);
    if (!((strcmp(uri, "/admin.cgi") == 0) ||
         (strncmp("/admin/", uri, 7) == 0))) {
        ICECAST_LOG_ERROR("Internal error: admin request isn't");
        client_send_error(client, 401, 1, "You need to authenticate\r\n");
        return;
    }

    if (strcmp(uri, "/admin.cgi") == 0) {
        command_string = uri + 1;
    }
    else {
        command_string = uri + 7;
    }

    ICECAST_LOG_DEBUG("Got command (%s)", command_string);

    if (client->admin_command <= 0) {
        ICECAST_LOG_ERROR("Error parsing command string or unrecognised command: %s",
                command_string);
        client_send_error(client, 400, 0, "Unrecognised command");
        return;
    }

    if (acl_test_admin(client->acl, client->admin_command) != ACL_POLICY_ALLOW) {
        if (client->admin_command == COMMAND_RAW_METADATA_UPDATE &&
            (acl_test_method(client->acl, httpp_req_source) == ACL_POLICY_ALLOW ||
             acl_test_method(client->acl, httpp_req_put)    == ACL_POLICY_ALLOW)) {
            ICECAST_LOG_DEBUG("Granted right to call COMMAND_RAW_METADATA_UPDATE to client because it is allowed to do SOURCE or PUT.");
        } else {
            client_send_error(client, 401, 1, "You need to authenticate\r\n");
            return;
        }
    }

    mount = httpp_get_query_param(client->parser, "mount");

    if(mount != NULL) {
        source_t *source;

        /* this request does not require auth but can apply to files on webroot */
        if (client->admin_command == COMMAND_BUILDM3U) {
            command_buildm3u(client, mount);
            return;
        }

        /* This is a mount request, handle it as such */
        avl_tree_rlock(global.source_tree);
        source = source_find_mount_raw(mount);

        if (source == NULL)
        {
            ICECAST_LOG_WARN("Admin command %s on non-existent source %s", 
                    command_string, mount);
            avl_tree_unlock(global.source_tree);
            client_send_error(client, 400, 0, "Source does not exist");
        }
        else
        {
            if (source->running == 0 && source->on_demand == 0)
            {
                avl_tree_unlock (global.source_tree);
                ICECAST_LOG_INFO("Received admin command %s on unavailable mount \"%s\"",
                        command_string, mount);
                client_send_error(client, 400, 0, "Source is not available");
                return;
            }
            if (client->admin_command == COMMAND_SHOUTCAST_METADATA_UPDATE &&
                    source->shoutcast_compat == 0)
            {
                avl_tree_unlock (global.source_tree);
                ICECAST_LOG_ERROR("illegal change of metadata on non-shoutcast "
                        "compatible stream");
                client_send_error(client, 400, 0, "illegal metadata call");
                return;
            }
            ICECAST_LOG_INFO("Received admin command %s on mount \"%s\"", 
                    command_string, mount);
            admin_handle_mount_request(client, source);
            avl_tree_unlock(global.source_tree);
        }
    }
    else {
        admin_handle_general_request(client);
    }
}

static void admin_handle_general_request(client_t *client)
{
    switch(client->admin_command) {
        case COMMAND_RAW_STATS:
            command_stats(client, NULL, RAW);
            break;
        case COMMAND_RAW_QUEUE_RELOAD:
            command_queue_reload(client, RAW);
            break;
        case COMMAND_RAW_LIST_MOUNTS:
            command_list_mounts(client, RAW);
            break;
        case COMMAND_RAW_LISTSTREAM:
            command_list_mounts(client, RAW);
            break;
        case COMMAND_PLAINTEXT_LISTSTREAM:
            command_list_mounts(client, PLAINTEXT);
            break;
        case COMMAND_TRANSFORMED_STATS:
            command_stats(client, NULL, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_QUEUE_RELOAD:
            command_queue_reload(client, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_LIST_MOUNTS:
            command_list_mounts(client, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_LISTSTREAM:
            command_list_mounts(client, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_MOVE_CLIENTS:
            command_list_mounts(client, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_MANAGEAUTH:
            command_manageauth(client, TRANSFORMED);
            break;
        case COMMAND_RAW_MANAGEAUTH:
            command_manageauth(client, RAW);
            break;
        default:
            ICECAST_LOG_WARN("General admin request not recognised");
            client_send_error(client, 400, 0, "Unknown admin request");
            return;
    }
}

static void admin_handle_mount_request(client_t *client, source_t *source) {
    switch(client->admin_command) {
        case COMMAND_RAW_STATS:
            command_stats(client, source->mount, RAW);
            break;
        case COMMAND_RAW_FALLBACK:
            command_fallback(client, source, RAW);
            break;
        case COMMAND_RAW_METADATA_UPDATE:
            command_metadata(client, source, RAW);
            break;
        case COMMAND_TRANSFORMED_METADATA_UPDATE:
            command_metadata(client, source, TRANSFORMED);
            break;
        case COMMAND_SHOUTCAST_METADATA_UPDATE:
            command_shoutcast_metadata(client, source);
            break;
        case COMMAND_RAW_SHOW_LISTENERS:
            command_show_listeners(client, source, RAW);
            break;
        case COMMAND_RAW_MOVE_CLIENTS:
            command_move_clients(client, source, RAW);
            break;
        case COMMAND_RAW_KILL_CLIENT:
            command_kill_client(client, source, RAW);
            break;
        case COMMAND_RAW_KILL_SOURCE:
            command_kill_source(client, source, RAW);
            break;
        case COMMAND_TRANSFORMED_STATS:
            command_stats(client, source->mount, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_FALLBACK:
            command_fallback(client, source, RAW);
            break;
        case COMMAND_TRANSFORMED_SHOW_LISTENERS:
            command_show_listeners(client, source, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_MOVE_CLIENTS:
            command_move_clients(client, source, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_KILL_CLIENT:
            command_kill_client(client, source, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_KILL_SOURCE:
            command_kill_source(client, source, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_UPDATEMETADATA:
            command_updatemetadata(client, source, TRANSFORMED);
            break;
        case COMMAND_RAW_UPDATEMETADATA:
            command_updatemetadata(client, source, RAW);
            break;
        default:
            ICECAST_LOG_WARN("Mount request not recognised");
            client_send_error(client, 400, 0, "Mount request unknown");
            break;
    }
}

#define COMMAND_REQUIRE(client,name,var) \
    do { \
        (var) = httpp_get_query_param((client)->parser, (name)); \
        if((var) == NULL) { \
            client_send_error((client), 400, 0, "Missing parameter"); \
            return; \
        } \
    } while(0);
#define COMMAND_OPTIONAL(client,name,var) \
    (var) = httpp_get_query_param((client)->parser, (name))

static void html_success(client_t *client, char *message)
{
    ssize_t ret;

    ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
                                 0, 200, NULL,
                                 "text/html", "utf-8",
                                 "", NULL);

    if (ret == -1 || ret >= PER_CLIENT_REFBUF_SIZE) {
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_error(client, 500, 0, "Header generation failed.");
        return;
    }

    snprintf(client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret,
        "<html><head><title>Admin request successful</title></head>"
        "<body><p>%s</p></body></html>", message);

    client->respcode = 200;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


static void command_move_clients(client_t *client, source_t *source,
    int response)
{
    const char *dest_source;
    source_t *dest;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[255];
    int parameters_passed = 0;

    ICECAST_LOG_DEBUG("Doing optional check");
    if((COMMAND_OPTIONAL(client, "destination", dest_source))) {
        parameters_passed = 1;
    }
    ICECAST_LOG_DEBUG("Done optional check (%d)", parameters_passed);
    if (!parameters_passed) {
        doc = admin_build_sourcelist(source->mount);
        admin_send_response(doc, client, response, 
             MOVECLIENTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
        return;
    }

    dest = source_find_mount (dest_source);

    if (dest == NULL)
    {
        client_send_error(client, 400, 0, "No such destination");
        return;
    }

    if (strcmp (dest->mount, source->mount) == 0)
    {
        client_send_error(client, 400, 0, "supplied mountpoints are identical");
        return;
    }

    if (dest->running == 0 && dest->on_demand == 0)
    {
        client_send_error(client, 400, 0, "Destination not running");
        return;
    }

    ICECAST_LOG_INFO("source is \"%s\", destination is \"%s\"", source->mount, dest->mount);

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    source_move_clients (source, dest);

    memset(buf, '\000', sizeof(buf));
    snprintf (buf, sizeof(buf), "Clients moved from %s to %s",
        source->mount, dest_source);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));

    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}

static inline xmlNodePtr __add_listener(client_t *client, xmlNodePtr parent, time_t now, operation_mode mode) {
    const char *tmp;
    xmlNodePtr node;
    char buf[22];

    /* TODO: kh has support for a child node "lag". We should add that.
     * BEFORE RELEASE 2.5.0 REVIEW #2097: Check if we are on-track for lowercasing child nodes.
     * BEFORE RELEASE 2.6.0 TODO #2097: Change case of child nodes to lower case.
     * The case of <ID>, <IP>, <UserAgent> and <Connected> should be converted to lower case.
     */

    node = xmlNewChild(parent, NULL, XMLSTR("listener"), NULL);
    if (!node)
        return NULL;

    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf)-1, "%lu", client->con->id);
    xmlSetProp(node, XMLSTR("id"), XMLSTR(buf));
    xmlNewChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "ID" : "id"), XMLSTR(buf));

    xmlNewChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "IP" : "ip"), XMLSTR(client->con->ip));

    tmp = httpp_getvar(client->parser, "user-agent");
    if (tmp)
        xmlNewChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "UserAgent" : "useragent"), XMLSTR(tmp));

    tmp = httpp_getvar(client->parser, "referer");
    if (tmp)
        xmlNewChild(node, NULL, XMLSTR("referer"), XMLSTR(tmp));

    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(now - client->con->con_time));
    xmlNewChild(node, NULL, XMLSTR(mode == OMODE_LEGACY ? "Connected" : "connected"), XMLSTR(buf));

    if (client->username)
        xmlNewChild(node, NULL, XMLSTR("username"), XMLSTR(client->username));

    if (client->role)
        xmlNewChild(node, NULL, XMLSTR("role"), XMLSTR(client->role));

    return node;
}

void admin_add_listeners_to_mount(source_t *source, xmlNodePtr parent, operation_mode mode) {
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

static void command_show_listeners(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;
    char buf[22];

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);
    srcnode = xmlNewChild(node, NULL, XMLSTR("source"), NULL);
    xmlSetProp(srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    xmlDocSetRootElement(doc, node);

    memset(buf, '\000', sizeof(buf));
    snprintf (buf, sizeof(buf), "%lu", source->listeners);
    xmlNewChild(srcnode, NULL, XMLSTR("Listeners"), XMLSTR(buf));

    admin_add_listeners_to_mount(source, srcnode, client->mode);

    admin_send_response(doc, client, response, 
        LISTCLIENTS_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
}

static void command_buildm3u(client_t *client,  const char *mount)
{
    const char *username = NULL;
    const char *password = NULL;
    ice_config_t *config;
    ssize_t ret;

    COMMAND_REQUIRE(client, "username", username);
    COMMAND_REQUIRE(client, "password", password);

    ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
                                 0, 200, NULL,
                                 "audio/x-mpegurl", NULL,
                                 NULL, NULL);

    if (ret == -1 || ret >= (PER_CLIENT_REFBUF_SIZE - 512)) { /* we want at least 512 Byte left for data */
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_error(client, 500, 0, "Header generation failed.");
        return;
    }


    config = config_get_config();
    snprintf(client->refbuf->data + ret, PER_CLIENT_REFBUF_SIZE - ret,
        "Content-Disposition: attachment; filename=listen.m3u\r\n\r\n"
        "http://%s:%s@%s:%d%s\r\n",
        username,
        password,
        config->hostname,
        config->port,
        mount
    );
    config_release_config();

    client->respcode = 200;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}

xmlNodePtr admin_add_role_to_authentication(auth_t *auth, xmlNodePtr parent) {
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

static void command_manageauth(client_t *client, int response) {
    xmlDocPtr doc;
    xmlNodePtr node, rolenode, usersnode, msgnode;
    const char *action = NULL;
    const char *username = NULL;
    const char *idstring = NULL;
    char *message = NULL;
    int ret = AUTH_OK;
    int error_code = 400;
    const char *error_message = "missing parameter";
    long unsigned int id;
    ice_config_t *config = config_get_config();
    auth_t *auth;

    do
    {
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
            error_code = 404;
            error_message = "Role not found";
            break;
        }

        COMMAND_OPTIONAL(client, "action", action);
        COMMAND_OPTIONAL(client, "username", username);

        if (action == NULL)
            action = "list";

        if (!strcmp(action, "add"))
        {
            const char *password = NULL;
            COMMAND_OPTIONAL(client, "password", password);

            if (username == NULL || password == NULL) {
                ICECAST_LOG_WARN("manage auth request add for %lu but no user/pass", id);
                break;
            }

            if (!auth->adduser) {
                error_message = "Adding users to role not supported by role";
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
        if (!strcmp(action, "delete"))
        {
            if (username == NULL) {
                ICECAST_LOG_WARN("manage auth request delete for %lu but no username", id);
                break;
            }

            if (!auth->deleteuser) {
                error_message = "Deleting users from role not supported by role";
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
        node = xmlNewDocNode(doc, NULL, XMLSTR("icestats"), NULL);

        rolenode = admin_add_role_to_authentication(auth, node);

        if (message) {
            msgnode = xmlNewChild(node, NULL, XMLSTR("iceresponse"), NULL);
            xmlNewChild(msgnode, NULL, XMLSTR("message"), XMLSTR(message));
        }

        xmlDocSetRootElement(doc, node);

        if (auth && auth->listuser) {
            usersnode = xmlNewChild(rolenode, NULL, XMLSTR("users"), NULL);
            auth->listuser(auth, usersnode);
        }

        config_release_config();
        auth_release(auth);

        admin_send_response(doc, client, response, 
                MANAGEAUTH_TRANSFORMED_REQUEST);
        free (message);
        xmlFreeDoc(doc);
        return;
    } while (0);

    config_release_config();
    auth_release(auth);
    client_send_error(client, error_code, 0, error_message);
}

static void command_kill_source(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("Source Removed"));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    xmlDocSetRootElement(doc, node);

    source->running = 0;

    admin_send_response(doc, client, response,
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}

static void command_kill_client(client_t *client, source_t *source,
    int response)
{
    const char *idtext;
    int id;
    client_t *listener;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[50] = "";

    COMMAND_REQUIRE(client, "id", idtext);

    id = atoi(idtext);

    listener = source_find_client(source, id);

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);
    ICECAST_LOG_DEBUG("Response is %d", response);

    if(listener != NULL) {
        ICECAST_LOG_INFO("Admin request: client %d removed", id);

        /* This tags it for removal on the next iteration of the main source
         * loop
         */
        listener->con->error = 1;
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d removed", id);
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    }
    else {
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d not found", id);
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR(buf));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("0"));
    }
    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}

static void command_fallback(client_t *client, source_t *source,
    int response)
{
    const char *fallback;
    char *old;

    ICECAST_LOG_DEBUG("Got fallback request");

    COMMAND_REQUIRE(client, "fallback", fallback);

    old = source->fallback_mount;
    source->fallback_mount = strdup(fallback);
    free(old);

    html_success(client, "Fallback configured");
}

static void command_metadata(client_t *client, source_t *source,
    int response)
{
    const char *action;
    const char *song, *title, *artist, *charset;
    format_plugin_t *plugin;
    xmlDocPtr doc;
    xmlNodePtr node;
    int same_ip = 1;

    doc = xmlNewDoc(XMLSTR("1.0"));
    node = xmlNewDocNode (doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlDocSetRootElement(doc, node);

    ICECAST_LOG_DEBUG("Got metadata update request");

    if (source->parser->req_type == httpp_req_put) {
        ICECAST_LOG_ERROR("Got legacy SOURCE-style metadata update command on source connected with PUT at mountpoint %s", source->mount);
    }

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_OPTIONAL(client, "song", song);
    COMMAND_OPTIONAL(client, "title", title);
    COMMAND_OPTIONAL(client, "artist", artist);
    COMMAND_OPTIONAL(client, "charset", charset);

    if (strcmp (action, "updinfo") != 0)
    {
        xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("No such action"));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("0"));
        admin_send_response(doc, client, response, 
            ADMIN_XSL_RESPONSE);
        xmlFreeDoc(doc);
        return;
    }

    plugin = source->format;
    if (source->client && strcmp (client->con->ip, source->client->con->ip) != 0)
        if (response == RAW && acl_test_admin(client->acl, COMMAND_RAW_METADATA_UPDATE) != ACL_POLICY_ALLOW)
            same_ip = 0;

    if (same_ip && plugin && plugin->set_tag)
    {
        if (song)
        {
            plugin->set_tag (plugin, "song", song, charset);
            ICECAST_LOG_INFO("Metadata on mountpoint %s changed to \"%s\"", source->mount, song);
        }
        else
        {
            if (artist && title)
            {
                plugin->set_tag (plugin, "title", title, charset);
                plugin->set_tag (plugin, "artist", artist, charset);
                ICECAST_LOG_INFO("Metadata on mountpoint %s changed to \"%s - %s\"",
                        source->mount, artist, title);
            }
        }
        /* updates are now done, let them be pushed into the stream */
        plugin->set_tag (plugin, NULL, NULL, NULL);
    }
    else
    {
        xmlNewChild(node, NULL, XMLSTR("message"), 
            XMLSTR("Mountpoint will not accept URL updates"));
        xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
        admin_send_response(doc, client, response, 
            ADMIN_XSL_RESPONSE);
        xmlFreeDoc(doc);
        return;
    }

    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("Metadata update successful"));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}

static void command_shoutcast_metadata(client_t *client, source_t *source)
{
    const char *action;
    const char *value;
    int same_ip = 1;

    ICECAST_LOG_DEBUG("Got shoutcast metadata update request");

    if (source->parser->req_type == httpp_req_put) {
        ICECAST_LOG_ERROR("Got legacy shoutcast-style metadata update command on source connected with PUT at mountpoint %s", source->mount);
    }

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_REQUIRE(client, "song", value);

    if (strcmp (action, "updinfo") != 0)
    {
        client_send_error(client, 400, 0, "No such action");
        return;
    }
    if (source->client && strcmp (client->con->ip, source->client->con->ip) != 0)
        if (acl_test_admin(client->acl, COMMAND_RAW_METADATA_UPDATE) != ACL_POLICY_ALLOW)
            same_ip = 0;

    if (same_ip && source->format && source->format->set_tag)
    {
        source->format->set_tag (source->format, "title", value, NULL);
        source->format->set_tag (source->format, NULL, NULL, NULL);

        ICECAST_LOG_DEBUG("Metadata on mountpoint %s changed to \"%s\"", 
                source->mount, value);
        html_success(client, "Metadata update successful");
    }
    else
    {
        client_send_error(client, 400, 0, "mountpoint will not accept URL updates");
    }
}

static void command_stats(client_t *client, const char *mount, int response) {
    xmlDocPtr doc;

    ICECAST_LOG_DEBUG("Stats request, sending xml stats");

    doc = stats_get_xml(1, mount, client->mode);
    admin_send_response(doc, client, response, STATS_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
    return;
}

static void command_queue_reload(client_t *client, int response) {
    xmlDocPtr doc;
    xmlNodePtr node;

    global_lock();
    global.schedule_config_reread = 1;
    global_unlock();

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode(doc, NULL, XMLSTR("iceresponse"), NULL);
    xmlNewChild(node, NULL, XMLSTR("message"), XMLSTR("Config reload queued"));
    xmlNewChild(node, NULL, XMLSTR("return"), XMLSTR("1"));
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response, ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
}       


static void command_list_mounts(client_t *client, int response)
{
    ICECAST_LOG_DEBUG("List mounts request");

    if (response == PLAINTEXT)
    {
        ssize_t ret = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
                               0, 200, NULL,
                               "text/plain", "utf-8",
                               "", NULL);

        if (ret == -1 || ret >= PER_CLIENT_REFBUF_SIZE) {
            ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
            client_send_error(client, 500, 0, "Header generation failed.");
            return;
        }

        client->refbuf->len = strlen (client->refbuf->data);
        client->respcode = 200;

        client->refbuf->next = stats_get_streams ();
        fserve_add_client (client, NULL);
    }
    else
    {
        xmlDocPtr doc;
        avl_tree_rlock (global.source_tree);
        doc = admin_build_sourcelist(NULL);
        avl_tree_unlock (global.source_tree);

        admin_send_response(doc, client, response, 
            LISTMOUNTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
    }
}

static void command_updatemetadata(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode (doc, NULL, XMLSTR("icestats"), NULL);
    srcnode = xmlNewChild (node, NULL, XMLSTR("source"), NULL);
    xmlSetProp (srcnode, XMLSTR("mount"), XMLSTR(source->mount));
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response, 
        UPDATEMETADATA_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
}
