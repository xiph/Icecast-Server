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

#ifndef __STATS_H__
#define __STATS_H__

#include "connection.h"
#include "httpp/httpp.h"
#include "client.h"
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>


typedef struct _stats_connection_tag
{
    connection_t *con;
    http_parser_t *parser;
} stats_connection_t;

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

void stats_initialize();
void stats_shutdown();

stats_t *stats_get_stats();

void stats_event(const char *source, const char *name, const char *value);
void stats_event_args(const char *source, char *name, char *format, ...);
void stats_event_inc(char *source, char *name);
void stats_event_add(char *source, char *name, unsigned long value);
void stats_event_dec(char *source, char *name);

void *stats_connection(void *arg);
void *stats_callback(void *arg);

void stats_transform_xslt(client_t *client, char *xslpath);
void stats_sendxml(client_t *client);
void stats_get_xml(xmlDocPtr *doc);
char *stats_get_value(char *source, char *name);

#endif  /* __STATS_H__ */





