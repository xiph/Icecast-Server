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
#include "format_mp3.h"

#include "logging.h"
#ifdef _WIN32
#define snprintf _snprintf
#endif

#define CATMODULE "admin"

#define COMMAND_ERROR             (-1)

/* Mount-specific commands */
#define COMMAND_RAW_FALLBACK        1
#define COMMAND_METADATA_UPDATE     2
#define COMMAND_RAW_SHOW_LISTENERS  3
#define COMMAND_RAW_MOVE_CLIENTS    4

#define COMMAND_TRANSFORMED_FALLBACK        50
#define COMMAND_TRANSFORMED_SHOW_LISTENERS  53
#define COMMAND_TRANSFORMED_MOVE_CLIENTS    54

/* Global commands */
#define COMMAND_RAW_LIST_MOUNTS   101
#define COMMAND_RAW_STATS         102
#define COMMAND_RAW_LISTSTREAM    103
#define COMMAND_PLAINTEXT_LISTSTREAM    104
#define COMMAND_TRANSFORMED_LIST_MOUNTS   201
#define COMMAND_TRANSFORMED_STATS         202
#define COMMAND_TRANSFORMED_LISTSTREAM    203

/* Client management commands */
#define COMMAND_RAW_KILL_CLIENT   301
#define COMMAND_RAW_KILL_SOURCE   302
#define COMMAND_TRANSFORMED_KILL_CLIENT   401
#define COMMAND_TRANSFORMED_KILL_SOURCE   402

#define FALLBACK_RAW_REQUEST "fallbacks"
#define FALLBACK_TRANSFORMED_REQUEST "fallbacks.xsl"
#define METADATA_REQUEST "metadata"
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
#define DEFAULT_RAW_REQUEST ""
#define DEFAULT_TRANSFORMED_REQUEST ""

#define RAW         1
#define TRANSFORMED 2
#define PLAINTEXT   3
int admin_get_command(char *command)
{
    if(!strcmp(command, FALLBACK_RAW_REQUEST))
        return COMMAND_RAW_FALLBACK;
    else if(!strcmp(command, FALLBACK_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_FALLBACK;
    else if(!strcmp(command, METADATA_REQUEST))
        return COMMAND_METADATA_UPDATE;
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
    else if(!strcmp(command, DEFAULT_TRANSFORMED_REQUEST))
        return COMMAND_TRANSFORMED_STATS;
    else if(!strcmp(command, DEFAULT_RAW_REQUEST))
        return COMMAND_TRANSFORMED_STATS;
    else
        return COMMAND_ERROR;
}

static void command_fallback(client_t *client, source_t *source, int response);
static void command_metadata(client_t *client, source_t *source);
static void command_show_listeners(client_t *client, source_t *source,
        int response);
static void command_move_clients(client_t *client, source_t *source,
        int response);
static void command_stats(client_t *client, int response);
static void command_list_mounts(client_t *client, int response);
static void command_kill_client(client_t *client, source_t *source,
        int response);
static void command_kill_source(client_t *client, source_t *source,
        int response);
static void admin_handle_mount_request(client_t *client, source_t *source,
        int command);
static void admin_handle_general_request(client_t *client, int command);
static void admin_send_response(xmlDocPtr doc, client_t *client, 
        int response, char *xslt_template);
static void html_write(client_t *client, char *fmt, ...);

xmlDocPtr admin_build_sourcelist(char *current_source)
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

    if (current_source) {
        xmlNewChild(xmlnode, NULL, "current_source", current_source);
    }

    node = avl_get_first(global.source_tree);
    while(node) {
        source = (source_t *)node->key;
        srcnode = xmlNewChild(xmlnode, NULL, "source", NULL);
        xmlSetProp(srcnode, "mount", source->mount);

        xmlNewChild(srcnode, NULL, "fallback", 
                    (source->fallback_mount != NULL)?
                    source->fallback_mount:"");
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%ld", source->listeners);
        xmlNewChild(srcnode, NULL, "listeners", buf);
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%ld", now - source->con->con_time);
        xmlNewChild(srcnode, NULL, "Connected", buf);
        xmlNewChild(srcnode, NULL, "Format", 
            source->format->format_description);
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
        xmlDocDumpMemory(doc, &buff, &len);
        html_write(client, "HTTP/1.0 200 OK\r\n"
               "Content-Length: %d\r\n"
               "Content-Type: text/xml\r\n"
               "\r\n", len);
        html_write(client, buff);
    }
    if (response == TRANSFORMED) {
        config = config_get_config();
        adminwebroot = config->adminroot_dir;
        config_release_config();
        fullpath_xslt_template_len = strlen(adminwebroot) + 
            strlen(xslt_template) + 2;
        fullpath_xslt_template = malloc(fullpath_xslt_template_len);
        memset(fullpath_xslt_template, '\000', fullpath_xslt_template_len);
        snprintf(fullpath_xslt_template, fullpath_xslt_template_len, "%s%s%s",
            adminwebroot, PATH_SEPARATOR, xslt_template);
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

    if(strncmp("/admin/", uri, 7)) {
        ERROR0("Internal error: admin request isn't");
        client_send_401(client);
        return;
    }

    command_string = uri + 7;

    DEBUG1("Got command (%s)", command_string);
    command = admin_get_command(command_string);

    if(command < 0) {
        ERROR1("Error parsing command string or unrecognised command: %s",
                command_string);
        client_send_400(client, "Unrecognised command");
        return;
    }

    mount = httpp_get_query_param(client->parser, "mount");

    if(mount != NULL) {
        source_t *source;

        /* This is a mount request, handle it as such */
        if(!connection_check_admin_pass(client->parser)) {
            if(!connection_check_source_pass(client->parser, mount)) {
                INFO1("Bad or missing password on mount modification admin "
                      "request (command: %s)", command_string);
                client_send_401(client);
                return;
            }
        }
        
        avl_tree_rlock(global.source_tree);
        source = source_find_mount_raw(mount);

        if (source == NULL)
        {
            WARN2("Admin command %s on non-existent source %s", 
                    command_string, mount);
            client_send_400(client, "Source does not exist");
        }
        else
        {
            INFO2("Received admin command %s on mount \"%s\"", 
                    command_string, mount);
            admin_handle_mount_request(client, source, command);
        }
        avl_tree_unlock(global.source_tree);
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
            if(!connection_check_admin_pass(client->parser)) {
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
        case COMMAND_METADATA_UPDATE:
            command_metadata(client, source);
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
    avl_node *client_node;
    client_t *current;
    xmlDocPtr doc;
    xmlNodePtr node;
    char buf[255];
    int parameters_passed = 0;

    DEBUG0("Doing optional check");
    if((COMMAND_OPTIONAL(client, "destination", dest_source))) {
        parameters_passed = 1;
    }
    DEBUG1("Done optional check (%d)", parameters_passed);
    if (!parameters_passed) {
        doc = admin_build_sourcelist(source->mount);
        admin_send_response(doc, client, response, 
             MOVECLIENTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
        client_destroy(client);
        return;
    }

    if (strcmp (dest->mount, source->mount) == 0) 
    {
        client_send_400 (client, "supplied mountpoints are identical");
        return;
    }

    dest = source_find_mount(dest_source);

    if(dest == NULL) {
        client_send_400(client, "No such source");
        return;
    }

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "iceresponse", NULL);
    xmlDocSetRootElement(doc, node);

    avl_tree_wlock(source->client_tree);
    client_node = avl_get_first(source->client_tree);
    while(client_node) {
        current = (client_t *)client_node->key;

        avl_tree_wlock(dest->pending_tree);
        avl_insert(dest->pending_tree, current);
        avl_tree_unlock(dest->pending_tree);

        client_node = avl_get_next(client_node);

        avl_delete(source->client_tree, current, source_remove_client);
        source->listeners--;
    }

    avl_tree_unlock(source->client_tree);
        
    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf)-1, "Clients moved from %s to %s", dest_source, 
        source->mount);
    xmlNewChild(node, NULL, "message", buf);
    xmlNewChild(node, NULL, "return", "1");

    admin_send_response(doc, client, response, 
        ADMIN_XSL_RESPONSE);
    xmlFreeDoc(doc);
    client_destroy(client);
}

static void command_show_listeners(client_t *client, source_t *source,
    int response)
{
    xmlDocPtr doc;
    xmlNodePtr node, srcnode, listenernode;
    avl_node *client_node;
    client_t *current;
    char buf[22];
    char *userAgent = NULL;
    time_t now = time(NULL);

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "icestats", NULL);
    srcnode = xmlNewChild(node, NULL, "source", NULL);
    xmlSetProp(srcnode, "mount", source->mount);
    xmlDocSetRootElement(doc, node);

    memset(buf, '\000', sizeof(buf));
    snprintf(buf, sizeof(buf)-1, "%ld", source->listeners);
    xmlNewChild(srcnode, NULL, "Listeners", buf);

    avl_tree_rlock(source->client_tree);

    client_node = avl_get_first(source->client_tree);
    while(client_node) {
        current = (client_t *)client_node->key;
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
        snprintf(buf, sizeof(buf)-1, "%ld", now - current->con->con_time);
        xmlNewChild(listenernode, NULL, "Connected", buf);
        memset(buf, '\000', sizeof(buf));
        snprintf(buf, sizeof(buf)-1, "%lu", current->con->id);
        xmlNewChild(listenernode, NULL, "ID", buf);
        client_node = avl_get_next(client_node);
    }

    avl_tree_unlock(source->client_tree);
    admin_send_response(doc, client, response, 
        LISTCLIENTS_TRANSFORMED_REQUEST);
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

    old = source->fallback_mount;
    source->fallback_mount = strdup(fallback);
    free(old);

    html_success(client, "Fallback configured");
}

static void command_metadata(client_t *client, source_t *source)
{
    char *action;
    char *value;
    mp3_state *state;
#ifdef USE_YP
    int i;
    time_t current_time;
#endif

    DEBUG0("Got metadata update request");

    COMMAND_REQUIRE(client, "mode", action);
    COMMAND_REQUIRE(client, "song", value);

    if(source->format->type != FORMAT_TYPE_MP3) {
        client_send_400(client, "Not mp3, cannot update metadata");
        return;
    }

    if(strcmp(action, "updinfo") != 0) {
        client_send_400(client, "No such action");
        return;
    }

    state = source->format->_state;

    thread_mutex_lock(&(state->lock));
    free(state->metadata);
    state->metadata = strdup(value);
    state->metadata_age++;
    state->metadata_raw = 0;
    thread_mutex_unlock(&(state->lock));

    DEBUG2("Metadata on mountpoint %s changed to \"%s\"", 
        source->mount, value);
    stats_event(source->mount, "title", value);
#ifdef USE_YP
    /* If we get an update on the mountpoint, force a
       yp touch */
    current_time = time(NULL);
    for (i=0; i<source->num_yp_directories; i++) {
        source->ypdata[i]->yp_last_touch = current_time - 
            source->ypdata[i]->yp_touch_interval + 2;
    }
#endif


    html_success(client, "Metadata update successful");
}

static void command_stats(client_t *client, int response) {
    xmlDocPtr doc;

    DEBUG0("Stats request, sending xml stats");

    stats_get_xml(&doc);
    admin_send_response(doc, client, response, STATS_TRANSFORMED_REQUEST);
    xmlFreeDoc(doc);
    client_destroy(client);
    return;
}

static void command_list_mounts(client_t *client, int response) {
    xmlDocPtr doc;
    avl_node *node;
    source_t *source;

    DEBUG0("List mounts request");


    if (response == PLAINTEXT)
    {
        node = avl_get_first(global.source_tree);
		html_write(client, 
            "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
        while(node) {
            source = (source_t *)node->key;
            html_write(client, "%s\n", source->mount);
            node = avl_get_next(node);
        }
    }
    else {

        doc = admin_build_sourcelist(NULL);

        admin_send_response(doc, client, response, 
            LISTMOUNTS_TRANSFORMED_REQUEST);
        xmlFreeDoc(doc);
    }
    client_destroy(client);

    return;
}

