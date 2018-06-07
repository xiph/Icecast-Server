/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Special fast event functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "reportxml.h"
#include "refobject.h"

/* For XMLSTR() */
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "reportxml"

struct reportxml_tag {
    refobject_base_t __base;
    reportxml_node_t *root;
};

struct reportxml_node_tag {
    refobject_base_t __base;
    xmlNodePtr xmlnode;
    reportxml_node_type_t type;
    reportxml_node_t **childs;
    size_t childs_len;
    char *content;
};

struct nodeattr;

struct nodeattr {
    const char *name;
    const char *type;
    const char *def;
    int required;
    int (*checker)(const struct nodeattr *attr, const char *str);
    const char *values[32];
};

struct nodedef {
    reportxml_node_type_t type;
    const char *name;
    int has_content;
    const struct nodeattr *attr[12];
};

static const struct nodeattr __attr__eol[1]             = {{NULL,           NULL,           NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_version[1]          = {{"version",      "CDATA",        "0.0.1",  1,  NULL, {"0.0.1", NULL}}};
static const struct nodeattr __attr_xmlns[1]            = {{"xmlns",        "URI",          "xxx",    1,  NULL, {"xxx", NULL}}};
static const struct nodeattr __attr_id[1]               = {{"id",           "ID",           NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_definition[1]       = {{"definition",   "UUID",         NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_akindof[1]          = {{"akindof",      "UUIDs",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_lang[1]             = {{"lang",         "LanguageCode", NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_dir[1]              = {{"dir",          NULL,           NULL,     0,  NULL, {"ltr", "rtl", NULL}}};
static const struct nodeattr __attr_template[1]         = {{"template",     "UUID",         NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_defines[1]          = {{"defines",      "UUID",         NULL,     1,  NULL, {NULL}}};
static const struct nodeattr __attr_function[1]         = {{"function",     "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_filename[1]         = {{"filename",     "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_line[1]             = {{"line",         "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_binary[1]           = {{"binary",       "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_offset[1]           = {{"offset",       "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_absolute[1]         = {{"absolute",     "iso8601",      NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_relative[1]         = {{"relative",     "iso8601",      NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_name[1]             = {{"name",         "CDATA",        NULL,     1,  NULL, {NULL}}};
static const struct nodeattr __attr_member[1]           = {{"member",       "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_value[1]            = {{"value",        "CDATA",        NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_state[1]            = {{"state",        NULL,           "set",    1,  NULL, {"declared", "set", "uninitialized", "missing", NULL}}};
static const struct nodeattr __attr_href[1]             = {{"href",         "URI",          NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr__action_type[1]     = {{"type",         NULL,           NULL,     1,  NULL, {"retry", "choice", "see-other", "authenticate", "pay", "change-protocol", "slow-down", "ask-user", "ask-admin", "bug", NULL}}};
static const struct nodeattr __attr__resource_type[1]   = {{"type",         NULL,           NULL,     1,  NULL, {"actor", "manipulation-target", "helper", "related", "result", "parameter", NULL}}};
static const struct nodeattr __attr__value_type[1]      = {{"type",         NULL,           NULL,     1,  NULL, {"null", "int", "float", "uuid", "string", "structure", "uri", "pointer", "version", "protocol", "username", "password", NULL}}};
static const struct nodeattr __attr__reference_type[1]  = {{"type",         NULL,           NULL,     1,  NULL, {"documentation", "log", "report", "related", NULL}}};

/* Helper:
 * grep '^ *REPORTXML_NODE_TYPE_' reportxml.h | sed 's/^\( *REPORTXML_NODE_TYPE_\([^,]*\)\),*$/\1 \2/;' | while read l s; do c=$(tr A-Z a-z <<<"$s"); printf "    {%-32s \"%-16s 0, {__attr__eol}},\n" "$l," "$c\","; done
 */
#define __BASIC_ELEMENT __attr_id, __attr_definition, __attr_akindof
static const struct nodedef __nodedef[] = {
    {REPORTXML_NODE_TYPE_REPORT,      "report",         0, {__attr_id, __attr_version, __attr_xmlns, __attr__eol}},
    {REPORTXML_NODE_TYPE_DEFINITION,  "definition",     0, {__BASIC_ELEMENT, __attr_template, __attr_defines, __attr__eol}},
    {REPORTXML_NODE_TYPE_INCIDENT,    "incident",       0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_STATE,       "state",          0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_BACKTRACE,   "backtrace",      0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_POSITION,    "position",       0, {__BASIC_ELEMENT, __attr_function, __attr_filename, __attr_line, __attr_binary, __attr_offset, __attr__eol}},
    {REPORTXML_NODE_TYPE_MORE,        "more",           0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_FIX,         "fix",            0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_ACTION,      "action",         0, {__BASIC_ELEMENT, __attr__action_type, __attr__eol}},
    {REPORTXML_NODE_TYPE_REASON,      "reason",         0, {__BASIC_ELEMENT, __attr__eol}},
    {REPORTXML_NODE_TYPE_TEXT,        "text",           1, {__BASIC_ELEMENT, __attr_lang, __attr_dir, __attr__eol}},
    {REPORTXML_NODE_TYPE_TIMESTAMP,   "timestamp",      0, {__BASIC_ELEMENT, __attr_absolute, __attr_relative, __attr__eol}},
    {REPORTXML_NODE_TYPE_RESOURCE,    "resource",       0, {__BASIC_ELEMENT, __attr__resource_type, __attr_name, __attr__eol}},
    {REPORTXML_NODE_TYPE_VALUE,       "value",          0, {__BASIC_ELEMENT, __attr_member, __attr_value, __attr_state, __attr__value_type, __attr__eol}},
    {REPORTXML_NODE_TYPE_REFERENCE,   "reference",      0, {__BASIC_ELEMENT, __attr__reference_type, __attr_href, __attr__eol}},
    {REPORTXML_NODE_TYPE_EXTENSION,   "extension",      0, {__BASIC_ELEMENT, __attr__eol}},
};

static const struct nodedef * __get_nodedef(reportxml_node_type_t type)
{
    size_t i;

    for (i = 0; i < (sizeof(__nodedef)/sizeof(*__nodedef)); i++) {
        if (__nodedef[i].type == type) {
            return &(__nodedef[i]);
        }
    }

    return NULL;
}

static const struct nodedef * __get_nodedef_by_name(const char *name)
{
    size_t i;

    for (i = 0; i < (sizeof(__nodedef)/sizeof(*__nodedef)); i++) {
        if (strcmp(__nodedef[i].name, name) == 0) {
            return &(__nodedef[i]);
        }
    }

    return NULL;
}

static const struct nodeattr * __get_nodeattr(const struct nodedef * nodedef, const char *key)
{
    size_t i;

    if (!nodedef || !key)
        return NULL;

    for (i = 0; i < (sizeof(nodedef->attr)/sizeof(*nodedef->attr)); i++) {
        if (nodedef->attr[i]->name == NULL) {
            /* we reached the end of the list */
            return NULL;
        }

        if (strcmp(nodedef->attr[i]->name, key) == 0) {
            return nodedef->attr[i];
        }
    }

    return NULL;
}


static void __report_free(refobject_t self, void **userdata)
{
    reportxml_t *report = REFOBJECT_TO_TYPE(self, reportxml_t *);

    refobject_unref(report->root);
}

static reportxml_t *    reportxml_new_with_root(reportxml_node_t *root)
{
    reportxml_t *ret;

    if (!root)
        return NULL;

    ret = REFOBJECT_TO_TYPE(refobject_new(sizeof(reportxml_t), __report_free, NULL, NULL, NULL), reportxml_t *);
    ret->root = root;

    return ret;
}

reportxml_t *           reportxml_new(void)
{
    reportxml_node_t *root = reportxml_node_new(REPORTXML_NODE_TYPE_REPORT, NULL, NULL, NULL);
    reportxml_t *ret;

    if (!root)
        return NULL;

    ret = reportxml_new_with_root(root);

    if (!ret) {
        refobject_unref(root);
        return NULL;
    }

    return ret;
}

reportxml_node_t *      reportxml_get_root_node(reportxml_t *report)
{
    if (!report)
        return NULL;

    if (refobject_ref(report->root) != 0)
        return NULL;

    return report->root;
}

reportxml_t *           reportxml_parse_xmldoc(xmlDocPtr doc)
{
    reportxml_node_t *root;
    reportxml_t *ret;
    xmlNodePtr xmlroot;

    if (!doc)
        return NULL;

    xmlroot = xmlDocGetRootElement(doc);
    if (!xmlroot)
        return NULL;

    root = reportxml_node_parse_xmlnode(xmlroot);
    if (!root)
        return NULL;

    if (reportxml_node_get_type(root) != REPORTXML_NODE_TYPE_REPORT) {
        refobject_unref(root);
        return NULL;
    }

    ret = reportxml_new_with_root(root);
    if (!ret) {
        refobject_unref(root);
        return NULL;
    }

    return ret;
}

xmlDocPtr               reportxml_render_xmldoc(reportxml_t *report)
{
    xmlDocPtr ret;
    xmlNodePtr node;

    if (!report)
        return NULL;

    node = reportxml_node_render_xmlnode(report->root);
    if (!node)
        return NULL;

    ret = xmlNewDoc(XMLSTR("1.0"));
    if (!ret) {
        xmlFreeNode(node);
        return NULL;
    }

    xmlDocSetRootElement(ret, node);

    return ret;
}

static void __report_node_free(refobject_t self, void **userdata)
{
    size_t i;

    reportxml_node_t *node = REFOBJECT_TO_TYPE(self, reportxml_node_t *);
    xmlFreeNode(node->xmlnode);

    for (i = 0; i < node->childs_len; i++) {
        refobject_unref(node->childs[i]);
    }

    free(node->content);
    free(node->childs);
}

reportxml_node_t *      reportxml_node_new(reportxml_node_type_t type, const char *id, const char *definition, const char *akindof)
{
    reportxml_node_t *ret;
    const struct nodedef *nodedef = __get_nodedef(type);
    size_t i;

    if (!nodedef)
        return NULL;

    ret = REFOBJECT_TO_TYPE(refobject_new(sizeof(reportxml_node_t), __report_node_free, NULL, NULL, NULL), reportxml_node_t *);

    if (ret == NULL)
        return NULL;

    ret->type = type;

    ret->xmlnode = xmlNewNode(NULL, XMLSTR(nodedef->name));

    if (!ret->xmlnode) {
        refobject_unref(ret);
        return NULL;
    }

    for (i = 0; i < (sizeof(nodedef->attr)/sizeof(*nodedef->attr)); i++) {
        const struct nodeattr *nodeattr = nodedef->attr[i];
        if (nodeattr->name == NULL)
            break;

        if (nodeattr->def) {
            if (reportxml_node_set_attribute(ret, nodeattr->name, nodeattr->def) != 0) {
                refobject_unref(ret);
                return NULL;
            }
        }
    }

#define _set_attr(x) \
    if ((x)) { \
        if (reportxml_node_set_attribute(ret, # x , (x)) != 0) { \
            refobject_unref(ret); \
            return NULL; \
        } \
    }

    _set_attr(id)
    _set_attr(definition)
    _set_attr(akindof)

    return ret;
}

reportxml_node_t *      reportxml_node_parse_xmlnode(xmlNodePtr xmlnode)
{
    reportxml_node_t *node;

    const struct nodedef *nodedef;

    if (!xmlnode)
        return NULL;

    nodedef = __get_nodedef_by_name((const char *)xmlnode->name);
    if (!nodedef)
        return NULL;

    node = reportxml_node_new(nodedef->type, NULL, NULL, NULL);
    if (!node)
        return NULL;

    if (xmlnode->properties) {
        xmlAttrPtr cur = xmlnode->properties;

        do {
            xmlChar *value = xmlNodeListGetString(xmlnode->doc, cur->children, 1);
            if (!value) {
                refobject_unref(node);
                return NULL;
            }

            if (reportxml_node_set_attribute(node, (const char*)cur->name, (const char*)value) != 0) {
                refobject_unref(node);
                return NULL;
            }

            xmlFree(value);
        } while ((cur = cur->next));
    }

    if (xmlnode->xmlChildrenNode) {
        xmlNodePtr cur = xmlnode->xmlChildrenNode;

        do {
            reportxml_node_t *child;

            if (xmlIsBlankNode(cur))
                continue;

            if (cur->type == XML_COMMENT_NODE)
                continue;

            if (cur->type == XML_TEXT_NODE) {
                xmlChar *value = xmlNodeListGetString(xmlnode->doc, cur, 1);

                if (!value) {
                    refobject_unref(node);
                    return NULL;
                }

                if (reportxml_node_set_content(node, (const char *)value) != 0) {
                    refobject_unref(node);
                    return NULL;
                }

                xmlFree(value);
                continue;
            }

            child = reportxml_node_parse_xmlnode(cur);
            if (!child) {
                refobject_unref(node);
                return NULL;
            }

            if (reportxml_node_add_child(node, child) != 0) {
                refobject_unref(node);
                return NULL;
            }
        } while ((cur = cur->next));
    }

    return node;
}

xmlNodePtr              reportxml_node_render_xmlnode(reportxml_node_t *node)
{
    xmlNodePtr ret;
    ssize_t count;
    size_t i;

    if (!node)
        return NULL;

    count = reportxml_node_count_child(node);
    if (count < 0)
        return NULL;

    ret = xmlCopyNode(node->xmlnode, 2);
    if (!ret)
        return NULL;

    for (i = 0; i < (size_t)count; i++) {
        reportxml_node_t *child = reportxml_node_get_child(node, i);
        xmlNodePtr xmlchild;

        if (!child) {
            xmlFreeNode(ret);
            return NULL;
        }

        xmlchild = reportxml_node_render_xmlnode(child);
        refobject_unref(child);
        if (!xmlchild) {
            xmlFreeNode(ret);
            return NULL;
        }

        xmlAddChild(ret, xmlchild);
    }

    if (node->content) {
        xmlNodePtr xmlchild = xmlNewText(XMLSTR(node->content));

        if (!xmlchild) {
            xmlFreeNode(ret);
            return NULL;
        }

        xmlAddChild(ret, xmlchild);
    }

    return ret;
}

reportxml_node_type_t   reportxml_node_get_type(reportxml_node_t *node)
{
    if (!node)
        return REPORTXML_NODE_TYPE__ERROR;

    return node->type;
}

int                     reportxml_node_set_attribute(reportxml_node_t *node, const char *key, const char *value)
{
    const struct nodedef *nodedef;
    const struct nodeattr *nodeattr;
    size_t i;

    if (!node || !key || !value)
        return -1;

    nodedef = __get_nodedef(node->type);
    nodeattr = __get_nodeattr(nodedef, key);
    if (!nodeattr)
        return -1;

    if (nodeattr->values[0] != NULL) {
        int found = 0;
        for (i = 0; i < (sizeof(nodeattr->values)/sizeof(*nodeattr->values)); i++) {
            if (nodeattr->values[i] == NULL)
                break;

            if (strcmp(nodeattr->values[i], value) == 0) {
                found = 1;
                break;
            }
        }

        if (!found)
            return -1;
    }

    if (nodeattr->checker) {
        if (nodeattr->checker(nodeattr, value) != 0) {
            return -1;
        }
    }

    xmlSetProp(node->xmlnode, XMLSTR(key), XMLSTR(value));

    return 0;
}

char *                  reportxml_node_get_attribute(reportxml_node_t *node, const char *key)
{
    xmlChar *k;
    char *n;

    if (!node || !key)
        return NULL;

    /* We do this super complicated thing as we do not know if we can use free() to free a xmlChar* object. */
    k = xmlGetProp(node->xmlnode, XMLSTR(key));
    if (!k)
        return NULL;

    n = strdup((char*)k);
    xmlFree(k);

    return n;
}

int                     reportxml_node_add_child(reportxml_node_t *node, reportxml_node_t *child)
{
    reportxml_node_t **n;

    if (!node || !child)
        return -1;

    n = realloc(node->childs, sizeof(*n)*(node->childs_len + 1));
    if (!n)
        return -1;

    node->childs = n;

    if (refobject_ref(child) != 0)
        return -1;

    node->childs[node->childs_len++] = child;

    ICECAST_LOG_DEBUG("Child %p added to Node %p", child, node);

    return 0;
}

ssize_t                 reportxml_node_count_child(reportxml_node_t *node)
{
    if (!node)
        return -1;

    return node->childs_len;
}

reportxml_node_t *      reportxml_node_get_child(reportxml_node_t *node, size_t idx)
{
    if (!node)
        return NULL;

    if (idx >= node->childs_len)
        return NULL;

    if (refobject_ref(node->childs[idx]) != 0)
        return NULL;

    return node->childs[idx];
}

int                     reportxml_node_set_content(reportxml_node_t *node, const char *value)
{
    const struct nodedef *nodedef;
    char *n = NULL;

    if (!node)
        return -1;

    nodedef = __get_nodedef(node->type);

    if (!nodedef->has_content)
        return -1;

    if (value) {
        n = strdup(value);
        if (!n)
            return -1;
    }

    free(node->content);
    node->content = n;

    return 0;
}

char *              reportxml_node_get_content(reportxml_node_t *node)
{
    if (!node)
        return NULL;

    if (node->content) {
        return strdup(node->content);
    } else {
        return NULL;
    }
}
