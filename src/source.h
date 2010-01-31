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
#include "fserve.h"

#include <stdio.h>

typedef struct source_tag
{
    char *mount;
    unsigned int flags;
    int listener_send_trigger;

    client_t *client;
    http_parser_t *parser;
    time_t client_stats_update;

    struct _format_plugin_tag *format;

    avl_tree *clients;

    util_dict *audio_info;

    /* name of a file, whose contents are sent at listener connection */
    FILE *intro_file;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

    fbinfo fallback;

    int skip_duration;
    long limit_rate;
    time_t wait_time;

    unsigned long termination_count;
    unsigned long peak_listeners;
    unsigned long listeners;
    unsigned long prev_listeners;

    int yp_public;

    /* per source burst handling for connecting clients */
    unsigned int burst_size;    /* trigger level for burst on connect */
    unsigned int burst_offset; 
    refbuf_t *burst_point;

    unsigned int queue_size;
    unsigned int queue_size_limit;

    unsigned timeout;  /* source timeout in seconds */
    unsigned long bytes_sent_since_update;
    unsigned long bytes_read_since_update;
    int stats_interval;

    time_t last_read;

    mutex_t lock;

    refbuf_t *stream_data;
    refbuf_t *stream_data_tail;

} source_t;

#define SOURCE_RUNNING              01
#define SOURCE_ON_DEMAND            02
#define SOURCE_PAUSE_LISTENERS      04
#define SOURCE_SHOUTCAST_COMPAT     010
#define SOURCE_TERMINATING          020
#define SOURCE_TEMPORARY_FALLBACK   040

#define source_available(x)     (((x)->flags & (SOURCE_RUNNING|SOURCE_ON_DEMAND)) && (x)->fallback.mount == NULL)
#define source_running(x)       ((x)->flags & SOURCE_RUNNING)

source_t *source_reserve (const char *mount);
void *source_client_thread (void *arg);
int  source_startup (client_t *client, const char *uri);
void source_update_settings (ice_config_t *config, source_t *source, mount_proxy *mountinfo);
void source_clear_listeners (source_t *source);
void source_clear_source (source_t *source);
source_t *source_find_mount(const char *mount);
source_t *source_find_mount_raw(const char *mount);
client_t *source_find_client(source_t *source, int id);
int source_compare_sources(void *arg, void *a, void *b);
void source_free_source(source_t *source);
void source_main(source_t *source);
void source_recheck_mounts (int update_all);
int  source_add_listener (const char *mount, mount_proxy *mountinfo, client_t *client);
int  source_read (source_t *source);
void source_setup_listener (source_t *source, client_t *client);
void source_init (source_t *source);
void source_shutdown (source_t *source, int with_fallback);
void source_set_fallback (source_t *source, const char *dest_mount);
void source_set_override (const char *mount, const char *dest);

#define SOURCE_BLOCK_SYNC           01
#define SOURCE_BLOCK_RELEASE        02
#define SOURCE_QUEUE_BLOCK          04

#endif


