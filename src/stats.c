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

#include <thread/thread.h>
#include <avl/avl.h>
#include <httpp/httpp.h>
#include <net/sock.h>

#include "connection.h"

#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "xslt.h"
#define CATMODULE "stats"
#include "logging.h"

#ifdef _WIN32
#define vsnprintf _vsnprintf
#define snprintf _snprintf
#endif

#define STATS_EVENT_SET     0
#define STATS_EVENT_INC     1
#define STATS_EVENT_DEC     2
#define STATS_EVENT_ADD     3
#define STATS_EVENT_REMOVE  4
#define STATS_EVENT_HIDDEN  5

typedef struct _event_listener_tag
{
    stats_event_t **queue;
    mutex_t *mutex;

    struct _event_listener_tag *next;
} event_listener_t;

volatile static int _stats_running = 0;
static thread_type *_stats_thread_id;
volatile static int _stats_threads = 0;

static stats_t _stats;
static mutex_t _stats_mutex;

volatile static stats_event_t *_global_event_queue;
mutex_t _global_event_mutex;

volatile static event_listener_t *_event_listeners;


static void *_stats_thread(void *arg);
static int _compare_stats(void *a, void *b, void *arg);
static int _compare_source_stats(void *a, void *b, void *arg);
static int _free_stats(void *key);
static int _free_source_stats(void *key);
static void _add_event_to_queue(stats_event_t *event, stats_event_t **queue);
static stats_node_t *_find_node(avl_tree *tree, char *name);
static stats_source_t *_find_source(avl_tree *tree, char *source);
static void _free_event(stats_event_t *event);


/* simple helper function for creating an event */
static stats_event_t *build_event (const char *source, const char *name, const char *value)
{
    stats_event_t *event;

    event = (stats_event_t *)calloc(1, sizeof(stats_event_t));
    if (event)
    {
        if (source)
            event->source = (char *)strdup(source);
        if (name)
            event->name = (char *)strdup(name);
        if (value)
            event->value = (char *)strdup(value);
        else
            event->action = STATS_EVENT_REMOVE;
    }
    return event;
}

static void queue_global_event (stats_event_t *event)
{
    thread_mutex_lock(&_global_event_mutex);
    if (_global_event_queue == NULL)
    {
        _global_event_queue = event;
    }
    else
    {
        stats_event_t *node = (stats_event_t *)_global_event_queue;
        while (node->next)
            node = node->next;
        node->next = event;
    }
    /* DEBUG3("event added (%s, %s, %s)", event->source,
         event->name, event->value); */
    thread_mutex_unlock(&_global_event_mutex);
}

void stats_initialize()
{
    _event_listeners = NULL;

    /* set up global struct */
    _stats.global_tree = avl_tree_new(_compare_stats, NULL);
    _stats.source_tree = avl_tree_new(_compare_source_stats, NULL);

    /* set up global mutex */
    thread_mutex_create("stats", &_stats_mutex);

    /* set up stats queues */
    _global_event_queue = NULL;
    thread_mutex_create("stats_global_event", &_global_event_mutex);

    /* fire off the stats thread */
    _stats_running = 1;
    _stats_thread_id = thread_create("Stats Thread", _stats_thread, NULL, THREAD_ATTACHED);
}

void stats_shutdown()
{
    int n;
    stats_event_t *event, *next;

    if(!_stats_running) /* We can't shutdown if we're not running. */
        return;

    /* wait for thread to exit */
    _stats_running = 0;
    thread_join(_stats_thread_id);

    /* wait for other threads to shut down */
    do {
        thread_sleep(300000);
        thread_mutex_lock(&_stats_mutex);
        n = _stats_threads;
        thread_mutex_unlock(&_stats_mutex);
    } while (n > 0);
    INFO0("stats thread finished");

    /* free the queues */

    /* destroy the queue mutexes */
    thread_mutex_destroy(&_global_event_mutex);

    thread_mutex_destroy(&_stats_mutex);
    avl_tree_free(_stats.source_tree, _free_source_stats);
    avl_tree_free(_stats.global_tree, _free_stats);

    event = (stats_event_t *)_global_event_queue;
    while(event) {
        if(event->source)
            free(event->source);
        if(event->value)
            free(event->value);
        if(event->name)
            free(event->name);
        next = event->next;
        free(event);
        event = next;
    }
}

stats_t *stats_get_stats()
{
    /* lock global stats
    
     copy stats

     unlock global stats

     return copied stats */

    return NULL;
}

/* simple name=tag stat create/update */
void stats_event(const char *source, const char *name, const char *value)
{
    stats_event_t *event;

    event = build_event (source, name, value);
    if (event)
        queue_global_event (event);
}

/* make stat hidden (non-zero). name can be NULL if it applies to a whole
 * source stats tree. */
void stats_event_hidden (const char *source, const char *name, int hidden)
{
    stats_event_t *event;
    const char *str = NULL;

    if (hidden)
        str = "";
    event = build_event (source, name, str);
    if (event)
    {
        event->action = STATS_EVENT_HIDDEN;
        queue_global_event (event);
    }
}

/* printf style formatting for stat create/update */
void stats_event_args(const char *source, char *name, char *format, ...)
{
    char buf[1024];
    va_list val;
    int ret;

    if (name == NULL)
        return;
    va_start(val, format);
    ret = vsnprintf(buf, 1024, format, val);
    va_end(val);

    if (ret < 0 || (unsigned int)ret >= sizeof (buf))
        WARN2 ("problem with formatting %s stat %s",
                source==NULL ? "global" : source, name);
    else
        stats_event(source, name, buf);
}

static char *_get_stats(char *source, char *name)
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

char *stats_get_value(char *source, char *name)
{
    return(_get_stats(source, name));
}

/* increase the value in the provided stat by 1 */
void stats_event_inc(const char *source, const char *name)
{
    stats_event_t *event = build_event (source, name, NULL);
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    if (event)
    {
        event->action = STATS_EVENT_INC;
        queue_global_event (event);
    }
}

void stats_event_add(const char *source, const char *name, unsigned long value)
{
    stats_event_t *event = build_event (source, name, NULL);
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    if (event)
    {
        event->value = malloc (16);
        snprintf (event->value, 16, "%ld", value);
        event->action = STATS_EVENT_ADD;
        queue_global_event (event);
    }
}

/* decrease the value in the provided stat by 1 */
void stats_event_dec(const char *source, const char *name)
{
    /* DEBUG2("%s on %s", name, source==NULL?"global":source); */
    stats_event_t *event = build_event (source, name, NULL);
    if (event)
    {
        event->action = STATS_EVENT_DEC;
        queue_global_event (event);
    }
}

/* note: you must call this function only when you have exclusive access
** to the avl_tree
*/
static stats_node_t *_find_node(avl_tree *stats_tree, char *name)
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
static stats_source_t *_find_source(avl_tree *source_tree, char *source)
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

static stats_event_t *_copy_event(stats_event_t *event)
{
    stats_event_t *copy = (stats_event_t *)calloc(1, sizeof(stats_event_t));
    if (event->source) 
        copy->source = (char *)strdup(event->source);
    else
        copy->source = NULL;
    if (event->name)
        copy->name = (char *)strdup(event->name);
    if (event->value)
        copy->value = (char *)strdup(event->value);
    else
        copy->value = NULL;
    copy->hidden = event->hidden;
    copy->next = NULL;

    return copy;
}


/* helper to apply specialised changes to a stats node */
static void modify_node_event (stats_node_t *node, stats_event_t *event)
{
    char *str;
    if (event->action == STATS_EVENT_HIDDEN)
    {
        if (event->value)
            node->hidden = 1;
        else
            node->hidden = 0;
        return;
    }
    if (event->action != STATS_EVENT_SET)
    {
        int value = 0;

        switch (event->action)
        {
            case STATS_EVENT_INC:
                value = atoi (node->value)+1;
                break;
            case STATS_EVENT_DEC:
                value = atoi (node->value)-1;
                break;
            case STATS_EVENT_ADD:
                value = atoi (node->value)+atoi (event->value);
                break;
            default:
                break;
        }
        str = malloc (16);
        snprintf (str, 16, "%d", value);
    }
    else
        str = (char *)strdup (event->value);
    free (node->value);
    node->value = str;
    DEBUG2 ("update node %s (%s)", node->name, node->value);
}


static void process_global_event (stats_event_t *event)
{
    stats_node_t *node;

    /* DEBUG3("global event %s %s %d", event->name, event->value, event->action); */
    if (event->action == STATS_EVENT_REMOVE)
    {
        /* we're deleting */
        node = _find_node(_stats.global_tree, event->name);
        if (node != NULL)
            avl_delete(_stats.global_tree, (void *)node, _free_stats);
        return;
    }
    node = _find_node(_stats.global_tree, event->name);
    if (node)
    {
        modify_node_event (node, event);
    }
    else
    {
        /* add node */
        node = (stats_node_t *)calloc(1, sizeof(stats_node_t));
        node->name = (char *)strdup(event->name);
        node->value = (char *)strdup(event->value);

        avl_insert(_stats.global_tree, (void *)node);
    }
}


static void process_source_event (stats_event_t *event)
{
    stats_source_t *snode = _find_source(_stats.source_tree, event->source);
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
        if (event->action == STATS_EVENT_HIDDEN)
            snode->hidden = 1;
        else
            snode->hidden = 0;

        avl_insert(_stats.source_tree, (void *)snode);
    }
    if (event->name)
    {
        stats_node_t *node = _find_node(snode->stats_tree, event->name);
        if (node == NULL)
        {
            if (event->action == STATS_EVENT_REMOVE)
                return;
            /* adding node */
            if (event->value)
            {
                DEBUG2 ("new node %s (%s)", event->name, event->value);
                node = (stats_node_t *)calloc(1,sizeof(stats_node_t));
                node->name = (char *)strdup(event->name);
                node->value = (char *)strdup(event->value);
                node->hidden = snode->hidden;

                avl_insert(snode->stats_tree, (void *)node);
            }
            return;
        }
        if (event->action == STATS_EVENT_REMOVE)
        {
            DEBUG1 ("delete node %s", event->name);
            avl_delete(snode->stats_tree, (void *)node, _free_stats);
            return;
        }
        modify_node_event (node, event);
        return;
    }
    if (event->action == STATS_EVENT_HIDDEN)
    {
        avl_node *node = avl_get_first (snode->stats_tree);

        if (event->value)
            snode->hidden = 1;
        else
            snode->hidden = 0;
        while (node)
        {
            stats_node_t *stats = (stats_node_t*)node->key;
            stats->hidden = snode->hidden;
            node = avl_get_next (node);
        }
        return;
    }
    if (event->action == STATS_EVENT_REMOVE)
    {
        DEBUG1 ("delete source node %s", event->source);
        avl_delete(_stats.source_tree, (void *)snode, _free_source_stats);
    }
}


static void *_stats_thread(void *arg)
{
    stats_event_t *event;
    stats_event_t *copy;
    event_listener_t *listener;

    stats_event (NULL, "server", ICECAST_VERSION_STRING);

    /* global currently active stats */
    stats_event (NULL, "clients", "0");
    stats_event (NULL, "connections", "0");
    stats_event (NULL, "sources", "0");
    stats_event (NULL, "stats", "0");

    /* global accumulating stats */
    stats_event (NULL, "client_connections", "0");
    stats_event (NULL, "source_client_connections", "0");
    stats_event (NULL, "source_relay_connections", "0");
    stats_event (NULL, "source_total_connections", "0");
    stats_event (NULL, "stats_connections", "0");

    INFO0 ("stats thread started");
    while (_stats_running) {
        if (_global_event_queue != NULL) {
            /* grab the next event from the queue */
            thread_mutex_lock(&_global_event_mutex);
            event = (stats_event_t *)_global_event_queue;
            _global_event_queue = event->next;
            thread_mutex_unlock(&_global_event_mutex);

            event->next = NULL;
            thread_mutex_unlock(&_global_event_mutex);

            thread_mutex_lock(&_stats_mutex);

            /* check if we are dealing with a global or source event */
            if (event->source == NULL)
                process_global_event (event);
            else
                process_source_event (event);
            
            /* now we have an event that's been processed into the running stats */
            /* this event should get copied to event listeners' queues */
            listener = (event_listener_t *)_event_listeners;
            while (listener) {
                copy = _copy_event(event);
                thread_mutex_lock(listener->mutex);
                _add_event_to_queue(copy, listener->queue);
                thread_mutex_unlock(listener->mutex);

                listener = listener->next;
            }

            /* now we need to destroy the event */
            _free_event(event);

            thread_mutex_unlock(&_stats_mutex);
            continue;
        }

        thread_sleep(300000);
    }

    return NULL;
}

/* you must have the _stats_mutex locked here */
static void _register_listener(stats_event_t **queue, mutex_t *mutex)
{
    event_listener_t *node;
    event_listener_t *evli = (event_listener_t *)malloc(sizeof(event_listener_t));

    evli->queue = queue;
    evli->mutex = mutex;
    evli->next = NULL;

    if (_event_listeners == NULL) {
        _event_listeners = evli;
    } else {
        node = (event_listener_t *)_event_listeners;
        while (node->next) node = node->next;
        node->next = evli;
    }
}

static stats_event_t *_make_event_from_node(stats_node_t *node, char *source)
{
    stats_event_t *event = (stats_event_t *)malloc(sizeof(stats_event_t));
    
    if (source != NULL)
        event->source = (char *)strdup(source);
    else
        event->source = NULL;
    event->name = (char *)strdup(node->name);
    event->value = (char *)strdup(node->value);
    event->hidden = node->hidden;
    event->action = STATS_EVENT_SET;
    event->next = NULL;

    return event;
}

static void _add_event_to_queue(stats_event_t *event, stats_event_t **queue)
{
    stats_event_t *node;

    if (*queue == NULL) {
        *queue = event;
    } else {
        node = *queue;
        while (node->next) node = node->next;
        node->next = event;
    }
}

static stats_event_t *_get_event_from_queue(stats_event_t **queue)
{
    stats_event_t *event;

    if (*queue == NULL) return NULL;

    event = *queue;
    *queue = (*queue)->next;
    event->next = NULL;

    return event;
}

static int _send_event_to_client(stats_event_t *event, connection_t *con)
{
    int ret;

    /* send data to the client!!!! */
    ret = sock_write(con->sock, "EVENT %s %s %s\n",
            (event->source != NULL) ? event->source : "global",
            event->name ? event->name : "null",
            event->value ? event->value : "null");

    return (ret == -1) ? 0 : 1;
}

void _dump_stats_to_queue(stats_event_t **queue)
{
    avl_node *node;
    avl_node *node2;
    stats_event_t *event;
    stats_source_t *source;

    thread_mutex_lock(&_stats_mutex);
    /* first we fill our queue with the current stats */
    /* start with the global stats */
    node = avl_get_first(_stats.global_tree);
    while (node) {
        event = _make_event_from_node((stats_node_t *)node->key, NULL);
        _add_event_to_queue(event, queue);

        node = avl_get_next(node);
    }

    /* now the stats for each source */
    node = avl_get_first(_stats.source_tree);
    while (node) {
        source = (stats_source_t *)node->key;
        node2 = avl_get_first(source->stats_tree);
        while (node2) {
            event = _make_event_from_node((stats_node_t *)node2->key, source->source);
            _add_event_to_queue(event, queue);

            node2 = avl_get_next(node2);
        }
        
        node = avl_get_next(node);
    }
    thread_mutex_unlock(&_stats_mutex);
}

/* factoring out code for stats loops
** this function copies all stats to queue, and registers 
** the queue for all new events atomically.
** note: mutex must already be created!
*/
static void _atomic_get_and_register(stats_event_t **queue, mutex_t *mutex)
{
    avl_node *node;
    avl_node *node2;
    stats_event_t *event;
    stats_source_t *source;

    thread_mutex_lock(&_stats_mutex);

    /* first we fill our queue with the current stats */
    
    /* start with the global stats */
    node = avl_get_first(_stats.global_tree);
    while (node) {
        event = _make_event_from_node((stats_node_t *)node->key, NULL);
        _add_event_to_queue(event, queue);

        node = avl_get_next(node);
    }

    /* now the stats for each source */
    node = avl_get_first(_stats.source_tree);
    while (node) {
        source = (stats_source_t *)node->key;
        node2 = avl_get_first(source->stats_tree);
        while (node2) {
            event = _make_event_from_node((stats_node_t *)node2->key, source->source);
            _add_event_to_queue(event, queue);

            node2 = avl_get_next(node2);
        }
        
        node = avl_get_next(node);
    }

    /* now we register to receive future event notices */
    _register_listener(queue, mutex);

    thread_mutex_unlock(&_stats_mutex);
}

void *stats_connection(void *arg)
{
    stats_connection_t *statcon = (stats_connection_t *)arg;
    stats_event_t *local_event_queue = NULL;
    mutex_t local_event_mutex;
    stats_event_t *event;

    /* increment the thread count */
    thread_mutex_lock(&_stats_mutex);
    _stats_threads++;
    thread_mutex_unlock(&_stats_mutex);

    thread_mutex_create("stats local event", &local_event_mutex);

    _atomic_get_and_register(&local_event_queue, &local_event_mutex);

    while (_stats_running) {
        thread_mutex_lock(&local_event_mutex);
        event = _get_event_from_queue(&local_event_queue);
        if (event != NULL) {
            if (!_send_event_to_client(event, statcon->con)) {
                _free_event(event);
                thread_mutex_unlock(&local_event_mutex);
                break;
            }
            _free_event(event);
        } else {
            thread_mutex_unlock(&local_event_mutex);
            thread_sleep (500000);
            continue;
        }
                   
        thread_mutex_unlock(&local_event_mutex);
    }

    thread_mutex_destroy(&local_event_mutex);

    thread_mutex_lock(&_stats_mutex);
    _stats_threads--;
    thread_mutex_unlock(&_stats_mutex);

    return NULL;
}

typedef struct _source_xml_tag {
    char *mount;
    xmlNodePtr node;

    struct _source_xml_tag *next;
} source_xml_t;

static xmlNodePtr _find_xml_node(char *mount, source_xml_t **list, xmlNodePtr root)
{
    source_xml_t *node, *node2;
    int found = 0;

    /* search for existing node */
    node = *list;
    while (node) {
        if (strcmp(node->mount, mount) == 0) {
            found = 1;
            break;
        }
        node = node->next;
    }

    if (found) return node->node;

    /* if we didn't find it, we must build it and add it to the list */

    /* build node */
    node = (source_xml_t *)malloc(sizeof(source_xml_t));
    node->mount = strdup(mount);
    node->node = xmlNewChild(root, NULL, "source", NULL);
    xmlSetProp(node->node, "mount", mount);
    node->next = NULL;

    /* add node */
    if (*list == NULL) {
        *list = node;
    } else {
        node2 = *list;
        while (node2->next) node2 = node2->next;
        node2->next = node;
    }

    return node->node;
}

void stats_transform_xslt(client_t *client, char *xslpath)
{
    xmlDocPtr doc;

    stats_get_xml(&doc, 0);

    xslt_transform(doc, xslpath, client);

    xmlFreeDoc(doc);
}

void stats_get_xml(xmlDocPtr *doc, int show_hidden)
{
    stats_event_t *event;
    stats_event_t *queue;
    xmlNodePtr node, srcnode;
    source_xml_t *src_nodes = NULL;
    source_xml_t *next;

    queue = NULL;
    _dump_stats_to_queue(&queue);

    *doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(*doc, NULL, "icestats", NULL);
    xmlDocSetRootElement(*doc, node);


    event = _get_event_from_queue(&queue);
    while (event)
    {
        if (event->hidden <= show_hidden)
        {
            xmlChar *name, *value;
            name = xmlEncodeEntitiesReentrant (*doc, event->name);
            value = xmlEncodeEntitiesReentrant (*doc, event->value);
            srcnode = node;
            if (event->source) {
                srcnode = _find_xml_node(event->source, &src_nodes, node);
            }
            xmlNewChild(srcnode, NULL, name, value);
            xmlFree (value);
            xmlFree (name);
        }

        _free_event(event);
        event = _get_event_from_queue(&queue);
    }

    while (src_nodes) {
        next = src_nodes->next;
        free(src_nodes->mount);
        free(src_nodes);
        src_nodes = next;
    }
}
void stats_sendxml(client_t *client)
{
    int bytes;
    stats_event_t *event;
    stats_event_t *queue;
    xmlDocPtr doc;
    xmlNodePtr node, srcnode;
    int len;
    xmlChar *buff = NULL;
    source_xml_t *snd;
    source_xml_t *src_nodes = NULL;

    queue = NULL;
    _dump_stats_to_queue(&queue);

    doc = xmlNewDoc("1.0");
    node = xmlNewDocNode(doc, NULL, "icestats", NULL);
    xmlDocSetRootElement(doc, node);


    event = _get_event_from_queue(&queue);
    while (event) {
        if (event->source == NULL) {
            xmlNewChild(node, NULL, event->name, event->value);
        } else {
            srcnode = _find_xml_node(event->source, &src_nodes, node);
            xmlNewChild(srcnode, NULL, event->name, event->value);
        }

        _free_event(event);
        event = _get_event_from_queue(&queue);
    }

    xmlDocDumpMemory(doc, &buff, &len);
    xmlFreeDoc(doc);
    
    client->respcode = 200;
    bytes = sock_write(client->con->sock, "HTTP/1.0 200 OK\r\n"
               "Content-Length: %d\r\n"
               "Content-Type: text/xml\r\n"
               "\r\n", len);
    if (bytes > 0) client->con->sent_bytes += bytes;
    else goto send_error;

    bytes = client_send_bytes (client, buff, (unsigned)len);

 send_error:
    while (src_nodes) {
        snd = src_nodes->next;
        free(src_nodes->mount);
        free(src_nodes);
        src_nodes = snd;
    }
    if (buff) xmlFree(buff);
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
    avl_tree_free(node->stats_tree, _free_stats);
    free(node->source);
    free(node);

    return 1;
}

static void _free_event(stats_event_t *event)
{
    if (event->source) free(event->source);
    if (event->name) free(event->name);
    if (event->value) free(event->value);
    free(event);
}
