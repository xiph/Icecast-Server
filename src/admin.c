#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "config.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "source.h"
#include "global.h"
#include "event.h"
#include "stats.h"

#include "format.h"
#include "format_mp3.h"

#include "logging.h"

#define CATMODULE "admin"

#define COMMAND_ERROR             (-1)

/* Mount-specific commands */
#define COMMAND_FALLBACK            1
#define COMMAND_METADATA_UPDATE     2
#define COMMAND_SHOW_LISTENERS      3

/* Global commands */
#define COMMAND_LIST_MOUNTS       101
#define COMMAND_RAW_STATS         102
#define COMMAND_RAW_LISTSTREAM    103

int admin_get_command(char *command)
{
    if(!strcmp(command, "fallbacks"))
        return COMMAND_FALLBACK;
    else if(!strcmp(command, "metadata"))
        return COMMAND_METADATA_UPDATE;
    else if(!strcmp(command, "listclients"))
        return COMMAND_SHOW_LISTENERS;
    else if(!strcmp(command, "rawstats"))
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, "stats.xml")) /* The old way */
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, "listmounts"))
        return COMMAND_LIST_MOUNTS;
    else if(!strcmp(command, "streamlist"))
        return COMMAND_RAW_LISTSTREAM;
    else
        return COMMAND_ERROR;
}

static void command_fallback(client_t *client, source_t *source);
static void command_metadata(client_t *client, source_t *source);
static void command_show_listeners(client_t *client, source_t *source);

static void command_raw_stats(client_t *client);
static void command_list_mounts(client_t *client, int formatted);

static void admin_handle_mount_request(client_t *client, source_t *source,
        int command);
static void admin_handle_general_request(client_t *client, int command);

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
        source = source_find_mount(mount);
        avl_tree_unlock(global.source_tree);

        if(source == NULL) {
            WARN2("Admin command %s on non-existent source %s", 
                    command_string, mount);
            client_send_400(client, "Source does not exist");
            return;
        }

        INFO2("Received admin command %s on mount \"%s\"", 
                command_string, mount);

        admin_handle_mount_request(client, source, command);
    }
    else {

        if(!connection_check_admin_pass(client->parser)) {
		    INFO1("Bad or missing password on admin command "
                  "request (command: %s)", command_string);
            client_send_401(client);
            return;
        }
        
        admin_handle_general_request(client, command);
    }
}

static void admin_handle_general_request(client_t *client, int command)
{
    switch(command) {
        case COMMAND_RAW_STATS:
            command_raw_stats(client);
            break;
        case COMMAND_LIST_MOUNTS:
            command_list_mounts(client, 1);
            break;
        case COMMAND_RAW_LISTSTREAM:
            command_list_mounts(client, 0);
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
        case COMMAND_FALLBACK:
            command_fallback(client, source);
            break;
        case COMMAND_METADATA_UPDATE:
            command_metadata(client, source);
            break;
        case COMMAND_SHOW_LISTENERS:
            command_show_listeners(client, source);
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

static void html_head(client_t *client)
{
    int bytes;

    client->respcode = 200;
    bytes = sock_write(client->con->sock,
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<html><head><title>Admin request</title></head>"
            "<body>");
    if(bytes > 0) client->con->sent_bytes = bytes;
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

static void command_show_listeners(client_t *client, source_t *source)
{
    avl_node *client_node;
    client_t *current;
    time_t now = time(NULL);

    DEBUG1("Dumping listeners on mountpoint %s", source->mount);

    html_head(client);

    html_write(client, 
            "<table><tr><td>IP</td><td>Connected</td><td>ID</td></tr>");

    avl_tree_rlock(source->client_tree);

    client_node = avl_get_first(source->client_tree);
    while(client_node) {
        current = (client_t *)client_node->key;

        html_write(client, "<tr><td>%s</td><td>%d</td><td>%ld</td></tr>",
                current->con->ip, now-current->con->con_time, current->con->id);

        client_node = avl_get_next(client_node);
    }

    avl_tree_unlock(source->client_tree);

    html_write(client, "</table></body></html>");

    client_destroy(client);
}

static void command_fallback(client_t *client, source_t *source)
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

    DEBUG2("Metadata on mountpoint %s changed to \"%s\"", source->mount, value);
    stats_event(source->mount, "title", value);

    html_success(client, "Metadata update successful");
}

static void command_raw_stats(client_t *client) {
    DEBUG0("Stats request, sending xml stats");

    stats_sendxml(client);
    client_destroy(client);
    return;
}

static void command_list_mounts(client_t *client, int formatted) {
    avl_node *node;
    source_t *source;
    int bytes;

    DEBUG0("List mounts request");

    if(formatted) {
        html_head(client);

        html_write(client, 
                "<table><tr><td>Mountpoint</td><td>Fallback</td>"
                "<td>Format</td><td>Listeners</td></tr>");
    }
    else {
        client->respcode = 200;
        bytes = sock_write(client->con->sock,
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n");
        if(bytes > 0) client->con->sent_bytes = bytes;
    }

    avl_tree_rlock(global.source_tree);

    node = avl_get_first(global.source_tree);
    while(node) {
        source = (source_t *)node->key;

        if(formatted) {
            html_write(client, 
                    "<tr><td>%s</td><td>%s</td><td>%s</td><td>%ld</td></tr>",
                    source->mount, (source->fallback_mount != NULL)?
                    source->fallback_mount:"", 
                    source->format->format_description, source->listeners);
        }
        else {
            bytes = sock_write(client->con->sock, "%s\r\n", source->mount);
            if(bytes > 0) client->con->sent_bytes += bytes;
        }

        node = avl_get_next(node);
    }

    avl_tree_unlock(global.source_tree);

    if(formatted) 
        html_write(client, "</table></body></html>");

    client_destroy(client);
    return;
}

