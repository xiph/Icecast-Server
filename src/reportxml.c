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

#include <igloo/thread.h>
#include <igloo/avl.h>

#include "reportxml.h"
#include "refobject.h"

/* For XMLSTR() */
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "reportxml"

/* The report XML document type */
struct reportxml_tag {
    /* base object */
    refobject_base_t __base;
    /* the root report XML node of the document */
    reportxml_node_t *root;
};

/* The report XML node type */
struct reportxml_node_tag {
    /* base object */
    refobject_base_t __base;
    /* an XML node used to store the attributes */
    xmlNodePtr xmlnode;
    /* the type of the node */
    reportxml_node_type_t type;
    /* the report XML childs */
    reportxml_node_t **childs;
    size_t childs_len;
    /* the XML childs (used by <extension>) */
    xmlNodePtr *xml_childs;
    size_t xml_childs_len;
    /* the node's text content (used by <text>) */
    char *content;
};

/* The report XML database type */
struct reportxml_database_tag {
    /* base object */
    refobject_base_t __base;
    /* the lock used to ensure the database object is thread safe. */
    igloo_mutex_t lock;
    /* The tree of definitions */
    igloo_avl_tree *definitions;
};

/* The nodeattr structure is used to store definition of node attributes */
struct nodeattr;
struct nodeattr {
    /* name of the attribute */
    const char *name;
    /* the type of the attribute. This is based on the DTD */
    const char *type;
    /* the default value for the attribute if any */
    const char *def;
    /* whether the attribute is required or not */
    int required;
    /* a function that can be used to check the content of the attribute if any */
    int (*checker)(const struct nodeattr *attr, const char *str);
    /* NULL terminated list of possible values (if enum) */
    const char *values[32];
};

/* The type of the content an node has */
enum nodecontent {
    /* The node may not have any content */
    NC_NONE,
    /* The node may have children */
    NC_CHILDS,
    /* The node may have a text content */
    NC_CONTENT,
    /* The node may have XML children */
    NC_XML
};

/* This structure is used to define a node */
struct nodedef {
    /* the type of the node */
    reportxml_node_type_t type;
    /* the name of the corresponding XML node */
    const char *name;
    /* The type of the content the node may have */
    enum nodecontent content;
    /* __attr__eol terminated list of attributes the node may have */
    const struct nodeattr *attr[12];
    /* REPORTXML_NODE_TYPE__ERROR terminated list of child node types the node may have */
    const reportxml_node_type_t childs[12];
};

/* Prototypes */
static int __attach_copy_of_node_or_definition(reportxml_node_t *parent, reportxml_node_t *node, reportxml_database_t *db, ssize_t depth);
static reportxml_node_t *      __reportxml_database_build_node_ext(reportxml_database_t *db, const char *id, ssize_t depth, reportxml_node_type_t *acst_type_ret);

/* definition of known attributes */
static const struct nodeattr __attr__eol[1]             = {{NULL,           NULL,           NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_version[1]          = {{"version",      "CDATA",        "0.0.1",  1,  NULL, {"0.0.1", NULL}}};
static const struct nodeattr __attr_xmlns[1]            = {{"xmlns",        "URI",          "http://icecast.org/specs/reportxml-0.0.1",    1,  NULL, {"http://icecast.org/specs/reportxml-0.0.1", NULL}}};
static const struct nodeattr __attr_id[1]               = {{"id",           "ID",           NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr_definition[1]       = {{"definition",   "UUID",         NULL,     0,  NULL, {NULL}}};
static const struct nodeattr __attr__definition[1]      = {{"_definition",  "UUID",         NULL,     0,  NULL, {NULL}}}; /* for internal use only */
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
static const struct nodeattr __attr_application[1]      = {{"application",  "URI",          NULL,     1,  NULL, {NULL}}};
static const struct nodeattr __attr__action_type[1]     = {{"type",         NULL,           NULL,     1,  NULL, {"retry", "choice", "see-other", "authenticate", "pay", "change-protocol", "slow-down", "ask-user", "ask-admin", "bug", NULL}}};
static const struct nodeattr __attr__resource_type[1]   = {{"type",         NULL,           NULL,     1,  NULL, {"actor", "manipulation-target", "helper", "related", "result", "parameter", NULL}}};
static const struct nodeattr __attr__value_type[1]      = {{"type",         NULL,           NULL,     1,  NULL, {"null", "int", "float", "uuid", "string", "structure", "uri", "pointer", "version", "protocol", "username", "password", NULL}}};
static const struct nodeattr __attr__reference_type[1]  = {{"type",         NULL,           NULL,     1,  NULL, {"documentation", "log", "report", "related", NULL}}};

/* definition of known nodes */
/* Helper:
 * grep '^ *REPORTXML_NODE_TYPE_' reportxml.h | sed 's/^\( *REPORTXML_NODE_TYPE_\([^,]*\)\),*$/\1 \2/;' | while read l s; do c=$(tr A-Z a-z <<<"$s"); printf "    {%-32s \"%-16s 0, {__attr__eol}},\n" "$l," "$c\","; done
 */
#define __BASIC_ELEMENT __attr_id, __attr_definition, __attr_akindof, __attr__definition
static const struct nodedef __nodedef[] = {
    {REPORTXML_NODE_TYPE_REPORT,      "report",         NC_CHILDS,  {__attr_id, __attr_version, __attr_xmlns, __attr__eol},
        {REPORTXML_NODE_TYPE_INCIDENT, REPORTXML_NODE_TYPE_DEFINITION, REPORTXML_NODE_TYPE_TIMESTAMP, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_DEFINITION,  "definition",     NC_CHILDS,  {__BASIC_ELEMENT, __attr_template, __attr_defines, __attr__eol},
        {REPORTXML_NODE_TYPE_INCIDENT, REPORTXML_NODE_TYPE_STATE, REPORTXML_NODE_TYPE_TIMESTAMP, REPORTXML_NODE_TYPE_RESOURCE, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_FIX, REPORTXML_NODE_TYPE_RESOURCE, REPORTXML_NODE_TYPE_REASON, REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_INCIDENT,    "incident",       NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_STATE, REPORTXML_NODE_TYPE_TIMESTAMP, REPORTXML_NODE_TYPE_RESOURCE, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_FIX, REPORTXML_NODE_TYPE_REASON, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_STATE,       "state",          NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_TIMESTAMP, REPORTXML_NODE_TYPE_BACKTRACE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_BACKTRACE,   "backtrace",      NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_POSITION, REPORTXML_NODE_TYPE_MORE, REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_POSITION,    "position",       NC_CHILDS,  {__BASIC_ELEMENT, __attr_function, __attr_filename, __attr_line, __attr_binary, __attr_offset, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_MORE,        "more",           NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_FIX,         "fix",            NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_ACTION, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_ACTION,      "action",         NC_CHILDS,  {__BASIC_ELEMENT, __attr__action_type, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_TIMESTAMP, REPORTXML_NODE_TYPE_VALUE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_REASON,      "reason",         NC_CHILDS,  {__BASIC_ELEMENT, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_RESOURCE, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_TEXT,        "text",           NC_CONTENT, {__BASIC_ELEMENT, __attr_lang, __attr_dir, __attr__eol},
        {REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_TIMESTAMP,   "timestamp",      NC_NONE,    {__BASIC_ELEMENT, __attr_absolute, __attr_relative, __attr__eol},
        {REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_RESOURCE,    "resource",       NC_CHILDS,  {__BASIC_ELEMENT, __attr__resource_type, __attr_name, __attr__eol},
        {REPORTXML_NODE_TYPE_VALUE, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_VALUE,       "value",          NC_CHILDS,  {__BASIC_ELEMENT, __attr_member, __attr_value, __attr_state, __attr__value_type, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_REFERENCE, REPORTXML_NODE_TYPE_VALUE, REPORTXML_NODE_TYPE_POSITION, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_REFERENCE,   "reference",      NC_CHILDS,  {__BASIC_ELEMENT, __attr__reference_type, __attr_href, __attr__eol},
        {REPORTXML_NODE_TYPE_TEXT, REPORTXML_NODE_TYPE_EXTENSION, REPORTXML_NODE_TYPE__ERROR}},
    {REPORTXML_NODE_TYPE_EXTENSION,   "extension",      NC_XML,     {__BASIC_ELEMENT, __attr_application, __attr__eol},
        {REPORTXML_NODE_TYPE__ERROR}}
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

static int __report_new(refobject_t self, const refobject_type_t *type, va_list ap)
{
    reportxml_t *ret = REFOBJECT_TO_TYPE(self, reportxml_t*);
    reportxml_node_t *root = reportxml_node_new(REPORTXML_NODE_TYPE_REPORT, NULL, NULL, NULL);

    if (!root)
        return -1;

    ret->root = root;

    return 0;
}

REFOBJECT_DEFINE_TYPE(reportxml_t,
        REFOBJECT_DEFINE_TYPE_FREE(__report_free),
        REFOBJECT_DEFINE_TYPE_NEW(__report_new)
        );

static reportxml_t *    reportxml_new_with_root(reportxml_node_t *root)
{
    reportxml_t *ret;

    if (!root)
        return NULL;

    ret = refobject_new__new(reportxml_t, NULL, NULL, NULL);
    ret->root = root;

    return ret;
}

reportxml_t *           reportxml_new(void)
{
    return refobject_new(reportxml_t);
}

reportxml_node_t *      reportxml_get_root_node(reportxml_t *report)
{
    if (!report)
        return NULL;

    if (refobject_ref(report->root) != 0)
        return NULL;

    return report->root;
}

reportxml_node_t *      reportxml_get_node_by_attribute(reportxml_t *report, const char *key, const char *value, int include_definitions)
{
    if (!report || !key || !value)
        return NULL;

    return reportxml_node_get_child_by_attribute(report->root, key, value, include_definitions);
}

reportxml_node_t *      reportxml_get_node_by_type(reportxml_t *report, reportxml_node_type_t type, int include_definitions)
{
    if (!report)
        return NULL;

    return reportxml_node_get_child_by_type(report->root, type, include_definitions);
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

    for (i = 0; i < node->xml_childs_len; i++) {
        xmlFreeNode(node->xml_childs[i]);
    }

    free(node->content);
    free(node->childs);
    free(node->xml_childs);
}

REFOBJECT_DEFINE_TYPE(reportxml_node_t,
        REFOBJECT_DEFINE_TYPE_FREE(__report_node_free)
        );

reportxml_node_t *      reportxml_node_new(reportxml_node_type_t type, const char *id, const char *definition, const char *akindof)
{
    reportxml_node_t *ret;
    const struct nodedef *nodedef = __get_nodedef(type);
    size_t i;

    if (!nodedef)
        return NULL;

    ret = refobject_new__new(reportxml_node_t, NULL, NULL, NULL);

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
    if (!nodedef) {
        ICECAST_LOG_ERROR("Can not parse XML node: Unknown name <%s>", xmlnode->name);
        return NULL;
    }

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
                ICECAST_LOG_DEBUG("Can not parse XML node: attribute \"%H\" value \"%H\" is invalid on node of type <%s>", cur->name, value, nodedef->name);
                return NULL;
            }

            xmlFree(value);
        } while ((cur = cur->next));
    }

    if (xmlnode->xmlChildrenNode) {
        xmlNodePtr cur = xmlnode->xmlChildrenNode;

        do {
            if (nodedef->content == NC_XML) {
                if (reportxml_node_add_xml_child(node, cur) != 0) {
                    refobject_unref(node);
                    return NULL;
                }
            } else {
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
                    refobject_unref(child);
                    refobject_unref(node);
                    return NULL;
                }
                refobject_unref(child);
            }
        } while ((cur = cur->next));
    }

    return node;
}

static reportxml_node_t *      __reportxml_node_copy_with_db(reportxml_node_t *node, reportxml_database_t *db, ssize_t depth)
{
    reportxml_node_t *ret;
    ssize_t child_count;
    ssize_t xml_child_count;
    size_t i;

    if (!node)
        return NULL;

    child_count = reportxml_node_count_child(node);
    if (child_count < 0)
        return NULL;

    xml_child_count = reportxml_node_count_xml_child(node);
    if (xml_child_count < 0)
        return NULL;

    ret = reportxml_node_parse_xmlnode(node->xmlnode);
    if (!ret)
        return NULL;

    if (node->content) {
        if (reportxml_node_set_content(ret, node->content) != 0) {
            refobject_unref(ret);
            return NULL;
        }
    }

    for (i = 0; i < (size_t)child_count; i++) {
        reportxml_node_t *child = reportxml_node_get_child(node, i);

        if (db && depth > 0) {
            if (__attach_copy_of_node_or_definition(ret, child, db, depth) != 0) {
                refobject_unref(child);
                refobject_unref(ret);
                return NULL;
            }
            refobject_unref(child);
        } else {
            reportxml_node_t *copy = __reportxml_node_copy_with_db(child, NULL, -1);

            refobject_unref(child);

            if (!copy) {
                refobject_unref(ret);
                return NULL;
            }

            if (reportxml_node_add_child(ret, copy) != 0) {
                refobject_unref(copy);
                refobject_unref(ret);
                return NULL;
            }

            refobject_unref(copy);
        }
    }

    for (i = 0; i < (size_t)xml_child_count; i++) {
        xmlNodePtr child = reportxml_node_get_xml_child(node, i);
        if (reportxml_node_add_xml_child(ret, child) != 0) {
            xmlFreeNode(child);
            refobject_unref(ret);
            return NULL;
        }
        xmlFreeNode(child);
    }

    return ret;
}

reportxml_node_t *      reportxml_node_copy(reportxml_node_t *node)
{
    return __reportxml_node_copy_with_db(node, NULL, -1);
}

xmlNodePtr              reportxml_node_render_xmlnode(reportxml_node_t *node)
{
    xmlNodePtr ret;
    ssize_t child_count;
    ssize_t xml_child_count;
    size_t i;
    xmlChar *definition;

    if (!node)
        return NULL;

    child_count = reportxml_node_count_child(node);
    if (child_count < 0)
        return NULL;

    xml_child_count = reportxml_node_count_xml_child(node);
    if (xml_child_count < 0)
        return NULL;

    ret = xmlCopyNode(node->xmlnode, 2);
    if (!ret)
        return NULL;

    definition = xmlGetProp(ret, XMLSTR("_definition"));
    if (definition) {
        xmlSetProp(ret, XMLSTR("definition"), definition);
        xmlUnsetProp(ret, XMLSTR("_definition"));
        xmlFree(definition);
    }

    for (i = 0; i < (size_t)child_count; i++) {
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

    for (i = 0; i < (size_t)xml_child_count; i++) {
        xmlNodePtr xmlchild = reportxml_node_get_xml_child(node, i);

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
    const struct nodedef *nodedef;
    reportxml_node_t **n;
    size_t i;
    int found;

    if (!node || !child)
        return -1;

    nodedef = __get_nodedef(node->type);

    if (nodedef->content != NC_CHILDS)
        return -1;

    found = 0;
    for (i = 0; nodedef->childs[i] != REPORTXML_NODE_TYPE__ERROR; i++) {
        if (nodedef->childs[i] == child->type) {
            found = 1;
            break;
        }
    }

    if (!found)
        return -1;

    n = realloc(node->childs, sizeof(*n)*(node->childs_len + 1));
    if (!n)
        return -1;

    node->childs = n;

    if (refobject_ref(child) != 0)
        return -1;

    node->childs[node->childs_len++] = child;

//    ICECAST_LOG_DEBUG("Child %p added to Node %p", child, node);

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

reportxml_node_t *      reportxml_node_get_child_by_attribute(reportxml_node_t *node, const char *key, const char *value, int include_definitions)
{
    reportxml_node_t *ret;
    xmlChar *k;
    size_t i;

    if (!node || !key ||!value)
        return NULL;

    k = xmlGetProp(node->xmlnode, XMLSTR(key));
    if (k != NULL) {
        if (strcmp((const char*)k, value) == 0) {
            xmlFree(k);

            if (refobject_ref(node) != 0)
                return NULL;

            return node;
        }
        xmlFree(k);
    }

    if (node->type == REPORTXML_NODE_TYPE_DEFINITION && !include_definitions)
        return NULL;

    for (i = 0; i < node->childs_len; i++) {
        ret = reportxml_node_get_child_by_attribute(node->childs[i], key, value, include_definitions);
        if (ret != NULL)
            return ret;
    }

    return NULL;
}

reportxml_node_t *      reportxml_node_get_child_by_type(reportxml_node_t *node, reportxml_node_type_t type, int include_definitions)
{
    size_t i;

    if (!node)
        return NULL;

    if (node->type == type) {
        if (refobject_ref(node) != 0)
            return NULL;
        return node;
    }

    if (node->type == REPORTXML_NODE_TYPE_DEFINITION && !include_definitions)
        return NULL;

    for (i = 0; i < node->childs_len; i++) {
        reportxml_node_t *ret;

        ret = reportxml_node_get_child_by_type(node->childs[i], type, include_definitions);
        if (ret != NULL)
            return ret;
    }

    return NULL;
}

int                     reportxml_node_set_content(reportxml_node_t *node, const char *value)
{
    const struct nodedef *nodedef;
    char *n = NULL;

    if (!node)
        return -1;

    nodedef = __get_nodedef(node->type);

    if (nodedef->content != NC_CONTENT)
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

int                     reportxml_node_add_xml_child(reportxml_node_t *node, xmlNodePtr child)
{
    const struct nodedef *nodedef;
    xmlNodePtr *n;

    if (!node || !child)
        return -1;

    nodedef = __get_nodedef(node->type);

    if (nodedef->content != NC_XML)
        return -1;

    n = realloc(node->xml_childs, sizeof(*n)*(node->xml_childs_len + 1));
    if (!n)
        return -1;

    node->xml_childs = n;

    node->xml_childs[node->xml_childs_len] = xmlCopyNode(child, 1);
    if (node->xml_childs[node->xml_childs_len] == NULL)
        return -1;

    node->xml_childs_len++;

    return 0;
}

ssize_t                 reportxml_node_count_xml_child(reportxml_node_t *node)
{
    if (!node)
        return -1;

    return node->xml_childs_len;
}

xmlNodePtr              reportxml_node_get_xml_child(reportxml_node_t *node, size_t idx)
{
    xmlNodePtr ret;

    if (!node)
        return NULL;

    if (idx >= node->xml_childs_len)
        return NULL;

    ret = xmlCopyNode(node->xml_childs[idx], 1);

    return ret;
}

static void __database_free(refobject_t self, void **userdata)
{
    reportxml_database_t *db = REFOBJECT_TO_TYPE(self, reportxml_database_t *);

    if (db->definitions)
        igloo_avl_tree_free(db->definitions, (igloo_avl_free_key_fun_type)refobject_unref);

    igloo_thread_mutex_destroy(&(db->lock));
}

static int __compare_definitions(void *arg, void *a, void *b)
{
    char *id_a, *id_b;
    int ret = 0;

    id_a = reportxml_node_get_attribute(a, "defines");
    id_b = reportxml_node_get_attribute(b, "defines");

    if (!id_a || !id_b || id_a == id_b) {
        ret = 0;
    } else {
        ret = strcmp(id_a, id_b);
    }

    free(id_a);
    free(id_b);

    return ret;
}

static int __database_new(refobject_t self, const refobject_type_t *type, va_list ap)
{
    reportxml_database_t *ret = REFOBJECT_TO_TYPE(self, reportxml_database_t*);

    thread_mutex_create(&(ret->lock));

    ret->definitions = igloo_avl_tree_new(__compare_definitions, NULL);
    if (!ret->definitions)
        return -1;

    return 0;
}

REFOBJECT_DEFINE_TYPE(reportxml_database_t,
        REFOBJECT_DEFINE_TYPE_FREE(__database_free),
        REFOBJECT_DEFINE_TYPE_NEW(__database_new)
        );

reportxml_database_t *  reportxml_database_new(void)
{
    return refobject_new(reportxml_database_t);
}

int                     reportxml_database_add_report(reportxml_database_t *db, reportxml_t *report)
{
    reportxml_node_t *root;
    ssize_t count;
    size_t i;

    if (!db || !report)
        return -1;

    root = reportxml_get_root_node(report);
    if (!root)
        return -1;

    count = reportxml_node_count_child(root);
    if (count < 0)
        return -1;

    thread_mutex_lock(&(db->lock));

    for (i = 0; i < (size_t)count; i++) {
        reportxml_node_t *node = reportxml_node_get_child(root, i);
        reportxml_node_t *copy;

        if (reportxml_node_get_type(node) != REPORTXML_NODE_TYPE_DEFINITION) {
            refobject_unref(node);
            continue;
        }

        copy = reportxml_node_copy(node);
        refobject_unref(node);

        if (!copy)
            continue;

        igloo_avl_insert(db->definitions, copy);
    }

    thread_mutex_unlock(&(db->lock));

    refobject_unref(root);

    return 0;
}

static int __attach_copy_of_node_or_definition(reportxml_node_t *parent, reportxml_node_t *node, reportxml_database_t *db, ssize_t depth)
{
    reportxml_node_t *copy;
    reportxml_node_t *def = NULL;
    char *definition;

    if (!parent || !node || !db)
        return -1;


    if (depth >= 2) {
        definition = reportxml_node_get_attribute(node, "definition");
        if (definition) {
            ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi, definition=\"%H\"", parent, node, depth, definition);
            def = reportxml_database_build_node(db, definition, depth - 1);
            ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi, Definition for \"%H\" at %p", parent, node, depth, definition, def);
            free(definition);
        }
    }

    if (def) {
        ssize_t count = reportxml_node_count_child(def);
        size_t i;

        ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi, Found definition.", parent, node, depth);

        if (count < 0) {
            refobject_unref(def);
            ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi <- -1", parent, node, depth);
            return -1;
        }

        for (i = 0; i < (size_t)count; i++) {
            reportxml_node_t *child = reportxml_node_get_child(def, i);

            ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi, Itering, child #%zu (%p)", parent, node, depth, i, child);

            if (__attach_copy_of_node_or_definition(parent, child, db, depth - 1) != 0) {
                refobject_unref(child);
                refobject_unref(def);
                ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi <- -1", parent, node, depth);
                return -1;
            }

            refobject_unref(child);
        }

        refobject_unref(def);

        ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi <- 0", parent, node, depth);
        return 0;
    } else {
        int ret;

        ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi, Found no definition.", parent, node, depth);

        copy = __reportxml_node_copy_with_db(node, db, depth - 1);
        if (!copy) {
            ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi <- -1", parent, node, depth);
            return -1;
        }

        ret = reportxml_node_add_child(parent, copy);

        refobject_unref(copy);

        ICECAST_LOG_DEBUG("parent=%p, node=%p, depth=%zi <- %i", parent, node, depth, ret);
        return ret;
    }
}

static reportxml_node_t *      __reportxml_database_build_node_ext(reportxml_database_t *db, const char *id, ssize_t depth, reportxml_node_type_t *acst_type_ret)
{
    reportxml_node_t *search;
    reportxml_node_t *found;
    reportxml_node_t *ret;
    enum {
        ACST_FIRST,
        ACST_YES,
        ACST_NO,
    } all_childs_same_type = ACST_FIRST;
    reportxml_node_type_t acst_type = REPORTXML_NODE_TYPE__ERROR;
    char *template;
    ssize_t count;
    size_t i;

    if (!db || !id)
        return NULL;

    /* Assign default depth in case it's set to -1 */
    if (depth < 0)
        depth = 8;

    ICECAST_LOG_DEBUG("Looking up \"%H\" in database %p with depth %zu", id, db, depth);

    if (!depth)
        return NULL;

    search = reportxml_node_new(REPORTXML_NODE_TYPE_DEFINITION, NULL, NULL, NULL);
    if (!search)
        return NULL;

    if (reportxml_node_set_attribute(search, "defines", id) != 0) {
        refobject_unref(search);
        return NULL;
    }

    thread_mutex_lock(&(db->lock));
    if (igloo_avl_get_by_key(db->definitions, REFOBJECT_TO_TYPE(search, void *), (void**)&found) != 0) {
        thread_mutex_unlock(&(db->lock));
        refobject_unref(search);
        return NULL;
    }

    refobject_unref(search);

    if (refobject_ref(found) != 0) {
        thread_mutex_unlock(&(db->lock));
        return NULL;
    }
    thread_mutex_unlock(&(db->lock));

    count = reportxml_node_count_child(found);
    if (count < 0) {
        refobject_unref(found);
        return NULL;
    }

    template = reportxml_node_get_attribute(found, "template");
    if (template) {
        reportxml_node_t *tpl = reportxml_database_build_node(db, template, depth - 1);

        free(template);

        if (tpl) {
            ret = reportxml_node_copy(tpl);
            refobject_unref(tpl);
        } else {
            ret = NULL;
        }
    } else {
        ret = reportxml_node_new(REPORTXML_NODE_TYPE_DEFINITION, NULL, NULL, NULL);
    }

    if (!ret) {
        refobject_unref(found);
        return NULL;
    }

    for (i = 0; i < (size_t)count; i++) {
        /* TODO: Look up definitions of our childs and childs' childs. */

        reportxml_node_t *node = reportxml_node_get_child(found, i);
        reportxml_node_type_t type = reportxml_node_get_type(node);

        switch (all_childs_same_type) {
            case ACST_FIRST:
                acst_type = type;
                all_childs_same_type = ACST_YES;
            break;
            case ACST_YES:
                if (acst_type != type)
                    all_childs_same_type = ACST_NO;
            break;
            case ACST_NO:
                /* noop */
            break;
        }

        /* We want depth, not depth - 1 here. __attach_copy_of_node_or_definition() takes care of this for us. */
        if (__attach_copy_of_node_or_definition(ret, node, db, depth) != 0) {
            refobject_unref(node);
            refobject_unref(found);
            refobject_unref(ret);
            ICECAST_LOG_ERROR("Can not attach child #%zu (%p) to attachment point (%p) in report. BAD.", i, node, ret);
            return NULL;
        }

        refobject_unref(node);
    }

    refobject_unref(found);

    if (all_childs_same_type == ACST_YES) {
        count = reportxml_node_count_child(ret);
        if (count < 0) {
            refobject_unref(ret);
            return NULL;
        }

        for (i = 0; i < (size_t)count; i++) {
            reportxml_node_t *node = reportxml_node_get_child(ret, i);

            if (!node) {
                refobject_unref(ret);
                return NULL;
            }

            if (reportxml_node_set_attribute(node, "_definition", id) != 0) {
                refobject_unref(node);
                refobject_unref(ret);
                return NULL;
            }

            refobject_unref(node);
        }
    }

    if (acst_type_ret) {
        if (all_childs_same_type == ACST_YES) {
            *acst_type_ret = acst_type;
        } else {
            *acst_type_ret = REPORTXML_NODE_TYPE__ERROR;
        }
    }

    return ret;
}

reportxml_node_t *      reportxml_database_build_node(reportxml_database_t *db, const char *id, ssize_t depth)
{
    return __reportxml_database_build_node_ext(db, id, depth, NULL);
}

/* We try to build a a report from the definition. Exat structure depends on what is defined. */
reportxml_t *           reportxml_database_build_report(reportxml_database_t *db, const char *id, ssize_t depth)
{
    reportxml_node_t *definition;
    reportxml_node_t *child;
    reportxml_node_t *root;
    reportxml_node_t *attach_to;
    reportxml_node_type_t type;
    reportxml_t *ret;
    ssize_t count;
    size_t i;

    if (!db || !id)
        return NULL;

    /* first find the definition itself.  This will be some REPORTXML_NODE_TYPE_DEFINITION node. */
    definition = __reportxml_database_build_node_ext(db, id, depth, &type);
    if (!definition) {
        ICECAST_LOG_WARN("No matching definition for \"%H\"", id);
        return NULL;
    }

    /* Let's see how many children we have. */
    count = reportxml_node_count_child(definition);
    if (count < 0) {
        refobject_unref(definition);
        ICECAST_LOG_ERROR("Can not get child count from definition. BAD.");
        return NULL;
    } else if (count == 0) {
        /* Empty definition? Not exactly an exciting report... */
        ICECAST_LOG_WARN("Empty definition for \"%H\". Returning empty report. This is likely an error.", id);
        refobject_unref(definition);
        return refobject_new(reportxml_t);
    }

    if (type == REPORTXML_NODE_TYPE__ERROR) {
        /* Now the hard part: find out what level we are. */
        child = reportxml_node_get_child(definition, 0);
        if (!child) {
            refobject_unref(definition);
            ICECAST_LOG_ERROR("Can not get first child. BAD.");
            return NULL;
        }

        type = reportxml_node_get_type(child);
        refobject_unref(child);
    }

    /* check for supported configurations */
    switch (type) {
        case REPORTXML_NODE_TYPE_INCIDENT:
        case REPORTXML_NODE_TYPE_STATE:
        break;
        default:
            refobject_unref(definition);
            ICECAST_LOG_WARN("Unsupported type of first child.");
            return NULL;
        break;
    }

    ret = refobject_new(reportxml_t);
    if (!ret) {
        refobject_unref(definition);
        ICECAST_LOG_ERROR("Can not allocate new report. BAD.");
        return NULL;
    }

    root = reportxml_get_root_node(ret);
    if (!ret) {
        refobject_unref(definition);
        refobject_unref(ret);
        ICECAST_LOG_ERROR("Can not get root node from report. BAD.");
        return NULL;
    }

    if (type == REPORTXML_NODE_TYPE_INCIDENT) {
        refobject_ref(attach_to = root);
    } else if (type == REPORTXML_NODE_TYPE_STATE) {
        attach_to = reportxml_node_new(REPORTXML_NODE_TYPE_INCIDENT, NULL, NULL, NULL);
        if (attach_to) {
            if (reportxml_node_add_child(root, attach_to) != 0) {
                refobject_unref(attach_to);
                attach_to = NULL;
            }
        }
    } else {
        attach_to = NULL;
    }

    refobject_unref(root);

    if (!attach_to) {
        refobject_unref(definition);
        refobject_unref(ret);
        ICECAST_LOG_ERROR("No point to attach to in report. BAD.");
        return NULL;
    }

    /* now move nodes. */

    for (i = 0; i < (size_t)count; i++) {
        child = reportxml_node_get_child(definition, i);

        if (reportxml_node_get_type(child) == type) {
            /* Attach definition to all childs that are the same type.
             * As long as we work to-the-specs all childs are of the same type.
             * But if we work in relaxed mode, there might be other tags.
             */
            reportxml_node_set_attribute(child, "definition", id);
        }

        /* we can directly attach as it's a already a copy. */
        if (reportxml_node_add_child(attach_to, child) != 0) {
            refobject_unref(definition);
            refobject_unref(attach_to);
            refobject_unref(ret);
            ICECAST_LOG_ERROR("Can not attach child #%zu (%p) to attachment point (%p) in report. BAD.", i, child, attach_to);
            return NULL;
        }

        refobject_unref(child);
    }

    refobject_unref(definition);
    refobject_unref(attach_to);

    return ret;
}
