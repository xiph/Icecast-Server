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

#include <string.h>
#include <stdlib.h>

#include "xml2json.h"
#include "json.h"
#include "util.h"

/* For XMLSTR() */
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "xml2json"

struct xml2json_cache {
    const char *default_namespace;
    xmlNsPtr ns;
    void (*render)(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache);
};

struct nodelist {
    xmlNodePtr *nodes;
    size_t len;
    size_t fill;
};

static void render_node(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache);
static void render_node_generic(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache);

static void nodelist_init(struct nodelist *list)
{
    memset(list, 0, sizeof(*list));
}

static void nodelist_free(struct nodelist *list)
{
    free(list->nodes);
    memset(list, 0, sizeof(*list));
}

static void nodelist_push(struct nodelist *list, xmlNodePtr node)
{
    if (list->fill == list->len) {
        xmlNodePtr *n = realloc(list->nodes, sizeof(xmlNodePtr)*(list->len + 16));
        if (!n) {
            ICECAST_LOG_ERROR("Can not allocate memory for node list. BAD.");
            return;
        }

        list->nodes = n;
        list->len += 16;
    }

    list->nodes[list->fill++] = node;
}

static xmlNodePtr nodelist_get(struct nodelist *list, size_t idx)
{
    if (idx >= list->fill)
        return NULL;
    return list->nodes[idx];
}

static void nodelist_unset(struct nodelist *list, size_t idx)
{
    if (idx >= list->fill)
        return;
    list->nodes[idx] = NULL;
}

static size_t nodelist_fill(struct nodelist *list)
{
    return list->fill;
}

static int nodelist_is_empty(struct nodelist *list)
{
    size_t i;

    for (i = 0; i < list->fill; i++)
        if (list->nodes[i])
            return 0;

    return 1;
}

static void handle_textchildnode(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    xmlChar *value = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (value) {
        json_renderer_write_key(renderer, (const char *)node->name, JSON_RENDERER_FLAGS_NONE);
        json_renderer_write_string(renderer, (const char*)value, JSON_RENDERER_FLAGS_NONE);
        xmlFree(value);
    }
}

static void handle_booleanchildnode(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    xmlChar *value = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (value) {
        json_renderer_write_key(renderer, (const char *)node->name, JSON_RENDERER_FLAGS_NONE);
        json_renderer_write_boolean(renderer, util_str_to_bool((const char*)value));
        xmlFree(value);
    }
}

static int handle_node_modules(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    if (node->type == XML_ELEMENT_NODE && strcmp((const char *)node->name, "modules") == 0) {
        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
        if (node->xmlChildrenNode) {
            xmlNodePtr cur = node->xmlChildrenNode;

            do {
                if (cur->type == XML_ELEMENT_NODE && cur->name && strcmp((const char *)cur->name, "module") == 0) {
                    xmlChar *name = xmlGetProp(cur, XMLSTR("name"));
                    if (name) {
                        json_renderer_write_key(renderer, (const char *)name, JSON_RENDERER_FLAGS_NONE);
                        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
                        if (node->properties) {
                            xmlAttrPtr prop = node->properties;

                            do {
                                xmlChar *value = xmlNodeListGetString(doc, prop->children, 1);
                                if (value) {
                                    json_renderer_write_key(renderer, (const char*)cur->name, JSON_RENDERER_FLAGS_NONE);
                                    json_renderer_write_string(renderer, (const char*)value, JSON_RENDERER_FLAGS_NONE);
                                    xmlFree(value);
                                }
                            } while ((prop = prop->next));
                        }
                        json_renderer_end(renderer);
                        xmlFree(name);
                    }
                } else {
                    json_renderer_write_key(renderer, "unhandled-child", JSON_RENDERER_FLAGS_NONE);
                    render_node(renderer, doc, cur, node, cache);
                }
                cur = cur->next;
            } while (cur);
        }
        json_renderer_end(renderer);
        return 1;
    }

    return 0;
}

static void render_node_legacyresponse(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    int handled = 0;

    if (node->type == XML_ELEMENT_NODE) {
        const char *nodename = (const char *)node->name;
        handled = 1;
        if (strcmp(nodename, "iceresponse") == 0) {
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
            if (node->xmlChildrenNode) {
                xmlNodePtr cur = node->xmlChildrenNode;

                do {
                    int handled_child = 1;

                    if (cur->type == XML_ELEMENT_NODE && cur->name) {
                        if (strcmp((const char *)cur->name, "message") == 0) {
                            handle_textchildnode(renderer, doc, cur, node, cache);
                        } else if (strcmp((const char *)cur->name, "return") == 0) {
                            xmlChar *value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
                            if (value) {
                                json_renderer_write_key(renderer, "success", JSON_RENDERER_FLAGS_NONE);
                                json_renderer_write_boolean(renderer, strcmp((const char *)value, "1") == 0);
                                xmlFree(value);
                            }
                        } else if (strcmp((const char *)cur->name, "modules") == 0) {
                            json_renderer_write_key(renderer, "modules", JSON_RENDERER_FLAGS_NONE);
                            render_node(renderer, doc, cur, node, cache);
                        } else {
                            handled_child = 0;
                        }
                    } else {
                        handled_child = 0;
                    }

                    if (!handled_child) {
                        json_renderer_write_key(renderer, "unhandled-child", JSON_RENDERER_FLAGS_NONE);
                        render_node(renderer, doc, cur, node, cache);
                    }
                    cur = cur->next;
                } while (cur);
            }
            json_renderer_end(renderer);
        } else if (strcmp(nodename, "modules") == 0) {
            handled = handle_node_modules(renderer, doc, node, parent, cache);
        } else {
            handled = 0;
        }
    }

    if (!handled)
        render_node_generic(renderer, doc, node, parent, cache);
}

static int handle_simple_child(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache, xmlNodePtr child, const char * number_keys[], const char * boolean_keys[])
{
    if (child->type == XML_ELEMENT_NODE && child->name) {
        size_t i;

        for (i = 0; number_keys[i]; i++) {
            if (strcmp((const char *)child->name, number_keys[i]) == 0) {
                xmlChar *value = xmlNodeListGetString(doc, child->xmlChildrenNode, 1);
                if (value) {
                    json_renderer_write_key(renderer, (const char *)child->name, JSON_RENDERER_FLAGS_NONE);
                    json_renderer_write_int(renderer, strtoll((const char*)value, NULL, 10));
                    xmlFree(value);
                    return 1;
                }
            }
        }

        for (i = 0; boolean_keys[i]; i++) {
            if (strcmp((const char *)child->name, boolean_keys[i]) == 0) {
                handle_booleanchildnode(renderer, doc, child, node, cache);
                return 1;
            }
        }

        if (child->xmlChildrenNode && !child->xmlChildrenNode->next && child->xmlChildrenNode->type == XML_TEXT_NODE) {
            handle_textchildnode(renderer, doc, child, node, cache);
            return 1;
        }
    }

    return 0;
}

static void render_node_legacystats(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    static const char * number_keys_global[] = {
        "listeners", "clients", "client_connections", "connections", "file_connections", "listener_connections",
        "source_client_connections", "source_relay_connections", "source_total_connections", "sources", "stats", "stats_connections", NULL
    };
    static const char * boolean_keys_global[] = {
        NULL
    };
    static const char * number_keys_source[] = {
        "audio_bitrate", "audio_channels", "audio_samplerate", "ice-bitrate", "listener_peak", "listeners", "slow_listeners",
        "total_bytes_read", "total_bytes_sent", NULL
    };
    static const char * boolean_keys_source[] = {
        "public", NULL
    };

    int handled = 0;

    if (node->type == XML_ELEMENT_NODE) {
        const char *nodename = (const char *)node->name;
        handled = 1;
        if (strcmp(nodename, "icestats") == 0 || strcmp(nodename, "source") == 0) {
            int is_icestats = strcmp(nodename, "icestats") == 0;
            struct nodelist nodelist;
            size_t i;
            size_t len;

            nodelist_init(&nodelist);

            if (is_icestats)
                json_renderer_begin(renderer, JSON_ELEMENT_TYPE_ARRAY);
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
            if (node->xmlChildrenNode) {
                xmlNodePtr cur = node->xmlChildrenNode;
                do {
                    if (!handle_simple_child(renderer, doc, node, parent, cache, cur,
                                is_icestats ? number_keys_global : number_keys_source,
                                is_icestats ? boolean_keys_global : boolean_keys_source
                                )) {
                        nodelist_push(&nodelist, cur);
                    }
                    cur = cur->next;
                } while (cur);
            }

            len = nodelist_fill(&nodelist);
            for (i = 0; i < len; i++) {
                xmlNodePtr cur = nodelist_get(&nodelist, i);
                if (cur == NULL)
                    continue;

                if (cur->type == XML_ELEMENT_NODE && cur->name) {
                    if (strcmp((const char *)cur->name, "modules") == 0) {
                        json_renderer_write_key(renderer, (const char *)cur->name, JSON_RENDERER_FLAGS_NONE);
                        handle_node_modules(renderer, doc, cur, node, cache);
                        nodelist_unset(&nodelist, i);
                    } else if (strcmp((const char *)cur->name, "source") == 0) {
                        size_t j;

                        json_renderer_write_key(renderer, (const char *)cur->name, JSON_RENDERER_FLAGS_NONE);
                        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);

                        for (j = i; j < len; j++) {
                            xmlNodePtr subcur = nodelist_get(&nodelist, j);
                            if (subcur == NULL)
                                continue;

                            if (subcur->type == XML_ELEMENT_NODE && subcur->name && strcmp((const char *)cur->name, (const char *)subcur->name) == 0) {
                                xmlChar *mount = xmlGetProp(subcur, XMLSTR("mount"));
                                if (mount) {
                                    json_renderer_write_key(renderer, (const char *)mount, JSON_RENDERER_FLAGS_NONE);
                                    xmlFree(mount);
                                    nodelist_unset(&nodelist, j);
                                    render_node_legacystats(renderer, doc, cur, node, cache);
                                }
                            }
                        }

                        json_renderer_end(renderer);
                        nodelist_unset(&nodelist, i);
                    } else if (strcmp((const char *)cur->name, "metadata") == 0) {
                        size_t j;

                        json_renderer_write_key(renderer, (const char *)cur->name, JSON_RENDERER_FLAGS_NONE);
                        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
                        for (j = i; j < len; j++) {
                            xmlNodePtr subcur = nodelist_get(&nodelist, j);
                            if (subcur == NULL)
                                continue;

                            if (subcur->type == XML_ELEMENT_NODE && subcur->name && strcmp((const char *)cur->name, (const char *)subcur->name) == 0) {
                                xmlNodePtr child = subcur->xmlChildrenNode;
                                while (child) {
                                    handle_textchildnode(renderer, doc, child, subcur, cache);
                                    child = child->next;
                                }
                                nodelist_unset(&nodelist, j);
                            }
                        }
                        json_renderer_end(renderer);
                    } else if (strcmp((const char *)cur->name, "authentication") == 0) {
                        size_t j;

                        json_renderer_write_key(renderer, (const char *)cur->name, JSON_RENDERER_FLAGS_NONE);
                        json_renderer_begin(renderer, JSON_ELEMENT_TYPE_ARRAY);
                        for (j = i; j < len; j++) {
                            xmlNodePtr subcur = nodelist_get(&nodelist, j);
                            if (subcur == NULL)
                                continue;

                            if (subcur->type == XML_ELEMENT_NODE && subcur->name && strcmp((const char *)cur->name, (const char *)subcur->name) == 0) {
                                xmlNodePtr child = subcur->xmlChildrenNode;
                                while (child) {
                                    render_node_legacystats(renderer, doc, child, subcur, cache);
                                    child = child->next;
                                }
                                nodelist_unset(&nodelist, j);
                            }
                        }
                        json_renderer_end(renderer);
                    } else if (strcmp((const char *)cur->name, "playlist") == 0) {
                        json_renderer_write_key(renderer, (const char *)cur->name, JSON_RENDERER_FLAGS_NONE);
                        render_node(renderer, doc, cur, node, cache);
                        nodelist_unset(&nodelist, i);
                    }
                }
                //render_node_generic(renderer, doc, node, parent, cache);
            }

            if (!nodelist_is_empty(&nodelist)) {
                json_renderer_write_key(renderer, "unhandled-child", JSON_RENDERER_FLAGS_NONE);
                json_renderer_begin(renderer, JSON_ELEMENT_TYPE_ARRAY);
                len = nodelist_fill(&nodelist);
                for (i = 0; i < len; i++) {
                    xmlNodePtr cur = nodelist_get(&nodelist, i);
                    if (cur == NULL)
                        continue;

                    render_node(renderer, doc, cur, node, cache);
                }
                json_renderer_end(renderer);
            }

            json_renderer_end(renderer);
            if (is_icestats)
                json_renderer_end(renderer);

            nodelist_free(&nodelist);
        } else if (strcmp(nodename, "role") == 0) {
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
            if (node->properties) {
                xmlAttrPtr cur = node->properties;

                do {
                    const char *name = (const char*)cur->name;
                    xmlChar *value = xmlNodeListGetString(doc, cur->children, 1);

                    if (value) {
                        json_renderer_write_key(renderer, name, JSON_RENDERER_FLAGS_NONE);
                        if (strncmp(name, "can-", 4) == 0) {
                            json_renderer_write_boolean(renderer, util_str_to_bool((const char *)value));
                        } else {
                            json_renderer_write_string(renderer, (const char*)value, JSON_RENDERER_FLAGS_NONE);
                        }
                        xmlFree(value);
                    }
                } while ((cur = cur->next));
            }
            json_renderer_end(renderer);
        } else {
            handled = 0;
        }
    }

    if (!handled)
        render_node_generic(renderer, doc, node, parent, cache);
}

static void render_node_xspf(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    const char * text_keys[] = {"title", "creator", "annotation", "info", "identifier", "image", "date", "license", "album", NULL};
    const char * uint_keys[] = {"trackNum", "duration", NULL};
    int handled = 0;

    if (node->type == XML_ELEMENT_NODE) {
        const char *nodename = (const char *)node->name;
        int handle_childs = 0;
        int close_after_me = 0;
        size_t i;

        handled = 1;

        for (i = 0; text_keys[i]; i++) {
            if (strcmp(nodename, text_keys[i]) == 0) {
                handle_textchildnode(renderer, doc, node, parent, cache);
                return;
            }
        }

        for (i = 0; uint_keys[i]; i++) {
            if (strcmp(nodename, uint_keys[i]) == 0) {
                xmlChar *value = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                if (value) {
                    long long int val = strtoll((const char*)value, NULL, 10);
                    if (val < 0)
                        return;

                    json_renderer_write_key(renderer, nodename, JSON_RENDERER_FLAGS_NONE);
                    json_renderer_write_uint(renderer, val);
                    xmlFree(value);
                    return ;
                }
                return;
            }
        }

        for (i = 0; text_keys[i]; i++) {
            if (strcmp(nodename, text_keys[i]) == 0) {
                handle_textchildnode(renderer, doc, node, parent, cache);
                return;
            }
        }

        if (strcmp(nodename, "playlist") == 0) {
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);
            json_renderer_write_key(renderer, "playlist", JSON_RENDERER_FLAGS_NONE);
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);

            handle_childs = 1;
            close_after_me = 2;
        } else if (strcmp(nodename, "trackList") == 0) {
            json_renderer_write_key(renderer, "track", JSON_RENDERER_FLAGS_NONE);
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_ARRAY);

            handle_childs = 1;
            close_after_me = 1;
        } else if (strcmp(nodename, "track") == 0) {
            json_renderer_begin(renderer, JSON_ELEMENT_TYPE_OBJECT);

            handle_childs = 1;
            close_after_me = 1;
        } else {
            handled = 0;
        }

        if (handled) {
            if (handle_childs) {
                xmlNodePtr child = node->xmlChildrenNode;
                while (child) {
                    render_node_xspf(renderer, doc, child, node, cache);
                    child = child->next;
                };
            }

            for (; close_after_me; close_after_me--)
                json_renderer_end(renderer);
        }
    }

    if (!handled)
        render_node_generic(renderer, doc, node, parent, cache);
}

static void render_node_generic(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
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
            render_node(renderer, doc, cur, node, cache);
            cur = cur->next;
        } while (cur);
        json_renderer_end(renderer);
    }

    json_renderer_end(renderer);
}


static void render_node(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache)
{
    void (*render)(json_renderer_t *renderer, xmlDocPtr doc, xmlNodePtr node, xmlNodePtr parent, struct xml2json_cache *cache) = NULL;
    xmlChar * workaroundProp = xmlGetProp(node, XMLSTR("xmlns"));

    if (node->ns == cache->ns && !workaroundProp)
        render = cache->render;

    if (render == NULL) {
        const char *href = cache->default_namespace;

        if (node->ns && node->ns->href)
            href = (const char *)node->ns->href;

        if (workaroundProp)
            href = (const char *)workaroundProp;

        if (href) {
            if (strcmp(href, "http://icecast.org/specs/legacyresponse-0.0.1") == 0) {
                render = render_node_legacyresponse;
            } else if (strcmp(href, "http://icecast.org/specs/legacystats-0.0.1") == 0) {
                render = render_node_legacystats;
            } else if (strcmp(href, "http://xspf.org/ns/0/") == 0) {
                render = render_node_xspf;
            }
        }

        if (render == NULL)
            render = render_node_generic;

        cache->ns = node->ns;
        cache->render = render;
    }

    if (workaroundProp)
        xmlFree(workaroundProp);

    render(renderer, doc, node, parent, cache);
}

char * xml2json_render_doc_simple(xmlDocPtr doc, const char *default_namespace)
{
    struct xml2json_cache cache;
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

    memset(&cache, 0, sizeof(cache));
    cache.default_namespace = default_namespace;

    render_node(renderer, doc, xmlroot, NULL, &cache);

    return json_renderer_finish(&renderer);
}
