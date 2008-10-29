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

typedef struct source_tag
{
    client_t *client;
    http_parser_t *parser;
    time_t client_stats_update;
    
    char *mount;

    /* set to zero to request the source to shutdown without causing a global
     * shutdown */
    int running;

    struct _format_plugin_tag *format;

    client_t *active_clients;
    client_t **fast_clients_p;

    rwlock_t *shutdown_rwlock;
    util_dict *audio_info;

    /* name of a file, whose contents are sent at listener connection */
    FILE *intro_file;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

    int throttle_stream;
    time_t throttle_termination;
    uint64_t limit_rate;
    int avg_bitrate_duration;
    time_t wait_time;
    long listener_send_trigger;

    unsigned long peak_listeners;
    unsigned long listeners;
    unsigned long prev_listeners;

    int yp_public;
    int shoutcast_compat;
    int log_id;

    /* per source burst handling for connecting clients */
    unsigned int burst_size;    /* trigger level for burst on connect */
    unsigned int burst_offset; 
    refbuf_t *burst_point;

    unsigned int queue_size;
    unsigned int queue_size_limit;
    unsigned int amount_added_to_queue;

    unsigned timeout;  /* source timeout in seconds */
    int on_demand;
    int on_demand_req;
    unsigned long bytes_sent_since_update;
    unsigned long bytes_read_since_update;
    int stats_interval;

    time_t last_read;

    mutex_t lock;

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


