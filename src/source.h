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

#ifndef __SOURCE_H__
#define __SOURCE_H__

#include "cfgfile.h"
#include "yp.h"
#include "util.h"
#include "format.h"

#include <stdio.h>

struct auth_tag;

typedef struct source_tag
{
    client_t *client;
    connection_t *con;
    http_parser_t *parser;
    
    char *mount;

    /* If this source drops, try to move all clients to this fallback */
    char *fallback_mount;

    /* set to zero to request the source to shutdown without causing a global
     * shutdown */
    int running;

    struct _format_plugin_tag *format;

    client_t *active_clients;
    client_t *first_normal_client;

    int check_pending;
    client_t *pending_clients;
    client_t **pending_clients_tail;
    long new_listeners;

    rwlock_t *shutdown_rwlock;
    util_dict *audio_info;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

    long listeners;
    long max_listeners;
    int yp_public;
    int yp_prevent;
    struct auth_tag *authenticator;
    int fallback_override;
    int fallback_when_full;
    int no_mount;
    int shoutcast_compat;

    /* per source burst handling for connecting clients */
    unsigned int burst_size;    /* trigger level for burst on connect */
    unsigned int burst_offset; 
    refbuf_t *burst_point;

    unsigned int queue_size;
    unsigned int queue_size_limit;

    unsigned timeout;  /* source timeout in seconds */
    int on_demand;
    int on_demand_req;
    int hidden;
    int file_only;
    int recheck_settings;

    time_t last_read;
    char *on_connect;
    char *on_disconnect;

    mutex_t lock;

    refbuf_t *stream_data;
    refbuf_t *stream_data_tail;

    FILE *intro_file;

} source_t;

source_t *source_reserve (const char *mount);
void *source_client_thread (void *arg);
void source_update_settings (ice_config_t *config, source_t *source);
void source_update (ice_config_t *config);
void source_clear_source (source_t *source);
source_t *source_find_mount(const char *mount);
source_t *source_find_mount_raw(const char *mount);
client_t *source_find_client(source_t *source, int id);
int source_compare_sources(void *arg, void *a, void *b);
void source_free_source(source_t *source);
void source_move_clients (source_t *source, source_t *dest);
int source_remove_client(void *key);
void source_main(source_t *source);
void add_client (char *mount, client_t *client);
int add_authenticated_client (source_t *source, client_t *client);
int source_free_client (source_t *source, client_t *client);
void source_recheck_mounts (void);

extern mutex_t move_clients_mutex;

#endif


