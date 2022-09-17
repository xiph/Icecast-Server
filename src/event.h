/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdbool.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <igloo/error.h>

#include "common/thread/thread.h"

#include "icecasttypes.h"

/* implemented */
#define EVENT_TYPE_LOG  "log"
#define EVENT_TYPE_EXEC "exec"
/* not implemented */
#define EVENT_TYPE_URL  "url"

#define MAX_REGLISTS_PER_EVENT 8

typedef enum {
    /* special keys */
    EVENT_EXTRA_LIST_END,
    EVENT_EXTRA_CLIENT,
    EVENT_EXTRA_SOURCE,
    /* real keys */
    EVENT_EXTRA_KEY_URI,
    EVENT_EXTRA_KEY_CONNECTION_IP,
    EVENT_EXTRA_KEY_CLIENT_ROLE,
    EVENT_EXTRA_KEY_CLIENT_USERNAME,
    EVENT_EXTRA_KEY_CLIENT_USERAGENT,
    EVENT_EXTRA_KEY_SOURCE_MEDIA_TYPE,
} event_extra_key_t;

typedef struct {
    event_extra_key_t key;
    const char *value;
} event_extra_entry_t;

struct event_registration_tag;
typedef struct event_registration_tag event_registration_t;

struct event_tag;
typedef struct event_tag event_t;
/* this has no lock member to protect multiple accesses as every non-readonly access is within event.c
 * and is protected by global lock or on the same thread anyway.
 */
struct event_tag {
    /* refernece counter */
    size_t refcount;
    /* reference to next element in chain */
    event_t *next;

    /* event_registration lists.
     * They are referenced here to avoid the need to access
     * config and global client list each time an event is emitted.
     */
    event_registration_t *reglist[MAX_REGLISTS_PER_EVENT];

    /* trigger name */
    char *trigger;

    /* from client */
    bool client_data;
    unsigned long connection_id; /* from client->con->id */
    time_t connection_time; /* from client->con->con_time */
    admin_command_id_t client_admin_command; /* from client->admin_command */
    /* extra */
    size_t extra_size;
    size_t extra_fill;
    event_extra_entry_t *extra_entries;
};

struct event_registration_tag {
    /* refernece counter */
    size_t refcount;
    /* reference to next element in chain */
    event_registration_t *next;

    /* protection */
    mutex_t lock;

    /* type of event */
    char *type;

    /* trigger name */
    char *trigger;

    /* backend state */
    void *state;

    /* emit events */
    int (*emit)(void *state, event_t *event);

    /* free backend state */
    void (*free)(void *state);
};

/* subsystem functions */
void event_initialise(void);
void event_shutdown(void);


/* basic functions to work with event registrations */
event_registration_t * event_new_from_xml_node(xmlNodePtr node);

void event_registration_addref(event_registration_t *er);
void event_registration_release(event_registration_t *er);
void event_registration_push(event_registration_t **er, event_registration_t *tail);

/* event signaling */
void event_emit_va(const char *trigger, ...);
#define event_emit_clientevent(event,client,uri) event_emit_va((event), EVENT_EXTRA_KEY_URI, (uri), EVENT_EXTRA_CLIENT, (client), EVENT_EXTRA_LIST_END)
#define event_emit_global(event) event_emit_va((event), EVENT_EXTRA_LIST_END)

/* reading extra from events */
const char * event_extra_get(const event_t *event, const event_extra_key_t key);
const char * event_extra_key_name(event_extra_key_t key);
igloo_error_t event_to_string_renderer(const event_t *event, string_renderer_t *renderer); /* expects renderer in list mode */

/* Implementations */
int event_get_exec(event_registration_t *er, config_options_t *options);
int event_get_url(event_registration_t *er, config_options_t *options);
int event_get_log(event_registration_t *er, config_options_t *options);

#endif
