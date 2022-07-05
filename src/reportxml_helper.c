/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * This file contains helper functions for report XML document parsing, manipulation, and rendering.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include "reportxml_helper.h"

#include "logging.h"
#define CATMODULE "reportxml-helper"

void reportxml_helper_add_value(reportxml_node_t *parent, const char *type, const char *member, const char *str)
{
    reportxml_node_t *value = reportxml_node_new(REPORTXML_NODE_TYPE_VALUE, NULL, NULL, NULL);
    reportxml_node_set_attribute(value, "type", type);
    if (member)
        reportxml_node_set_attribute(value, "member", member);
    if (str) {
        reportxml_node_set_attribute(value, "value", str);
    } else {
        reportxml_node_set_attribute(value, "state", "unset");
    }
    reportxml_node_add_child(parent, value);
    refobject_unref(value);
}

void reportxml_helper_add_value_int(reportxml_node_t *parent, const char *member, long long int val)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "%lli", val);
    reportxml_helper_add_value(parent, "int", member, buf);
}

void reportxml_helper_add_text(reportxml_node_t *parent, const char *definition, const char *text)
{
    reportxml_node_t *textnode = reportxml_node_new(REPORTXML_NODE_TYPE_TEXT, NULL, definition, NULL);
    reportxml_node_set_content(textnode, text);
    reportxml_node_add_child(parent, textnode);
    refobject_unref(textnode);
}

void reportxml_helper_add_reference(reportxml_node_t *parent, const char *type, const char *href)
{
    reportxml_node_t *referenenode = reportxml_node_new(REPORTXML_NODE_TYPE_REFERENCE, NULL, NULL, NULL);
    reportxml_node_set_attribute(referenenode, "type", type);
    reportxml_node_set_attribute(referenenode, "href", href);
    reportxml_node_add_child(parent, referenenode);
    refobject_unref(referenenode);
}

reportxml_node_t * reportxml_helper_add_incident(const char *state, const char *text, const char *docs, reportxml_database_t *db)
{
    reportxml_node_t *ret;
    reportxml_node_t *statenode;

    if (db && state) {
        ret = reportxml_database_build_fragment(db, state, -1, REPORTXML_NODE_TYPE_INCIDENT);
        if (ret) {
            statenode = reportxml_node_get_child_by_type(ret, REPORTXML_NODE_TYPE_STATE, 0);
            reportxml_node_set_attribute(statenode, "definition", state);
            refobject_unref(statenode);
            return ret;
        }
    }

    ret = reportxml_node_new(REPORTXML_NODE_TYPE_INCIDENT, NULL, NULL, NULL);
    statenode = reportxml_node_new(REPORTXML_NODE_TYPE_STATE, NULL, state, NULL);

    if (text)
        reportxml_helper_add_text(statenode, NULL, text);

    reportxml_node_add_child(ret, statenode);
    refobject_unref(statenode);

    if (docs)
        reportxml_helper_add_reference(ret, "documentation", docs);

    return ret;
}
