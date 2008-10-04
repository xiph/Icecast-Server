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

#include "cfgfile.h"
#include "connection.h"
#include "httpp/httpp.h"
#include "client.h"
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define STATS_HIDDEN   1
#define STATS_SLAVE    2
#define STATS_GENERAL  4
#define STATS_COUNTERS 8
#define STATS_PUBLIC   (STATS_GENERAL|STATS_COUNTERS)
#define STATS_ALL      ~0

void stats_initialize(void);
void stats_shutdown(void);

void stats_global(ice_config_t *config);
void stats_get_streamlist (char *buffer, size_t remaining);
refbuf_t *stats_get_streams (int prepend);
void stats_clear_virtual_mounts (void);

void stats_event(const char *source, const char *name, const char *value);
void stats_event_conv(const char *mount, const char *name,
        const char *value, const char *charset);
void stats_event_args(const char *source, char *name, char *format, ...);
void stats_event_inc(const char *source, const char *name);
void stats_event_add(const char *source, const char *name, unsigned long value);
void stats_event_sub(const char *source, const char *name, unsigned long value);
void stats_event_dec(const char *source, const char *name);
void stats_event_hidden (const char *source, const char *name, const char *value, int hidden);
void stats_event_time (const char *mount, const char *name);

void *stats_connection(void *arg);
void stats_callback (client_t *client, void *mount);
void stats_global_calc(void);

void stats_transform_xslt(client_t *client, const char *uri);
void stats_sendxml(client_t *client);
void stats_get_xml(xmlDocPtr *doc, int show_hidden, const char *show_mount);
char *stats_get_value(const char *source, const char *name);

#endif  /* __STATS_H__ */

