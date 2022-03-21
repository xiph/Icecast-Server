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
 * Copyright 2012-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __SOURCE_H__
#define __SOURCE_H__

#include <stdio.h>
#include <stdbool.h>

#include "common/thread/thread.h"
#include "common/httpp/httpp.h"

#include "icecasttypes.h"
#include "connection.h"
#include "yp.h"
#include "util.h"
#include "format.h"
#include "playlist.h"

struct source_tag {
    mutex_t lock;
    client_t *client;
    connection_t *con;
    http_parser_t *parser;
    time_t client_stats_update;
    
    char *mount; // TODO: Should we at some point migrate away from this to only use identifier?
    mount_identifier_t *identifier;

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
    fallback_override_t fallback_override;
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
    bool allow_direct_access; // copy of mount_proxy->allow_direct_access
    time_t last_read;
    int short_delay;

    refbuf_t *stream_data;
    refbuf_t *stream_data_tail;

    playlist_t *history;
};

source_t *source_reserve (const char *mount);
void *source_client_thread (void *arg);
void source_client_callback (client_t *client, void *source);
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo);
void source_clear_source (source_t *source);
#define source_find_mount(mount) source_find_mount_with_history((mount), NULL)
source_t *source_find_mount_with_history(const char *mount, navigation_history_t *history);
source_t *source_find_mount_raw(const char *mount);
client_t *source_find_client(source_t *source, connection_id_t id);
int source_compare_sources(void *arg, void *a, void *b);
void source_free_source(source_t *source);
void source_move_clients(source_t *source, source_t *dest, connection_id_t *id, navigation_direction_t direction);
int source_remove_client(void *key);
void source_main(source_t *source);
void source_recheck_mounts (int update_all);

/* Writes a buffer of raw data to a dumpfile. returns true if the write was successful (and complete). */
bool source_write_dumpfile(source_t *source, const void *buffer, size_t len);

extern mutex_t move_clients_mutex;

#endif
