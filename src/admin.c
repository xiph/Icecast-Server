#include <string.h>
#include <stdlib.h>

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

#define COMMAND_ERROR           (-1)
#define COMMAND_FALLBACK          1
#define COMMAND_RAW_STATS         2
#define COMMAND_METADATA_UPDATE   3

int admin_get_command(char *command)
{
    if(!strcmp(command, "fallbacks"))
        return COMMAND_FALLBACK;
    else if(!strcmp(command, "rawstats"))
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, "stats.xml")) /* The old way */
        return COMMAND_RAW_STATS;
    else if(!strcmp(command, "metadata"))
        return COMMAND_METADATA_UPDATE;
    else
        return COMMAND_ERROR;
}

static void command_fallback(client_t *client, source_t *source);
static void command_metadata(client_t *client, source_t *source);

static void command_raw_stats(client_t *client);

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
        if(!connection_check_source_pass(client->parser, mount)) {
		    INFO1("Bad or missing password on mount modification admin "
                  "request (command: %s)", command_string);
            client_send_401(client);
            return;
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
        default:
            WARN0("Mount request not recognised");
            client_send_400(client, "Mount request unknown");
            return;
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

static void command_success(client_t *client, char *message)
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

static void command_fallback(client_t *client, source_t *source)
{
    char *fallback;
    char *old;

    DEBUG0("Got fallback request");

    COMMAND_REQUIRE(client, "fallback", fallback);

    old = source->fallback_mount;
    source->fallback_mount = strdup(fallback);
    free(old);

    command_success(client, "Fallback configured");
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

    command_success(client, "Metadata update successful");
}

static void command_raw_stats(client_t *client) {
    DEBUG0("Stats request, sending xml stats");

    stats_sendxml(client);
    client_destroy(client);
    return;
}

