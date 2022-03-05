/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* This file contains helper functions for report XML document parsing, manipulation, and rendering. */

#ifndef __REPORTXML_HELPER_H__
#define __REPORTXML_HELPER_H__

#include "reportxml.h"

void reportxml_helper_add_value(reportxml_node_t *parent, const char *type, const char *member, const char *str);

#define reportxml_helper_add_value_string(parent,member,str) reportxml_helper_add_value((parent), "string", (member), (str))
#define reportxml_helper_add_value_enum(parent,member,str) reportxml_helper_add_value((parent), "enum", (member), (str))
#define reportxml_helper_add_value_boolean(parent,member,value) reportxml_helper_add_value((parent), "boolean", (member), (value) ? "true" : "false")
void reportxml_helper_add_value_int(reportxml_node_t *parent, const char *member, long long int val);

void reportxml_helper_add_text(reportxml_node_t *parent, const char *definition, const char *text);

void reportxml_helper_add_reference(reportxml_node_t *parent, const char *type, const char *href);

reportxml_node_t * reportxml_helper_add_incident(const char *state, const char *text, const char *docs, reportxml_database_t *db);

#endif
