/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * This file contains functions for rendering XML as JSON.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xml2json.h"
#include "json.h"

#include "logging.h"
#define CATMODULE "xml2json"

static void render_node(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent)
{
    json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);

    json_renderer_write_key(renderer, "type", JSON_RENDERER_FLAGS_NONE);
    switch (node->type) {
        case XML_ELEMENT_NODE:
            json_renderer_write_string(renderer, "element", JSON_RENDERER_FLAGS_NONE);
            if (node->name) {
                json_renderer_write_key(renderer, "name", JSON_RENDERER_FLAGS_NONE);
                json_renderer_write_string(renderer, (const char *)node->name, JSON_RENDERER_FLAGS_NONE);
            }
        break;
        case XML_TEXT_NODE:
            json_renderer_write_string(renderer, "text", JSON_RENDERER_FLAGS_NONE);
        break;
        case XML_COMMENT_NODE:
            json_renderer_write_string(renderer, "comment", JSON_RENDERER_FLAGS_NONE);
        break;
        default:
            json_renderer_write_null(renderer);
        break;
    }

    if (node->content) {
        json_renderer_write_key(renderer, "text", JSON_RENDERER_FLAGS_NONE);
        json_renderer_write_string(renderer, (const char *)node->content, JSON_RENDERER_FLAGS_NONE);
    }

    json_renderer_write_key(renderer, "ns", JSON_RENDERER_FLAGS_NONE);
    if (node->ns && node->ns->href) {
        json_renderer_write_string(renderer, (const char *)node->ns->href, JSON_RENDERER_FLAGS_NONE);
        if (node->ns->prefix) {
            json_renderer_write_key(renderer, "nsprefix", JSON_RENDERER_FLAGS_NONE);
            json_renderer_write_string(renderer, (const char *)node->ns->prefix, JSON_RENDERER_FLAGS_NONE);
        }
    } else {
        json_renderer_write_null(renderer);
    }

    if (node->properties) {
        xmlAttrPtr cur = node->properties;

        json_renderer_write_key(renderer, "properties", JSON_RENDERER_FLAGS_NONE);
        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
        do {
            xmlChar *value = xmlNodeListGetString(doc, cur->children, 1);
            if (value) {
                json_renderer_write_key(renderer, (const char*)cur->name, JSON_RENDERER_FLAGS_NONE);
                json_renderer_write_string(renderer, (const char*)value, JSON_RENDERER_FLAGS_NONE);
                xmlFree(value);
            }
        } while ((cur = cur->next));
        json_renderer_end(renderer);
    }

    if (node->xmlChildrenNode) {
        xmlNodePtr cur = node->xmlChildrenNode;

        json_renderer_write_key(renderer, "children", JSON_RENDERER_FLAGS_NONE);
        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_ARRAY);
        do {
            render_node(renderer, doc, cur, node);
            cur = cur->next;
        } while (cur);
        json_renderer_end(renderer);
    }

    json_renderer_end(renderer);
}

char * xml2json_render_doc_simple(xmlDocPtr doc)
{
    json_renderer_t *renderer;
    xmlNodePtr xmlroot;

    if (!doc)
        return NULL;

    renderer = json_renderer_create(JSON_RENDERER_FLAGS_NONE);
    if (!renderer)
        return NULL;

    xmlroot = xmlDocGetRootElement(doc);
    if (!xmlroot)
        return NULL;

    render_node(renderer, doc, xmlroot, NULL);

    return json_renderer_finish(&renderer);
}
