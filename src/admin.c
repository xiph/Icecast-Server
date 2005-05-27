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
#include "event.h"
#include "stats.h"
#include "os.h"
#include "xslt.h"

#include "format.h"

#include "logging.h"
#include "auth.h"
#ifdef _WIN32
#define snprintf _snprintf
#endif

#define CATMODULE "admin"

#define COMMAND_ERROR             (-1)

/* Mount-specific commands */
#define COMMAND_RAW_FALLBACK        1
#define COMMAND_RAW_METADATA_UPDATE 2
#define COMMAND_RAW_SHOW_LISTENERS  3
#define COMMAND_RAW_MOVE_CLIENTS    4
#define COMMAND_RAW_MANAGEAUTH      5
#define COMMAND_SHOUTCAST_METADATA_UPDATE   6
#define COMMAND_RAW_UPDATEMETADATA  7

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
#define COMMAND_RAW_ADMIN_FUNCTION          105
#define COMMAND_RAW_MANAGE_RELAY            106
#define COMMAND_TRANSFORMED_LIST_MOUNTS     201
#define COMMAND_TRANSFORMED_STATS           202
#define COMMAND_TRANSFORMED_LISTSTREAM      203
#define COMMAND_TRANSFORMED_ADMIN_FUNCTION  204
#define COMMAND_TRANSFORMED_MANAGE_RELAY    205

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
#define MANAGE_RELAY_RAW_REQUEST "managerelays"
#define MANAGE_RELAY_TRANSFORMED_REQUEST "managerelays.xsl"
#define ADM_FUNCTION_RAW_REQUEST "function"
#define ADM_FUNCTION_TRANSFORMED_REQUEST "function.xsl"
#define DEFAULT_RAW_REQUEST ""
#define DEFAULT_TRANSFORMED_REQUEST ""
#define BUILDM3U_RAW_REQUEST "buildm3u"

#define RAW         1
#define TRANSFORMED 2
#define PLAINTEXT   3

int admin_get_command(char *command)
{
    if(!strcmp(command, FALLBACK_RAW_REQUEST))
        return COMMAND_RAW_FALLBACK;
    else if(!strcmp(command, FALLBACK_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_FALLBACK;
    else if(!strcmp(command, METADATA_RAW_REQUEST))
        return COMMAND_RAW_METADATA_UPDATE;
    else if(!strcmp(command, METADATA_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_METADATA_UPDATE;
    else if(!strcmp(command, SHOUTCAST_METADATA_REQUEST))
        return COMMAND_SHOUTCAST_METADATA_UPDATE;
    else if(!strcmp(command, LISTCLIENTS_RAW_REQUEST))
        return COMMAND_RAW_SHOW_LISTENERS;
    else if(!strcmp(command, LISTCLIENTS_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_SHOW_LISTENERS;
    else if(!strcmp(command, STATS_RAW_REQUEST))
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, STATS_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_STATS;
    else if(!strcmp(command, "stats.xml")) /* The old way */
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, LISTMOUNTS_RAW_REQUEST))
        return COMMAND_RAW_LIST_MOUNTS;
    else if(!strcmp(command, LISTMOUNTS_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_LIST_MOUNTS;
    else if(!strcmp(command, STREAMLIST_RAW_REQUEST))
        return COMMAND_RAW_LISTSTREAM;
    else if(!strcmp(command, STREAMLIST_PLAINTEXT_REQUEST))
        return COMMAND_PLAINTEXT_LISTSTREAM;
    else if(!strcmp(command, MOVECLIENTS_RAW_REQUEST))
        return COMMAND_RAW_MOVE_CLIENTS;
    else if(!strcmp(command, MOVECLIENTS_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_MOVE_CLIENTS;
    else if(!strcmp(command, KILLCLIENT_RAW_REQUEST))
        return COMMAND_RAW_KILL_CLIENT;
    else if(!strcmp(command, KILLCLIENT_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_KILL_CLIENT;
    else if(!strcmp(command, KILLSOURCE_RAW_REQUEST))
        return COMMAND_RAW_KILL_SOURCE;
    else if(!strcmp(command, KILLSOURCE_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_KILL_SOURCE;
    else if(!strcmp(command, MANAGEAUTH_RAW_REQUEST))
        return COMMAND_RAW_MANAGEAUTH;
    else if(!strcmp(command, MANAGEAUTH_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_MANAGEAUTH;
    else if(!strcmp(command, UPDATEMETADATA_RAW_REQUEST))
        return COMMAND_RAW_UPDATEMETADATA;
    else if(!strcmp(command, UPDATEMETADATA_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_UPDATEMETADATA;
    else if(!strcmp(command, BUILDM3U_RAW_REQUEST))
        return COMMAND_BUILDM3U;
    else if(!strcmp(command, MANAGE_RELAY_RAW_REQUEST))
        return COMMAND_RAW_MANAGE_RELAY;
    else if(!strcmp(command, MANAGE_RELAY_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_MANAGE_RELAY;
    else if(!strcmp(command, ADM_FUNCTION_RAW_REQUEST))
        return COMMAND_RAW_ADMIN_FUNCTION;
    else if(!strcmp(command, ADM_FUNCTION_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_ADMIN_FUNCTION;
    else if(!strcmp(command, DEFAULT_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_STATS;
    else if(!strcmp(command, DEFAULT_RAW_REQUEST))
        return COMMAND_TRANSFORMED_STATS;
    else
        return COMMAND_ERROR;
}

static void command_fallback(client_t *client, source_t *source, int response);
static void command_metadata(client_t *client, source_t *source, int response);
static void command_shoutcast_metadata(client_t *client, source_t *source);
static void command_show_listeners(client_t *client, source_t *source,
        int response);
static void command_move_clients(client_t *client, source_t *source,
        int response);
static void command_stats(client_t *client, int response);
static void command_list_mounts(client_t *client, int response);
static void command_kill_client(client_t *client, source_t *source,
        int response);
static void command_manageauth(client_t *client, source_t *source,
        int response);
static void command_buildm3u(client_t *client, source_t *source,
        int response);
static void command_kill_source(client_t *client, source_t *source,
        int response);
static void command_updatemetadata(client_t *client, source_t *source,
        int response);
static void command_admin_function (client_t *client, int response);
static void command_manage_relay (client_t *client, int response);

static void admin_handle_mount_request(client_t *client, source_t *source,
        int command);
static void admin_handle_general_request(client_t *client, int command);
static void admin_send_response(xmlDocPtr doc, client_t *client, 
        int response, char *xslt_template);
static void html_write(client_t *client, char *fmt, ...);

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

    doc = xmlNewDoc("1.0");
    xmlnode = xmlNewDocNode(doc, NULL, "icestats", NULL);
    xmlDocSetRootElement(doc, xmlnode);

    if (mount) {
        xmlNewChild(xmlnode, NULL, "current_source", mount);
    }

    node = avl_get_first(global.source_tree);
    while(node) {
        source = (source_t *)node->key;
        if (mount && strcmp (mount, source->mount) == 0)
        {
            node = avl_get_next (node);
            continue;
        }

        thread_mutex_lock (&source->lock);
        if (source->running || source->on_demand)
        {
            ice_config_t *config;
            mount_proxy *mountinfo;

            srcnode = xmlNewChild (xmlnode, NULL, "source", NULL);
            xmlSetProp (srcnode, "mount", source->mount);

            xmlNewChild (srcnode, NULL, "fallback", 
                    (source->fallback_mount != NULL)?
                    source->fallback_mount:"");
            snprintf (buf, sizeof(buf), "%lu", source->listeners);
            xmlNewChild (srcnode, NULL, "listeners", buf);

            config = config_get_config();
            mountinfo = config_find_mount (config, source->mount);
            if (mountinfo && mountinfo->auth)
            {
                xmlNewChild(srcnode, NULL, "authenticator", 
                        mountinfo->auth->type);
            }
            config_release_config();

            if (source->running)
            {
                snprintf (buf, sizeof(buf), "%lu",
                        (unsigned long)(now - source->client->con->con_time));
                xmlNewChild (srcnode, NULL, "Connected", buf);
                xmlNewChild (srcnode, NULL, "content-type", 
                        source->format->contenttype);
            }
        }
        thread_mutex_unlock (&source->lock);
        node = avl_get_next(node);
    }
    return(doc);
}

void admin_send_response(xmlDocPtr doc, client_t *client, 
        int response, char *xslt_template)
{
    xmlChar *buff = NULL;
    int len = 0;
    ice_config_t *config;
    char *fullpath_xslt_template;
    int fullpath_xslt_template_len;
    char *adminwebroot;

    client->respcode = 200;
    if (response == RAW) {
        // xmlDocDumpMemory(doc, &buff, &len);
        xmlDocDumpFormatMemoryEnc (doc, &buff, &len, NULL, 1);
        html_write(client, "HTTP/1.0 200 OK\r\n"
               "Content-Length: %d\r\n"
               "Content-Type: text/xml\r\n"
               "\r\n", len);
        html_write(client, "%s", buff);
    }
    if (response == TRANSFORMED) {
        config = config_get_config();
        adminwebroot = config->adminroot_dir;
        fullpath_xslt_template_len = strlen(adminwebroot) + 
            strlen(xslt_template) + 2;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s",
            adminwebroot, PATH_SEPARATOR, xslt_template);
        config_release_config();
        html_write(client, "HTTP/1.0 200 OK\r\n"
               "Content-Type: text/html\r\n"
               "\r\n");
        DEBUG1("Sending XSLT (%s)", fullpath_xslt_template);
        xslt_transform(doc, fullpath_xslt_template, client);
        free(fullpath_xslt_template);
    }
    if (buff) {
        xmlFree(buff);
    }
}
void admin_handle_request(client_t *client, char *uri)
{
    char *mount, *command_string;
    int command;

    DEBUG1("Admin request (%s)", uri);
    if (!((strcmp(uri, "/admin.cgi") == 0) ||
                (strncmp("/admin/", uri, 7) == 0))) {
        ERROR0("Internal error: admin request isn't");
        client_send_401(client);
        return;
    }

    if (strcmp(uri, "/admin.cgi") == 0) {
        command_string = uri + 1;
    }
    else {
        command_string = uri + 7;
    }

    DEBUG1("Got command (%s)", command_string);
    command = admin_get_command(command_string);

    if(command < 0) {
        ERROR1("Error parsing command string or unrecognised command: %s",
                command_string);
        client_send_400(client, "Unrecognised command");
        return;
    }

    if (command == COMMAND_SHOUTCAST_METADATA_UPDATE) {

        ice_config_t *config;
        char *pass = httpp_get_query_param (client->parser, "pass");
        if (pass == NULL)
        {
            client_send_400 (client, "missing pass parameter");
            return;
        }
        config = config_get_config ();
        httpp_set_query_param (client->parser, "mount", config->shoutcast_mount);
        httpp_setvar (client->parser, HTTPP_VAR_PROTOCOL, "ICY");
        httpp_setvar (client->parser, HTTPP_VAR_ICYPASSWORD, pass);
        config_release_config ();
    }

    mount = httpp_get_query_param(client->parser, "mount");

    if(mount != NULL) {
        source_t *source;

        if (command == COMMAND_BUILDM3U) {
            client->authenticated = 1;
        }
        /* This is a mount request, but admin user is allowed */
        if (client->authenticated != 1)
        {
            if (connection_check_admin_pass(client->parser) == 0)
            {
                if (connection_check_source_pass(client->parser, mount) == 0)
                {
                    INFO1("Bad or missing password on mount modification admin "
                            "request (command: %s)", command_string);
                    client_send_401(client);
                    return;
                }
            }
        }
        
        avl_tree_rlock(global.source_tree);
        source = source_find_mount_raw(mount);

        if (source == NULL)
        {
            WARN2("Admin command %s on non-existent source %s", 
                    command_string, mount);
            avl_tree_unlock(global.source_tree);
            client_send_400 (client, "Source does not exist");
        }
        else
        {
            if (source->running == 0 && source->on_demand == 0)
            {
                avl_tree_unlock (global.source_tree);
                INFO2("Received admin command %s on unavailable mount \"%s\"",
                        command_string, mount);
                client_send_400 (client, "Source is not available");
                return;
            }
            if (command == COMMAND_SHOUTCAST_METADATA_UPDATE &&
                    source->shoutcast_compat == 0)
            {
                avl_tree_unlock (global.source_tree);
                ERROR0 ("illegal change of metadata on non-shoutcast "
                        "compatible stream");
                client_send_400 (client, "illegal metadata call");
                return;
            }
            INFO2("Received admin command %s on mount \"%s\"", 
                    command_string, mount);
            admin_handle_mount_request (client, source, command);
            avl_tree_unlock(global.source_tree);
        }
    }
    else {
        if (command == COMMAND_PLAINTEXT_LISTSTREAM) {
            /* this request is used by a slave relay to retrieve
               mounts from the master, so handle this request
               validating against the relay password */
            if(!connection_check_relay_pass(client->parser)) {
                INFO1("Bad or missing password on admin command "
                      "request (command: %s)", command_string);
                client_send_401(client);
                return;
            }
        }
        else {
            if(!connection_check_admin_pass (client->parser)) {
                INFO1("Bad or missing password on admin command "
                      "request (command: %s)", command_string);
                client_send_401(client);
                return;
            }
        }
        
        admin_handle_general_request(client, command);
    }
}

static void admin_handle_general_request(client_t *client, int command)
{
    switch(command) {
        case COMMAND_RAW_STATS:
            command_stats(client, RAW);
            break;
        case COMMAND_RAW_LIST_MOUNTS:
            command_list_mounts(client, RAW);
            break;
        case COMMAND_RAW_LISTSTREAM:
            command_list_mounts(client, RAW);
            break;
        case COMMAND_RAW_MANAGE_RELAY:
            command_manage_relay (client, RAW);
            break;
        case COMMAND_RAW_ADMIN_FUNCTION:
            command_admin_function(client, RAW);
            break;
        case COMMAND_PLAINTEXT_LISTSTREAM:
            command_list_mounts(client, PLAINTEXT);
            break;
        case COMMAND_TRANSFORMED_STATS:
            command_stats(client, TRANSFORMED);
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
        case COMMAND_TRANSFORMED_MANAGE_RELAY:
            command_manage_relay (client, TRANSFORMED);
            break;
        case COMMAND_TRANSFORMED_ADMIN_FUNCTION:
            command_admin_function(client, TRANSFORMED);
            break;
        default:
            WARN0("General admin request not recognised");
            client_send_400(client, "Unknown admin request");
            return;
    }
}

static void admin_handle_mount_request(client_t *client, source_t *source, 
        int command)
{
    switch(command) {
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
        case COMMAND_TRANSFORMED_MANAGEAUTH:
            command_manageauth(client, source, TRANSFORMED);
            break;
        case COMMAND_RAW_MANAGEAUTH:
            command_manageauth(client, source, RAW);
            break;
        case COMMAND_TRANSFORMED_UPDATEMETADATA:
            command_updatemetadata(client, source, TRANSFORMED);
            break;
        case COMMAND_RAW_UPDATEMETADATA:
            command_updatemetadata(client, source, RAW);
            break;
        case COMMAND_BUILDM3U:
            command_buildm3u(client, source, RAW);
            break;
        default:
            WARN0("Mount request not recognised");
            client_send_400(client, "Mount request unknown");
            break;
    }
}

#define COMMAND_REQUIRE(client,name,var) \
    do { \
        (var) = httpp_get_query_param((client)->parser, (name)); \
        if((var) == NULL) { \
            client_send_400((client), "Missing parameter"); \
            return; \
        } \
    } while(0);
#define COMMAND_OPTIONAL(client,name,var) \
    (var) = httpp_get_query_param((client)->parser, (name))

static void html_success(client_t *client, char *message)
{
    int bytes;

    client->respcode = 200;
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n" 
            "<html><head><title>Admin request successful</title></head>"
            "<body><p>%s</p></body></html>", message);
    if(bytes > 0) client->con->sent_bytes = bytes;
    client_destroy(client);
}

static void html_write(client_t *client, char *fmt, ...)
{
    int bytes;
    va_list ap;

    va_start(ap, fmt);
    bytes = sock_write_fmt(client->con->sock, fmt, ap);
    va_end(ap);
    if(bytes > 0) client->con->sent_bytes = bytes;
}

static void command_move_clients(client_t *client, source_t *source,
    int response)
{
    char *dest_source;
    source_t *dest;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[255];
    int parameters_passed = 0;

    if((COMMAND_OPTIONAL(client, "destination", dest_source))) {
        parameters_passed = 1;
    }
    if (!parameters_passed) {
        doc = admin_build_sourcelist(source->mount);
        admin_send_response(doc, client, response, 
             MOVECLIENTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
        client_destroy(client);
        return;
    }

    dest = source_find_mount (dest_source);

    if (dest == NULL)
    {
        client_send_400 (client, "No such destination");
        return;
    }

    if (strcmp (dest->mount, source->mount) == 0)
    {
        client_send_400 (client, "supplied mountpoints are identical");
        return;
    }

    if (dest->running == 0 && dest->on_demand == 0)
    {
        client_send_400 (client, "Destination not running");
        return;
    }

    DEBUG2("source is \"%s\", destination is \"%s\"", source->mount, dest->mount);

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);

    source_move_clients (source, dest);

    memset(buf, '\000', sizeof(buf));
    snprintf (buf, sizeof(buf), "Clients moved from %s to %s",
            source->mount, dest_source);
    xmlNewChild(node, NULL, "message", buf);
    xmlNewChild(node, NULL, "return", "1");

    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static int admin_function (const char *function, char *buf, unsigned int len)
{
    if (strcmp (function, "reopenlog") == 0)
    {
        ice_config_t *config = config_grab_config();

        restart_logging (config);
        config_release_config();
        snprintf (buf, len, "Re-opening log files");
        return 0;
    }
    if (strcmp (function, "updatecfg") == 0)
    {
        event_config_read(NULL);
        snprintf (buf, len, "Updating from configuration file");
        return 0;
    }
    return 1;
}


static void command_admin_function (client_t *client, int response)
{
    xmlDocPtr doc;
    xmlNodePtr node;
    char *perform;
    char buf[256];

    COMMAND_REQUIRE (client, "perform", perform);
    if (admin_function (perform, buf, sizeof buf) < 0)
    {
        client_send_400 (client, "No such handler");
        return;
    }
    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);

    xmlNewChild(node, NULL, "message", buf);
    xmlNewChild(node, NULL, "return", "1");

    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}


static void add_relay_xmlnode (xmlNodePtr node, relay_server *relay, int master)
{
    xmlNodePtr relaynode = xmlNewChild (node, NULL, "relay", NULL);
    char str [20];
    xmlNewChild (relaynode, NULL, "server", relay->server);
    xmlNewChild (relaynode, NULL, "mount", relay->mount);
    snprintf (str, sizeof (str), "%d", relay->port);
    xmlNewChild (relaynode, NULL, "port", str);
    xmlNewChild (relaynode, NULL, "localmount", relay->localmount);
    snprintf (str, sizeof (str), "%d", relay->enable);
    xmlNewChild (relaynode, NULL, "enable", str);
    snprintf (str, sizeof (str), "%d", relay->on_demand);
    xmlNewChild (relaynode, NULL, "on_demand", str);
    snprintf (str, sizeof (str), "%d", master);
    xmlNewChild (relaynode, NULL, "master", str);
}


static void command_manage_relay (client_t *client, int response)
{
    char *relay_mount, *enable;
    const char *msg;
    relay_server *relay;
    xmlDocPtr doc;
    xmlNodePtr node;

    COMMAND_OPTIONAL (client, "relay", relay_mount);
    COMMAND_OPTIONAL (client, "enable", enable);

    if (relay_mount == NULL || enable == NULL)
    {
        doc = xmlNewDoc ("1.0");
        node = xmlNewDocNode (doc, NULL, "icerelaystats", NULL);
        xmlDocSetRootElement(doc, node);
        thread_mutex_lock (&(config_locks()->relay_lock));

        for (relay = global.relays; relay; relay=relay->next)
            add_relay_xmlnode (node, relay, 0);
        for (relay = global.master_relays; relay; relay=relay->next)
            add_relay_xmlnode (node, relay, 1);

        thread_mutex_unlock (&(config_locks()->relay_lock));
        admin_send_response (doc, client, response, MANAGE_RELAY_TRANSFORMED_REQUEST);
        xmlFreeDoc (doc);
        client_destroy (client);
        return;
    }

    thread_mutex_lock (&(config_locks()->relay_lock));

    relay = slave_find_relay (global.relays, relay_mount);
    if (relay == NULL)
        relay = slave_find_relay (global.master_relays, relay_mount);

    msg = "no such relay";
    if (relay)
    {
        relay->enable = atoi (enable);
        msg = "relay has been changed";
        if (relay->enable == 0)
        {
            if (relay->source && relay->source->running == 0)
                relay->source->on_demand = 0;
        }
        slave_rebuild_mounts();
    }
    thread_mutex_unlock (&(config_locks()->relay_lock));

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);
    xmlNewChild(node, NULL, "message", msg);
    xmlNewChild(node, NULL, "return", "1");
    admin_send_response(doc, client, response, ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}


static void command_show_listeners(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode, listenernode;
    client_t *current;
    char buf[22];
    char *userAgent = NULL;
    time_t now = time(NULL);

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "icestats", NULL);
    srcnode = xmlNewChild(node, NULL, "source", NULL);

    thread_mutex_lock (&source->lock);

    xmlSetProp(srcnode, "mount", source->mount);
    xmlDocSetRootElement(doc, node);

    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf), "%lu", source->listeners);
    xmlNewChild(srcnode, NULL, "Listeners", buf);

    current = source->active_clients;
    while (current)
    {
        listenernode = xmlNewChild(srcnode, NULL, "listener", NULL);
        xmlNewChild(listenernode, NULL, "IP", current->con->ip);
        userAgent = httpp_getvar(current->parser, "user-agent");
        if (userAgent) {
            xmlNewChild(listenernode, NULL, "UserAgent", userAgent);
        }
        else {
            xmlNewChild(listenernode, NULL, "UserAgent", "Unknown");
        }
        memset(buf, '\000', sizeof(buf));
        snprintf (buf, sizeof(buf), "%lu",
                (unsigned long)(now - current->con->con_time));
        xmlNewChild(listenernode, NULL, "Connected", buf);
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%lu", current->con->id);
        xmlNewChild(listenernode, NULL, "ID", buf);
        if (current->username)
            xmlNewChild(listenernode, NULL, "username", current->username);

        current = current->next;
    }

    thread_mutex_unlock (&source->lock);

    admin_send_response(doc, client, response, 
        LISTCLIENTS_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_buildm3u(client_t *client, source_t *source,
    int response)
{
    char *username = NULL;
    char *password = NULL;
    char *host = NULL;
    int port = 0;
    ice_config_t *config;

    COMMAND_REQUIRE(client, "username", username);
    COMMAND_REQUIRE(client, "password", password);

    config = config_get_config();
    host = strdup(config->hostname);
    port = config->port;
    config_release_config();

    client->respcode = 200;
    sock_write(client->con->sock,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: audio/x-mpegurl\r\n"
        "Content-Disposition = attachment; filename=listen.m3u\r\n\r\n" 
        "http://%s:%s@%s:%d%s\r\n",
        username,
        password,
        host,
        port,
        source->mount
    );

    free(host);
    client_destroy(client);
}


static void command_manageauth(client_t *client, source_t *source,
        int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode, msgnode;
    char *action = NULL;
    char *username = NULL;
    char *password = NULL;
    char *message = NULL;
    int ret = AUTH_OK;
    ice_config_t *config = config_get_config ();
    mount_proxy *mountinfo = config_find_mount (config, source->mount);

    if((COMMAND_OPTIONAL(client, "action", action)))
    {
        if (mountinfo == NULL || mountinfo->auth == NULL)
        {
            WARN1 ("manage auth request for %s but no facility available", source->mount);
            config_release_config ();
            client_send_404 (client, "no such auth facility");
            return;
        }
        if (!strcmp(action, "add")) {
            COMMAND_REQUIRE(client, "username", username);
            COMMAND_REQUIRE(client, "password", password);
            ret = mountinfo->auth->adduser(mountinfo->auth, username, password);
            if (ret == AUTH_FAILED) {
                message = strdup("User add failed - check the icecast error log");
            }
            if (ret == AUTH_USERADDED) {
                message = strdup("User added");
            }
            if (ret == AUTH_USEREXISTS) {
                message = strdup("User already exists - not added");
            }
        }
        if (!strcmp(action, "delete")) {
            COMMAND_REQUIRE(client, "username", username);
            ret = mountinfo->auth->deleteuser(mountinfo->auth, username);
            if (ret == AUTH_FAILED) {
                message = strdup("User delete failed - check the icecast error log");
            }
            if (ret == AUTH_USERDELETED) {
                message = strdup("User deleted");
            }
        }
    }

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "icestats", NULL);
    srcnode = xmlNewChild(node, NULL, "source", NULL);
    xmlSetProp(srcnode, "mount", source->mount);

    if (message) {
        msgnode = xmlNewChild(node, NULL, "iceresponse", NULL);
        xmlNewChild(msgnode, NULL, "message", message);
    }

    xmlDocSetRootElement(doc, node);

    if (mountinfo && mountinfo->auth && mountinfo->auth->listuser)
        mountinfo->auth->listuser (mountinfo->auth, srcnode);

    config_release_config ();

    admin_send_response(doc, client, response, 
            MANAGEAUTH_TRANSFORMED_REQUEST);
    if (message) {
        free(message);
    }
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_kill_source(client_t *client, source_t *source,
        int response)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlNewChild(node, NULL, "message", "Source Removed");
    xmlNewChild(node, NULL, "return", "1");
    xmlDocSetRootElement(doc, node);

    source->running = 0;

    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_kill_client(client_t *client, source_t *source,
    int response)
{
    char *idtext;
    int id;
    client_t *listener;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[50] = "";

    COMMAND_REQUIRE(client, "id", idtext);

    id = atoi(idtext);

    thread_mutex_lock (&source->lock);
    listener = source_find_client(source, id);

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);
    DEBUG1("Response is %d", response);

    if(listener != NULL) {
        INFO1("Admin request: client %d removed", id);

        /* This tags it for removal on the next iteration of the main source
         * loop
         */
        listener->con->error = 1;
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d removed", id);
        xmlNewChild(node, NULL, "message", buf);
        xmlNewChild(node, NULL, "return", "1");
    }
    else {
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "Client %d not found", id);
        xmlNewChild(node, NULL, "message", buf);
        xmlNewChild(node, NULL, "return", "0");
    }
    thread_mutex_unlock (&source->lock);
    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_fallback(client_t *client, source_t *source,
    int response)
{
    char *fallback;
    char *old;

    DEBUG0("Got fallback request");

    COMMAND_REQUIRE(client, "fallback", fallback);

    thread_mutex_lock (&source->lock);
    old = source->fallback_mount;
    source->fallback_mount = strdup(fallback);
    free(old);
    thread_mutex_unlock (&source->lock);

    html_success(client, "Fallback configured");
}

static void command_metadata(client_t *client, source_t *source,
    int response)
{
    char *action;
    char *song, *title, *artist;
    format_plugin_t *plugin;
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);

    DEBUG0("Got metadata update request");

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_OPTIONAL(client, "song", song);
    COMMAND_OPTIONAL(client, "title", title);
    COMMAND_OPTIONAL(client, "artist", artist);

    if (strcmp (action, "updinfo") != 0)
    {
        xmlNewChild(node, NULL, "message", "No such action");
        xmlNewChild(node, NULL, "return", "0");
        admin_send_response(doc, client, response,
            ADMIN_XSL_RESPONSE);
        xmlFreeDoc(doc);
        client_destroy(client);
        return;
    }

    thread_mutex_lock (&source->lock);

    plugin = source->format;

    if (plugin && plugin->set_tag)
    {
        if (song)
        {
            plugin->set_tag (plugin, "song", song);
            INFO2("Metadata on mountpoint %s changed to \"%s\"", source->mount, song);
        }
        else
        {
            if (artist && title)
            {
                plugin->set_tag (plugin, "title", title);
                plugin->set_tag (plugin, "artist", artist);
                INFO3("Metadata on mountpoint %s changed to \"%s - %s\"",
                        source->mount, artist, title);
            }
        }

        thread_mutex_unlock (&source->lock);
    }
    else
    {
        thread_mutex_unlock (&source->lock);
        xmlNewChild(node, NULL, "message", 
            "Mountpoint will not accept URL updates");
        xmlNewChild(node, NULL, "return", "1");
        admin_send_response(doc, client, response, 
            ADMIN_XSL_RESPONSE);
        xmlFreeDoc(doc);
        client_destroy(client);
        return;
    }

    xmlNewChild(node, NULL, "message", "Metadata update successful");
    xmlNewChild(node, NULL, "return", "1");
    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_shoutcast_metadata(client_t *client, source_t *source)
{
    char *action;
    char *value;

    DEBUG0("Got shoutcast metadata update request");

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_REQUIRE(client, "song", value);

    if (strcmp (action, "updinfo") != 0)
    {
        thread_mutex_unlock (&source->lock);
        client_send_400 (client, "No such action");
        return;
    }

    if (source->format && source->format->set_tag)
    {
        source->format->set_tag (source->format, "title", value);

        DEBUG2("Metadata on mountpoint %s changed to \"%s\"", 
                source->mount, value);
        thread_mutex_unlock (&source->lock);
        html_success(client, "Metadata update successful");
    }
    else
    {
        thread_mutex_unlock (&source->lock);
        client_send_400 (client, "mountpoint will not accept URL updates");
    }
}

static void command_stats(client_t *client, int response) {
    xmlDocPtr doc;

    DEBUG0("Stats request, sending xml stats");

    stats_get_xml(&doc, 1);
    admin_send_response(doc, client, response, STATS_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
    client_destroy(client);
    return;
}

static void command_list_mounts(client_t *client, int response)
{
    DEBUG0("List mounts request");

    avl_tree_rlock (global.source_tree);
    if (response == PLAINTEXT)
    {
        char buffer [4096], *buf = buffer;
        unsigned int remaining = sizeof (buffer);
        int ret = snprintf (buffer, remaining,
                "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");

        ice_config_t *config = config_get_config ();
        mount_proxy *mountinfo = config->mounts;
        while (mountinfo)
        {
            mount_proxy *current = mountinfo;
            source_t *source;
            mountinfo = mountinfo->next;

            /* avoid non-specific mounts */
            if (strcmp (current->mountname, "all") == 0)
                continue;
            /* now check that a source is available */
            source = source_find_mount (current->mountname);

            if (source == NULL)
                continue;
            if (source->running == 0 && source->on_demand == 0)
                continue;
            if (source->hidden)
                continue;
            remaining -= ret;
            buf += ret;
            ret = snprintf (buf, remaining, "%s\n", current->mountname);
        }
        avl_tree_unlock (global.source_tree);
        config_release_config();

        /* handle last line */
        if (ret > 0 && (unsigned)ret < remaining)
        {
            remaining -= ret;
            buf += ret;
        }
        sock_write_bytes (client->con->sock, buffer, sizeof (buffer)-remaining);
    }
    else
    {
        xmlDocPtr doc = admin_build_sourcelist(NULL);
        avl_tree_unlock (global.source_tree);

        admin_send_response(doc, client, response, 
                LISTMOUNTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
    }
    client_destroy(client);

    return;
}

static void command_updatemetadata(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "icestats", NULL);
    srcnode = xmlNewChild(node, NULL, "source", NULL);
    xmlSetProp(srcnode, "mount", source->mount);
    xmlDocSetRootElement(doc, node);

    admin_send_response(doc, client, response, 
        UPDATEMETADATA_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
    client_destroy(client);
}

