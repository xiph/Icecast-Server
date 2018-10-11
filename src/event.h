/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <permafrost/thread.h>

#include "icecasttypes.h"

/* implemented */
#define EVENT_TYPE_LOG  "log"
#define EVENT_TYPE_EXEC "exec"
/* not implemented */
#define EVENT_TYPE_URL  "url"

#define MAX_REGLISTS_PER_EVENT 8

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
    char *uri; /* from context */
    unsigned long connection_id; /* from client->con->id */
    char *connection_ip; /* from client->con->ip */
    time_t connection_time; /* from client->con->con_time */
    char *client_role; /* from client->role */
    char *client_username; /* from client->username */
    char *client_useragent; /* from httpp_getvar(client->parser, "user-agent") */
    admin_command_id_t client_admin_command; /* from client->admin_command */
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
void event_emit_clientevent(const char *trigger, client_t *client, const char *uri);
#define event_emit_global(x) event_emit_clientevent((x), NULL, NULL)

#endif
