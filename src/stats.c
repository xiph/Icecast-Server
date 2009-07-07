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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "net/sock.h"

#include "connection.h"

#include "source.h"
#include "admin.h"
#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "xslt.h"
#include "util.h"
#include "fserve.h"
#define CATMODULE "stats"
#include "logging.h"

#if !defined HAVE_ATOLL && defined HAVE_STRTOLL
#define atoll(nptr) strtoll(nptr, (char **)NULL, 10)
#endif

#define VAL_BUFSIZE 20
#define STATS_BLOCK_NORMAL  01

#define STATS_LARGE  CLIENT_FORMAT_BIT

#define STATS_EVENT_SET     0
#define STATS_EVENT_INC     1
#define STATS_EVENT_DEC     2
#define STATS_EVENT_ADD     3
#define STATS_EVENT_SUB     4
#define STATS_EVENT_REMOVE  5
#define STATS_EVENT_HIDDEN  0x80

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

typedef struct _event_listener_tag
{
    int hidden_level;
    char *source;

    /* queue for unwritten stats to stats clients */
    refbuf_t **queue_recent_p;
    unsigned int content_len;

    struct _event_listener_tag *next;
} event_listener_t;


typedef struct _stats_tag
{
    avl_tree *global_tree;
    avl_tree *source_tree;

    /* list of listeners for stats */
    event_listener_t *event_listeners, *listeners_removed;

} stats_t;

static volatile int _stats_running = 0;

static stats_t _stats;
static mutex_t _stats_mutex;


static int _compare_stats(void *a, void *b, void *arg);
static int _compare_source_stats(void *a, void *b, void *arg);
static int _free_stats(void *key);
static int _free_source_stats(void *key);
static stats_node_t *_find_node(avl_tree *tree, const char *name);
static stats_source_t *_find_source(avl_tree *tree, const char *source);
static void process_event (stats_event_t *event);
static void _add_stats_to_stats_client (event_listener_t *listener, const char *fmt, va_list ap);
static void stats_listener_send (int flags, const char *fmt, ...);
static void process_event_unlocked (stats_event_t *event);


/* simple helper function for creating an event */
static void build_event (stats_event_t *event, const char *source, const char *name, const char *value)
{
    event->source = (char *)source;
    event->name = (char *)name;
    event->value = (char *)value;
    event->hidden = STATS_PUBLIC;
    if (source) event->hidden |= STATS_SLAVE;
    if (value)
        event->action = STATS_EVENT_SET;
    else
        event->action = STATS_EVENT_REMOVE;
}


void stats_initialize(void)
{
    if (_stats_running)
        return;

    /* set up global struct */
    _stats.global_tree = avl_tree_new(_compare_stats, NULL);
    _stats.source_tree = avl_tree_new(_compare_source_stats, NULL);

    _stats.event_listeners = NULL;
    _stats.listeners_removed = NULL;

    /* set up global mutex */
    thread_mutex_create(&_stats_mutex);

    _stats_running = 1;

    stats_event_time (NULL, "server_start");

    /* global currently active stats */
    stats_event_hidden (NULL, "clients", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "sources", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "stats", "0", STATS_COUNTERS);
    stats_event (NULL, "listeners", "0");

    /* global accumulating stats */
    stats_event_hidden (NULL, "client_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "source_client_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "source_relay_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "source_total_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "stats_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "listener_connections", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "outgoing_kbitrate", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "stream_kbytes_sent", "0", STATS_COUNTERS);
    stats_event_hidden (NULL, "stream_kbytes_read", "0", STATS_COUNTERS);
}

void stats_shutdown(void)
{
    if(!_stats_running) /* We can't shutdown if we're not running. */
        return;

    _stats_running = 0;

    thread_mutex_destroy(&_stats_mutex);
    avl_tree_free(_stats.source_tree, _free_source_stats);
    avl_tree_free(_stats.global_tree, _free_stats);
}


/* simple name=tag stat create/update */
void stats_event(const char *source, const char *name, const char *value)
{
    stats_event_t event;

    if (value && xmlCheckUTF8 ((unsigned char *)value) == 0)
    {
        WARN3 ("seen non-UTF8 data (%s), probably incorrect metadata (%s, %s)",
                source?source:"global", name, value);
        return;
    }
    build_event (&event, source, name, (char *)value);
    process_event (&event);
}


/* wrapper for stats_event, this takes a charset to convert from */
void stats_event_conv(const char *mount, const char *name, const char *value, const char *charset)
{
    const char *metadata = value;
    xmlBufferPtr conv = xmlBufferCreate ();

    if (charset)
    {
        xmlCharEncodingHandlerPtr handle = xmlFindCharEncodingHandler (charset);

        if (handle)
        {
            xmlBufferPtr raw = xmlBufferCreate ();
            xmlBufferAdd (raw, (const xmlChar *)value, strlen (value));
            if (xmlCharEncInFunc (handle, conv, raw) > 0)
                metadata = (char *)xmlBufferContent (conv);
            xmlBufferFree (raw);
            xmlCharEncCloseFunc (handle);
        }
        else
            WARN1 ("No charset found for \"%s\"", charset);
    }

    stats_event (mount, name, metadata);
    xmlBufferFree (conv);
}

/* make stat hidden (non-zero). name can be NULL if it applies to a whole
 * source stats tree. */
void stats_event_hidden (const char *source, const char *name, const char *value, int hidden)
{
    stats_event_t event;

    build_event (&event, source, name, value);
    event.hidden = hidden;
    if (value)
        event.action |= STATS_EVENT_HIDDEN;
    else
        event.action = STATS_EVENT_HIDDEN;
    process_event (&event);
}

/* printf style formatting for stat create/update */
void stats_event_args(const char *source, char *name, char *format, ...)
{
    va_list val;
    int ret;
    char buf[1024];

    if (name == NULL)
        return;
    va_start(val, format);
    ret = vsnprintf(buf, sizeof (buf), format, val);
    va_end(val);

    if (ret < 0 || (unsigned int)ret >= sizeof (buf))
    {
        WARN2 ("problem with formatting %s stat %s",
                source==NULL ? "global" : source, name);
        return;
    }
    stats_event(source, name, buf);
}

static char *_get_stats(const char *source, const char *name)
{
    stats_node_t *stats = NULL;
    stats_source_t *src = NULL;
    char *value = NULL;

    thread_mutex_lock(&_stats_mutex);

    if (source == NULL) {
        stats = _find_node(_stats.global_tree, name);
    } else {
        src = _find_source(_stats.source_tree, source);
        if (src) {
            stats = _find_node(src->stats_tree, name);
        }
    }

    if (stats) value = (char *)strdup(stats->value);

    thread_mutex_unlock(&_stats_mutex);

    return value;
}

char *stats_get_value(const char *source, const char *name)
{
    return(_get_stats(source, name));
}

/* increase the value in the provided stat by 1 */
void stats_event_inc(const char *source, const char *name)
{
    stats_event_t event;
    char buffer[VAL_BUFSIZE];
    build_event (&event, source, name, buffer);
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    event.action = STATS_EVENT_INC;
    process_event (&event);
}

void stats_event_add(const char *source, const char *name, unsigned long value)
{
    stats_event_t event;
    char buffer [VAL_BUFSIZE];

    build_event (&event, source, name, buffer);
    snprintf (buffer, VAL_BUFSIZE, "%ld", value);
    event.action = STATS_EVENT_ADD;
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    process_event (&event);
}

void stats_event_sub(const char *source, const char *name, unsigned long value)
{
    stats_event_t event;
    char buffer[VAL_BUFSIZE];
    build_event (&event, source, name, buffer);
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    snprintf (buffer, VAL_BUFSIZE, "%ld", value);
    event.action = STATS_EVENT_SUB;
    process_event (&event);
}

/* decrease the value in the provided stat by 1 */
void stats_event_dec(const char *source, const char *name)
{
    stats_event_t event;
    char buffer[VAL_BUFSIZE];
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    build_event (&event, source, name, buffer);
    event.action = STATS_EVENT_DEC;
    process_event (&event);
}

/* note: you must call this function only when you have exclusive access
** to the avl_tree
*/
static stats_node_t *_find_node(avl_tree *stats_tree, const char *name)
{
    stats_node_t *stats;
    avl_node *node;
    int cmp;

    /* get the root node */
    node = stats_tree->root->right;
    
    while (node) {
        stats = (stats_node_t *)node->key;
        cmp = strcmp(name, stats->name);
        if (cmp < 0) 
            node = node->left;
        else if (cmp > 0)
            node = node->right;
        else
            return stats;
    }
    
    /* didn't find it */
    return NULL;
}

/* note: you must call this function only when you have exclusive access
** to the avl_tree
*/
static stats_source_t *_find_source(avl_tree *source_tree, const char *source)
{
    stats_source_t *stats;
    avl_node *node;
    int cmp;

    /* get the root node */
    node = source_tree->root->right;
    while (node) {
        stats = (stats_source_t *)node->key;
        cmp = strcmp(source, stats->source);
        if (cmp < 0)
            node = node->left;
        else if (cmp > 0)
            node = node->right;
        else
            return stats;
    }

    /* didn't find it */
    return NULL;
}


/* helper to apply specialised changes to a stats node */
static void modify_node_event (stats_node_t *node, stats_event_t *event)
{
    if (node == NULL || event == NULL)
        return;
    if (event->action & STATS_EVENT_HIDDEN)
    {
        node->hidden = event->hidden;
        event->action &= ~STATS_EVENT_HIDDEN;
    }
    if (event->action != STATS_EVENT_SET)
    {
        int64_t value = 0;

        switch (event->action)
        {
            case STATS_EVENT_INC:
                value = atoll (node->value)+1;
                break;
            case STATS_EVENT_DEC:
                value = atoll (node->value)-1;
                break;
            case STATS_EVENT_ADD:
                value = atoll (node->value) + atoll (event->value);
                break;
            case STATS_EVENT_SUB:
                value = atoll (node->value) - atoll (event->value);
                break;
            default:
                break;
        }
        snprintf (event->value, VAL_BUFSIZE, "%" PRId64, value);
    }
    if (node->value)
    {
        free (node->value);
        node->value = strdup (event->value);
    }
    DEBUG3 ("update \"%s\" %s (%s)", event->source?event->source:"global", node->name, node->value);
}


static void process_global_event (stats_event_t *event)
{
    stats_node_t *node = NULL;

    /* DEBUG3("global event %s %s %d", event->name, event->value, event->action); */
    if (event->action == STATS_EVENT_REMOVE)
    {
        /* we're deleting */
        node = _find_node(_stats.global_tree, event->name);
        if (node != NULL)
        {
            stats_listener_send (node->hidden, "DELETE global %s\n", event->name);
            avl_delete(_stats.global_tree, (void *)node, _free_stats);
        }
        return;
    }
    node = _find_node(_stats.global_tree, event->name);
    if (node)
    {
        modify_node_event (node, event);
        stats_listener_send (node->hidden, "EVENT global %s %s\n", node->name, node->value);
    }
    else
    {
        /* add node */
        node = (stats_node_t *)calloc(1, sizeof(stats_node_t));
        node->name = (char *)strdup(event->name);
        node->value = (char *)strdup(event->value);
        node->hidden = event->hidden;

        avl_insert(_stats.global_tree, (void *)node);
        stats_listener_send (node->hidden, "EVENT global %s %s\n", event->name, event->value);
    }
}


static void process_source_event (stats_event_t *event)
{
    stats_source_t *snode = _find_source(_stats.source_tree, event->source);
    stats_node_t *node = NULL;

    if (snode == NULL)
    {
        if (event->action == STATS_EVENT_REMOVE)
            return;
        snode = (stats_source_t *)calloc(1,sizeof(stats_source_t));
        if (snode == NULL)
            return;
        DEBUG1 ("new source stat %s", event->source);
        snode->source = (char *)strdup(event->source);
        snode->stats_tree = avl_tree_new(_compare_stats, NULL);
        snode->hidden = STATS_SLAVE|STATS_GENERAL|STATS_HIDDEN;

        avl_insert(_stats.source_tree, (void *)snode);
    }
    if (event->name)
    {
        node = _find_node (snode->stats_tree, event->name);
        if (node == NULL)
        {
            if (event->action == STATS_EVENT_REMOVE)
                return;
            /* adding node */
            if (event->value)
            {
                DEBUG3 ("new node on %s \"%s\" (%s)", event->source, event->name, event->value);
                node = (stats_node_t *)calloc(1,sizeof(stats_node_t));
                node->name = (char *)strdup(event->name);
                node->value = (char *)strdup(event->value);
                node->hidden = event->hidden;
                if (snode->hidden & STATS_HIDDEN)
                    node->hidden |= STATS_HIDDEN;
                stats_listener_send (node->hidden, "EVENT %s %s %s\n", event->source, event->name, event->value);
                avl_insert(snode->stats_tree, (void *)node);
            }
            return;
        }
        if (event->action == STATS_EVENT_REMOVE)
        {
            DEBUG1 ("delete node %s", event->name);
            stats_listener_send (node->hidden, "DELETE %s %s\n", event->source, event->name);
            avl_delete(snode->stats_tree, (void *)node, _free_stats);
            return;
        }
        modify_node_event (node, event);
        stats_listener_send (node->hidden, "EVENT %s %s %s\n", event->source, node->name, node->value);
        return;
    }
    /* change source hidden status */
    if (event->action & STATS_EVENT_HIDDEN)
    {
        avl_node *node = avl_get_first (snode->stats_tree);
        int visible = 0;

        if ((event->hidden&STATS_HIDDEN) == (snode->hidden&STATS_HIDDEN))
            return;
        if (snode->hidden & STATS_HIDDEN)
        {
            snode->hidden &= ~STATS_HIDDEN;
            stats_listener_send (snode->hidden, "NEW %s\n", snode->source);
            visible = 1;
        }
        else
        {
            stats_listener_send (snode->hidden, "DELETE %s\n", snode->source);
            snode->hidden |= STATS_HIDDEN;
        }
        while (node)
        {
            stats_node_t *stats = (stats_node_t*)node->key;
            if (visible)
            {
                stats->hidden &= ~STATS_HIDDEN;
                stats_listener_send (stats->hidden, "EVENT %s %s %s\n", snode->source, stats->name, stats->value);
            }
            else
                stats->hidden |= STATS_HIDDEN;
            node = avl_get_next (node);
        }
        return;
    }
    if (event->action == STATS_EVENT_REMOVE)
        avl_delete(_stats.source_tree, (void *)snode, _free_source_stats);
}


void stats_event_time (const char *mount, const char *name)
{
    time_t now = time(NULL);
    struct tm local;
    char buffer[100];

    localtime_r (&now, &local);
    strftime (buffer, sizeof (buffer), ICECAST_TIME_FMT, &local);
    stats_event_hidden (mount, name, buffer, STATS_GENERAL);
}


static int stats_listeners_send (client_t *client)
{
    int loop = 8;
    int ret = 0;
    event_listener_t *listener = client->shared_data;

    if (client->con->error)
        return -1;
    if (client->flags & STATS_LARGE)
        loop = 4;
    else
        if (listener->content_len > 50000)
        {
            WARN1 ("dropping stats client, %ld in queue", listener->content_len);
            return -1;
        }
    client->schedule_ms = client->worker->time_ms + 100;
    thread_mutex_lock(&_stats_mutex);
    while (loop)
    {
        refbuf_t *refbuf = client->refbuf;

        if (refbuf == NULL)
            break;
        if ((client->flags & STATS_LARGE) && (refbuf->flags & STATS_BLOCK_NORMAL))
            client->flags &= ~STATS_LARGE;

        ret = format_generic_write_to_client (client);
        if (ret > 0)
            listener->content_len -= ret;
        if (client->pos == refbuf->len)
        {
            client->refbuf = refbuf->next;
            refbuf->next = NULL;
            refbuf_release (refbuf);
            client->pos = 0;
            if (client->refbuf == NULL)
            {
                listener->queue_recent_p = &client->refbuf;
                break;
            }
        }
        else if (ret < 4096)
            break; /* short write, so stop for now */
        loop--;
    }
    thread_mutex_unlock(&_stats_mutex);
    if (loop == 0)
        client->schedule_ms -= 100;
    return 0;
}


static void clear_stats_queue (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    while (refbuf)
    {
        refbuf_t *to_go = refbuf;
        refbuf = to_go->next;
        if (to_go->_count != 1) DEBUG1 ("odd count for stats %d", to_go->_count);
        to_go->next = NULL;
        refbuf_release (to_go);
    }
    client->refbuf = NULL;
}


static void stats_listener_send (int hidden_level, const char *fmt, ...)
{
    va_list ap;
    event_listener_t *listener = _stats.event_listeners,
                     **trail = &_stats.event_listeners;

    va_start(ap, fmt);

    while (listener)
    {
        if (listener->hidden_level & hidden_level)
            _add_stats_to_stats_client (listener, fmt, ap);
        trail = &listener->next;
        listener = listener->next;
    }
    va_end(ap);
}

void stats_global (ice_config_t *config)
{
    stats_event_hidden (NULL, "server_id", config->server_id, STATS_GENERAL);
    stats_event_hidden (NULL, "host", config->hostname, STATS_GENERAL);
    stats_event (NULL, "location", config->location);
    stats_event (NULL, "admin", config->admin);
#if 0
    /* restart a master stats connection */
    config->master = calloc (1, sizeof ice_master_details);
    config->master->hostname = xmlCharStrdup ("127.0.0.1");
    config->master->port = 8000;
    config->master->username = xmlCharStrdup ("relay");
    config->master->password = xmlCharStrdup ("relayme");
    _stats.sock = sock_connect_wto_bind (server, port, bind, 10);
#endif
}

static void process_event_unlocked (stats_event_t *event)
{
    /* check if we are dealing with a global or source event */
    if (event->source == NULL)
        process_global_event (event);
    else
        process_source_event (event);
}

static void process_event (stats_event_t *event)
{
    if (event == NULL)
        return;
    thread_mutex_lock (&_stats_mutex);
    process_event_unlocked (event);
    thread_mutex_unlock (&_stats_mutex);
}


static int _append_to_bufferv (refbuf_t *refbuf, int max_len, const char *fmt, va_list ap)
{
    char *buf = (char*)refbuf->data + refbuf->len;
    int len = max_len - refbuf->len;
    int ret;
    va_list vl;

    va_copy (vl, ap);
    if (len <= 0)
        return -1;
    ret = vsnprintf (buf, len, fmt, vl);
    if (ret < 0 || ret >= len)
        return -1;
    refbuf->len += ret;
    return 0;
}

static int _append_to_buffer (refbuf_t *refbuf, int max_len, const char *fmt, ...)
{
    int ret;
    va_list va;

    va_start (va, fmt);
    ret = _append_to_bufferv (refbuf, max_len, fmt, va);
    va_end(va);
    return ret;
}


static void _add_node_to_stats_client (event_listener_t *listener, refbuf_t *refbuf)
{
    if (refbuf->len)
    {
        *listener->queue_recent_p = refbuf;
        listener->queue_recent_p = &refbuf->next;
        listener->content_len += refbuf->len;
    }
}


static void _add_stats_to_stats_client (event_listener_t *listener,const char *fmt, va_list ap)
{
    unsigned int size = 50;
    while (size < 300)
    {
        refbuf_t *refbuf = refbuf_new (size);
        refbuf->len = 0;

        if (_append_to_bufferv (refbuf, size, fmt, ap) == 0)
        {
            refbuf->flags |= STATS_BLOCK_NORMAL;
            _add_node_to_stats_client (listener, refbuf);
            return;
        }
        refbuf_release (refbuf);
        size += 100;
    }
}


static xmlNodePtr _dump_stats_to_doc (xmlNodePtr root, const char *show_mount, int hidden)
{
    avl_node *avlnode;
    xmlNodePtr ret = NULL;

    thread_mutex_lock(&_stats_mutex);
    /* general stats first */
    avlnode = avl_get_first(_stats.global_tree);
    while (avlnode)
    {
        stats_node_t *stat = avlnode->key;
        if (stat->hidden & hidden)
            xmlNewTextChild (root, NULL, XMLSTR(stat->name), XMLSTR(stat->value));
        avlnode = avl_get_next (avlnode);
    }
    /* now per mount stats */
    avlnode = avl_get_first(_stats.source_tree);
    while (avlnode)
    {
        stats_source_t *source = (stats_source_t *)avlnode->key;
        if (((hidden&STATS_HIDDEN) || (source->hidden&STATS_HIDDEN) == (hidden&STATS_HIDDEN)) &&
                (show_mount == NULL || strcmp (show_mount, source->source) == 0))
        {
            avl_node *avlnode2 = avl_get_first (source->stats_tree);
            xmlNodePtr xmlnode = xmlNewTextChild (root, NULL, XMLSTR("source"), NULL);

            xmlSetProp (xmlnode, XMLSTR("mount"), XMLSTR(source->source));
            if (ret == NULL)
                ret = xmlnode;
            while (avlnode2)
            {
                stats_node_t *stat = avlnode2->key;
                if ((hidden&STATS_HIDDEN) || (stat->hidden&STATS_HIDDEN) == (hidden&STATS_HIDDEN))
                    xmlNewTextChild (xmlnode, NULL, XMLSTR(stat->name), XMLSTR(stat->value));
                avlnode2 = avl_get_next (avlnode2);
            }
        }
        avlnode = avl_get_next (avlnode);
    }
    thread_mutex_unlock(&_stats_mutex);
    return ret;
}


/* factoring out code for stats loops
** this function copies all stats to queue, and registers 
*/
static void _register_listener (event_listener_t *listener)
{
    avl_node *node;
    stats_event_t stats_count;
    refbuf_t *refbuf;
    size_t size = 8192;
    char buffer[20];

    build_event (&stats_count, NULL, "stats_connections", buffer);
    stats_count.action = STATS_EVENT_INC;
    process_event_unlocked (&stats_count);

    /* first we fill our queue with the current stats */
    refbuf = refbuf_new (size);
    refbuf->len = 0;

    /* the global stats */
    node = avl_get_first(_stats.global_tree);
    while (node)
    {
        stats_node_t *stat = node->key;

        if (stat->hidden & listener->hidden_level)
        {
            if (_append_to_buffer (refbuf, size, "EVENT global %s %s\n", stat->name, stat->value) < 0)
            {
                _add_node_to_stats_client (listener, refbuf);
                refbuf = refbuf_new (size);
                refbuf->len = 0;
                continue;
            }
        }
        node = avl_get_next(node);
    }
    /* now the stats for each source */
    node = avl_get_first(_stats.source_tree);
    while (node)
    {
        avl_node *node2;
        stats_source_t *snode = (stats_source_t *)node->key;
        if (snode->hidden & listener->hidden_level)
        {
            if (_append_to_buffer (refbuf, size, "NEW %s\n", snode->source) < 0)
            {
                _add_node_to_stats_client (listener, refbuf);
                refbuf = refbuf_new (size);
                refbuf->len = 0;
                continue;
            }
        }
        node = avl_get_next(node);
        node2 = avl_get_first(snode->stats_tree);
        while (node2)
        {
            stats_node_t *stat = node2->key;
            if (stat->hidden & listener->hidden_level)
            {
                if (_append_to_buffer (refbuf, size, "EVENT %s %s %s\n", snode->source, stat->name, stat->value) < 0)
                {
                    _add_node_to_stats_client (listener, refbuf);
                    refbuf = refbuf_new (size);
                    refbuf->len = 0;
                    continue;
                }
            }
            node2 = avl_get_next (node2);
        }
    }
    _add_node_to_stats_client (listener, refbuf);

    /* now we register to receive future event notices */
    listener->next = _stats.event_listeners;
    _stats.event_listeners = listener;
}


static void stats_client_release (client_t *client)
{
    event_listener_t *listener = _stats.event_listeners,
                     **trail = &_stats.event_listeners;
    while (listener)
    {
        if (listener == client->shared_data)
        {
            stats_event_t stats_count;
            char buffer [20];

            *trail = listener->next;
            clear_stats_queue (client);
            free (listener->source);
            free (listener);
            client_destroy (client);
            build_event (&stats_count, NULL, "stats_connections", buffer);
            stats_count.action = STATS_EVENT_DEC;
            process_event_unlocked (&stats_count);
            return;
        }
        trail = &listener->next;
        listener = listener->next;
    }
}


struct _client_functions stats_client_send_ops =
{
    stats_listeners_send,
    stats_client_release
};

void stats_add_listener (client_t *client, int hidden_level)
{
    event_listener_t *listener = calloc (1, sizeof (event_listener_t));
    listener->hidden_level = hidden_level;

    client->respcode = 200;
    client->ops = &stats_client_send_ops;
    client->shared_data = listener;
    client_set_queue (client, NULL);
    client->flags |= CLIENT_ACTIVE;
    client->refbuf = refbuf_new (100);
    snprintf (client->refbuf->data, 100,
            "HTTP/1.0 200 OK\r\ncapability: streamlist\r\n\r\n");
    client->refbuf->len = strlen (client->refbuf->data);
    listener->content_len = client->refbuf->len;
    listener->queue_recent_p = &client->refbuf->next;

    client->flags |= STATS_LARGE;
    thread_mutex_lock(&_stats_mutex);
    _register_listener (listener);
    thread_mutex_unlock(&_stats_mutex);
}

void stats_transform_xslt(client_t *client, const char *uri)
{
    xmlDocPtr doc;
    char *xslpath = util_get_path_from_normalised_uri (uri, 0);
    const char *mount = httpp_get_query_param (client->parser, "mount");

    doc = stats_get_xml (STATS_PUBLIC, mount);

    xslt_transform(doc, xslpath, client);

    xmlFreeDoc(doc);
    free (xslpath);
}

xmlDocPtr stats_get_xml (int hidden, const char *show_mount)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    doc = xmlNewDoc (XMLSTR("1.0"));
    node = xmlNewDocNode (doc, NULL, XMLSTR("icestats"), NULL);
    xmlDocSetRootElement(doc, node);

    node = _dump_stats_to_doc (node, show_mount, hidden);

    if (show_mount && node)
    {
		source_t *source;
        /* show each listener */
        avl_tree_rlock (global.source_tree);
        source = source_find_mount_raw (show_mount);

        if (source)
            admin_source_listeners (source, node);

        avl_tree_unlock (global.source_tree);
    }
    return doc;
}

static int _compare_stats(void *arg, void *a, void *b)
{
    stats_node_t *nodea = (stats_node_t *)a;
    stats_node_t *nodeb = (stats_node_t *)b;

    return strcmp(nodea->name, nodeb->name);
}

static int _compare_source_stats(void *arg, void *a, void *b)
{
    stats_source_t *nodea = (stats_source_t *)a;
    stats_source_t *nodeb = (stats_source_t *)b;

    return strcmp(nodea->source, nodeb->source);
}

static int _free_stats(void *key)
{
    stats_node_t *node = (stats_node_t *)key;
    free(node->value);
    free(node->name);
    free(node);
    
    return 1;
}

static int _free_source_stats(void *key)
{
    stats_source_t *node = (stats_source_t *)key;
    stats_listener_send (node->hidden, "DELETE %s\n", node->source);
    DEBUG1 ("delete source node %s", node->source);
    avl_tree_free(node->stats_tree, _free_stats);
    free(node->source);
    free(node);

    return 1;
}


/* return a list of blocks which contain lines of text. Each line is a mountpoint
 * reference that a slave will use for relaying.  The prepend setting is to indicate
 * if some something else needs to be added to each line.
 */
refbuf_t *stats_get_streams (int prepend)
{
#define STREAMLIST_BLKSIZE  4096
    avl_node *node;
    unsigned int remaining = STREAMLIST_BLKSIZE, prelen;
    refbuf_t *start = refbuf_new (remaining), *cur = start;
    const char *pre = "";
    char *buffer = cur->data;

    if (prepend)
        pre = "/admin/streams?mount=";
    prelen = strlen (pre);

    /* now the stats for each source */
    thread_mutex_lock (&_stats_mutex);
    node = avl_get_first(_stats.source_tree);
    while (node)
    {
        int ret;
        stats_source_t *source = (stats_source_t *)node->key;

        if (source->hidden & STATS_SLAVE)
        {
            if (remaining <= strlen (source->source) + prelen + 3)
            {
                cur->len = STREAMLIST_BLKSIZE - remaining;
                cur->next = refbuf_new (STREAMLIST_BLKSIZE);
                remaining = STREAMLIST_BLKSIZE;
                cur = cur->next;
                buffer = cur->data;
            }
            ret = snprintf (buffer, remaining, "%s%s\r\n", pre, source->source);
            if (ret > 0)
            {
                buffer += ret;
                remaining -= ret;
            }
        }
        node = avl_get_next(node);
    }
    thread_mutex_unlock (&_stats_mutex);
    cur->len = STREAMLIST_BLKSIZE - remaining;
    return start;
}



/* This removes any source stats from virtual mountpoints, ie mountpoints
 * where no source_t exists. This function requires the global sources lock
 * to be held before calling.
 */
void stats_clear_virtual_mounts (void)
{
    avl_node *snode;

    thread_mutex_lock (&_stats_mutex);
    snode = avl_get_first(_stats.source_tree);
    while (snode)
    {
        stats_source_t *src = (stats_source_t *)snode->key;
        source_t *source = source_find_mount_raw (src->source);

        if (source == NULL)
        {
            /* no source_t is reserved so remove them now */
            snode = avl_get_next (snode);
            avl_delete (_stats.source_tree, src, _free_source_stats);
            continue;
        }

        snode = avl_get_next (snode);
    }
    thread_mutex_unlock (&_stats_mutex);
}


void stats_global_calc (void)
{
    event_listener_t *listener;
    stats_event_t event;
    char buffer [VAL_BUFSIZE];

    build_event (&event, NULL, "outgoing_kbitrate", buffer);
    event.hidden = STATS_COUNTERS|STATS_HIDDEN;

    thread_mutex_lock (&_stats_mutex);
    snprintf (buffer, sizeof(buffer), "%" PRIu64,
            (int64_t)global_getrate_avg (global.out_bitrate) * 8 / 1000);
    process_event_unlocked (&event);
    /* retrieve the list of closing down clients */
    listener = _stats.listeners_removed;
    _stats.listeners_removed = NULL;
    thread_mutex_unlock (&_stats_mutex);
}

