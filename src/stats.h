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
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __STATS_H__
#define __STATS_H__

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "icecasttypes.h"
#include "refbuf.h"

#define STATS_XML_FLAG_NONE             0x0000U
#define STATS_XML_FLAG_SHOW_HIDDEN      0x0001U
#define STATS_XML_FLAG_SHOW_LISTENERS   0x0002U

typedef struct _stats_node_tag
{
    char *name;
    char *value;
    int hidden;
} stats_node_t;

typedef struct _stats_event_tag
{
    char *source;
    char *name;
    char *value;
    int  hidden;
    int  action;

    struct _stats_event_tag *next;
} stats_event_t;

typedef struct _stats_source_tag
{
    char *source;
    int  hidden;
    avl_tree *stats_tree;
} stats_source_t;

typedef struct _stats_tag
{
    avl_tree *global_tree;

    /* global stats
    start_time
    total_users
    max_users
    total_sources
    max_sources
    total_user_connections
    total_source_connections
    */

    avl_tree *source_tree;

    /* stats by source, and for stats
    start_time
    total_users
    max_users
    */

} stats_t;

void stats_initialize(void);
void stats_shutdown(void);

void stats_global(ice_config_t *config);
stats_t *stats_get_stats(void);
refbuf_t *stats_get_streams (void);
void stats_clear_virtual_mounts (void);

void stats_event(const char *source, const char *name, const char *value);
void stats_event_conv(const char *mount, const char *name,
        const char *value, const char *charset);
void stats_event_args(const char *source, char *name, char *format, ...);
void stats_event_inc(const char *source, const char *name);
void stats_event_add(const char *source, const char *name, unsigned long value);
void stats_event_sub(const char *source, const char *name, unsigned long value);
void stats_event_dec(const char *source, const char *name);
void stats_event_hidden (const char *source, const char *name, int hidden);
void stats_event_time (const char *mount, const char *name);
void stats_event_time_iso8601 (const char *mount, const char *name);

void *stats_connection(void *arg);
void stats_callback (client_t *client, void *notused);

void stats_transform_xslt(client_t *client);
void stats_sendxml(client_t *client);
xmlDocPtr stats_get_xml(unsigned int flags, const char *show_mount, client_t *client);
char *stats_get_value(const char *source, const char *name);

#endif  /* __STATS_H__ */

