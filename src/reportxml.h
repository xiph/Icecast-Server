/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __REPORTXML_H__
#define __REPORTXML_H__

#include <libxml/tree.h>

#include "icecasttypes.h"
#include "compat.h"

typedef enum {
    REPORTXML_NODE_TYPE__ERROR,
    REPORTXML_NODE_TYPE_REPORT,
    REPORTXML_NODE_TYPE_DEFINITION,
    REPORTXML_NODE_TYPE_INCIDENT,
    REPORTXML_NODE_TYPE_STATE,
    REPORTXML_NODE_TYPE_BACKTRACE,
    REPORTXML_NODE_TYPE_POSITION,
    REPORTXML_NODE_TYPE_MORE,
    REPORTXML_NODE_TYPE_FIX,
    REPORTXML_NODE_TYPE_ACTION,
    REPORTXML_NODE_TYPE_REASON,
    REPORTXML_NODE_TYPE_TEXT,
    REPORTXML_NODE_TYPE_TIMESTAMP,
    REPORTXML_NODE_TYPE_RESOURCE,
    REPORTXML_NODE_TYPE_VALUE,
    REPORTXML_NODE_TYPE_REFERENCE,
    REPORTXML_NODE_TYPE_EXTENSION
} reportxml_node_type_t;

reportxml_t *           reportxml_new(void);
reportxml_node_t *      reportxml_get_root_node(reportxml_t *report);
reportxml_node_t *      reportxml_get_node_by_attribute(reportxml_t *report, const char *key, const char *value, int include_definitions);
reportxml_t *           reportxml_parse_xmldoc(xmlDocPtr doc);
xmlDocPtr               reportxml_render_xmldoc(reportxml_t *report);

reportxml_node_t *      reportxml_node_new(reportxml_node_type_t type, const char *id, const char *definition, const char *akindof);
reportxml_node_t *      reportxml_node_parse_xmlnode(xmlNodePtr xmlnode);
reportxml_node_t *      reportxml_node_copy(reportxml_node_t *node);
xmlNodePtr              reportxml_node_render_xmlnode(reportxml_node_t *node);
reportxml_node_type_t   reportxml_node_get_type(reportxml_node_t *node);
int                     reportxml_node_set_attribute(reportxml_node_t *node, const char *key, const char *value);
char *                  reportxml_node_get_attribute(reportxml_node_t *node, const char *key);
int                     reportxml_node_add_child(reportxml_node_t *node, reportxml_node_t *child);
ssize_t                 reportxml_node_count_child(reportxml_node_t *node);
reportxml_node_t *      reportxml_node_get_child(reportxml_node_t *node, size_t idx);
reportxml_node_t *      reportxml_node_get_child_by_attribute(reportxml_node_t *node, const char *key, const char *value, int include_definitions);
int                     reportxml_node_set_content(reportxml_node_t *node, const char *value);
char *                  reportxml_node_get_content(reportxml_node_t *node);
int                     reportxml_node_add_xml_child(reportxml_node_t *node, xmlNodePtr child);
ssize_t                 reportxml_node_count_xml_child(reportxml_node_t *node);
xmlNodePtr              reportxml_node_get_xml_child(reportxml_node_t *node, size_t idx);

reportxml_database_t *  reportxml_database_new(void);
int                     reportxml_database_add_report(reportxml_database_t *db, reportxml_t *report);
reportxml_node_t *      reportxml_database_build_node(reportxml_database_t *db, const char *id, ssize_t depth);
reportxml_t *           reportxml_database_build_report(reportxml_database_t *db, const char *id, ssize_t depth);

#endif
