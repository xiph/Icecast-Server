/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* slave.c
 * by Ciaran Anscomb <ciaran.anscomb@6809.org.uk>
 *
 * Periodically requests a list of streams from a master server
 * and creates source threads for any it doesn't already have.
 * */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

#include "os.h"

#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "httpp/httpp.h"

#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "source.h"
#include "format.h"

#define CATMODULE "slave"

static void *_slave_thread(void *arg);
thread_type *_slave_thread_id;
static int _initialized = 0;

void slave_initialize(void) {
    ice_config_t *config;
    if (_initialized) return;

    config = config_get_config();
    /* Don't create a slave thread if it isn't configured */
    if (config->master_server == NULL && 
            config->relay == NULL)
    {
        config_release_config();
        return;
    }
    config_release_config();

    _initialized = 1;
    _slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}

void slave_shutdown(void) {
    if (!_initialized) return;
    _initialized = 0;
    thread_join(_slave_thread_id);
}

static void create_relay_stream(char *server, int port, 
        char *remotemount, char *localmount, int mp3)
{
    sock_t streamsock;
    char header[4096];
    connection_t *con;
    http_parser_t *parser;
    client_t *client;

    if(!localmount)
        localmount = remotemount;

    DEBUG1("Adding source at mountpoint \"%s\"", localmount);

    streamsock = sock_connect_wto(server, port, 0);
    if (streamsock == SOCK_ERROR) {
        WARN2("Failed to relay stream from master server, couldn't connect to http://%s:%d", server, port);
        return;
    }
    con = create_connection(streamsock, -1, NULL);
    /* At this point we may not know if we are relaying a mp3 or vorbis stream,
     * so lets send in the icy-metadata header just in case, it's harmless in 
     * the vorbis case. If we don't send in this header then relay will not 
     * have mp3 metadata.
     */
    sock_write(streamsock, "GET %s HTTP/1.0\r\n"
                           "User-Agent: " ICECAST_VERSION_STRING "\r\n"
                           "Icy-MetaData: 1\r\n"
                           "\r\n", 
                           remotemount);
    memset(header, 0, sizeof(header));
    if (util_read_header(con->sock, header, 4096) == 0) {
        WARN0("Header read failed");
        connection_close(con);
        return;
    }
    parser = httpp_create_parser();
    httpp_initialize(parser, NULL);
    if(!httpp_parse_response(parser, header, strlen(header), localmount)) {
        if(httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE)) {
            ERROR1("Error parsing relay request: %s", 
                    httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
        }
        else
            ERROR0("Error parsing relay request");
        connection_close(con);
        httpp_destroy(parser);
        return;
    }

    client = client_create(con, parser);
    if (!connection_create_source(client, con, parser, 
                httpp_getvar(parser, HTTPP_VAR_URI))) {
        DEBUG0("Failed to create source");
        client_destroy(client);
    }

    return;
}

static void *_slave_thread(void *arg) {
    sock_t mastersock;
    char buf[256];
    int interval;
    char *authheader, *data;
    int len;
    char *username = "relay";
    int max_interval;
    relay_server *relay;
    ice_config_t *config;
    
    config = config_get_config();

    interval = max_interval = config->master_update_interval;

    config_release_config();

    while (_initialized) {
        if (max_interval > ++interval) {
            thread_sleep(1000000);
            continue;
        }
        else {
            /* In case it's been reconfigured */
            config = config_get_config();
            max_interval = config->master_update_interval;

            interval = 0;
        }

        if(config->master_server != NULL) {
            char *server = strdup (config->master_server);
            int port = config->master_server_port;
            char *password = NULL;
            if (config->master_password != NULL)
                password = strdup (config->master_password);
            else
                password = strdup (config->source_password);
            config_release_config();

            mastersock = sock_connect_wto(server, port, 0);

            if (mastersock == SOCK_ERROR) {
                WARN0("Relay slave failed to contact master server to fetch stream list");
                free (server);
                free (password);
                continue;
            }

            len = strlen(username) + strlen(password) + 1;
            authheader = malloc(len+1);
            strcpy(authheader, username);
            strcat(authheader, ":");
            strcat(authheader, password);
            data = util_base64_encode(authheader);
            sock_write(mastersock, 
                    "GET /admin/streamlist.txt HTTP/1.0\r\n"
                    "Authorization: Basic %s\r\n"
                    "\r\n", data);
            free(authheader);
            free(data);
            while (sock_read_line(mastersock, buf, sizeof(buf))) {
                if(!strlen(buf))
                    break;
            }

            while (sock_read_line(mastersock, buf, sizeof(buf))) {
                avl_tree_rlock(global.source_tree);
                if (!source_find_mount(buf)) {
                    avl_tree_unlock(global.source_tree);

                    create_relay_stream(server, port, buf, NULL, 0);
                } 
                else
                    avl_tree_unlock(global.source_tree);
            }
            free (server);
            free (password);
            sock_close(mastersock);
        }
        else {
            config_release_config();
        }

        /* And now, we process the individual mounts... */
        config = config_get_config();
        relay = config->relay;
        thread_mutex_lock(&(config_locks()->relay_lock));
        config_release_config();

        while(relay) {
            avl_tree_rlock(global.source_tree);
            if(!source_find_mount_raw(relay->localmount)) {
                avl_tree_unlock(global.source_tree);

                create_relay_stream(relay->server, relay->port, relay->mount,
                        relay->localmount, relay->mp3metadata);
            }
            else
                avl_tree_unlock(global.source_tree);
            relay = relay->next;
        }

        thread_mutex_unlock(&(config_locks()->relay_lock));
    }
    thread_exit(0);
    return NULL;
}

