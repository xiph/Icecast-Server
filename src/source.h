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
#include "thread/thread.h"

#include <stdio.h>

typedef struct source_tag
{
    mutex_t lock;
    client_t *client;
    connection_t *con;
    http_parser_t *parser;
    time_t client_stats_update;
    
    char *mount;

    /* If this source drops, try to move all clients to this fallback */
    char *fallback_mount;

    /* set to zero to request the source to shutdown without causing a global
     * shutdown */
    int running;

    struct _format_plugin_tag *format;

    avl_tree *client_tree;
    avl_tree *pending_tree;

    rwlock_t *shutdown_rwlock;
    util_dict *audio_info;

    FILE *intro_file;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

    unsigned long peak_listeners;
    unsigned long listeners;
    unsigned long prev_listeners;
    long max_listeners;
    int yp_public;
    int fallback_override;
    int fallback_when_full;
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
    time_t last_read;
    int short_delay;

    refbuf_t *stream_data;
    refbuf_t *stream_data_tail;

} source_t;

source_t *source_reserve (const char *mount);
void *source_client_thread (void *arg);
void source_startup (client_t *client, const char *uri, int auth_style);
void source_client_callback (client_t *client, void *source);
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo);
void source_clear_source (source_t *source);
source_t *source_find_mount(const char *mount);
source_t *source_find_mount_raw(const char *mount);
client_t *source_find_client(source_t *source, int id);
int source_compare_sources(void *arg, void *a, void *b);
void source_free_source(source_t *source);
void source_move_clients (source_t *source, source_t *dest);
int source_remove_client(void *key);
void source_main(source_t *source);
void source_recheck_mounts (int update_all);

extern mutex_t move_clients_mutex;

#endif


