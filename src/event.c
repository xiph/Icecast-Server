/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp Schafft <lion@lion.leolix.org>
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include "event.h"
#include "event_log.h"
#include "event_exec.h"
#include "event_url.h"
#include "logging.h"
#include "admin.h"

#define CATMODULE "event"

static mutex_t event_lock;
static event_t *event_queue = NULL;
static int event_running = 0;
static thread_type *event_thread = NULL;

/* work with event_t* */
static void event_addref(event_t *event) {
    if (!event)
        return;
    event->refcount++;
}

static void event_release(event_t *event) {
    size_t i;

    if (!event)
        return;
    event->refcount--;
    if (event->refcount)
        return;

    for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++)
        event_registration_release(event->reglist[i]);

    free(event->trigger);
    free(event->uri);
    free(event->connection_ip);
    free(event->client_role);
    free(event->client_username);
    free(event->client_useragent);
    event_release(event->next);

    free(event);
}

static void event_push(event_t **event, event_t *next) {
    size_t i = 0;

    if (!event || !next)
        return;

    while (*event && i < 128) {
        event = &(*event)->next;
        i++;
    }

    if (i == 128) {
        ICECAST_LOG_ERROR("Can not push event %p into queue. Queue is full.", event);
        return;
    }

    event_addref(*event = next);
}

static void event_push_reglist(event_t *event, event_registration_t *reglist) {
    size_t i;

    if (!event || !reglist)
        return;

    for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++) {
        if (!event->reglist[i]) {
            event_registration_addref(event->reglist[i] = reglist);
            return;
        }
    }

    ICECAST_LOG_ERROR("Can not push reglist %p into event %p. No space left on event.", reglist, event);
}

static event_t *event_new(const char *trigger) {
    event_t *ret;

    if (!trigger)
        return NULL;

    ret = calloc(1, sizeof(event_t));

    if (!ret)
        return NULL;

    ret->refcount = 1;
    ret->trigger = strdup(trigger);
    ret->client_admin_command = ADMIN_COMMAND_ERROR;

    if (!ret->trigger) {
        event_release(ret);
        return NULL;
    }

    return ret;
}

/* subsystem functions */
static inline void _try_event(event_registration_t *er, event_t *event) {
    /* er is already locked */
    if (strcmp(er->trigger, event->trigger) != 0)
        return;

    if (er->emit)
        er->emit(er->state, event);
}

static inline void _try_registrations(event_registration_t *er, event_t *event) {
    if (!er)
        return;

    thread_mutex_lock(&er->lock);
    while (1) {
        /* try registration */
        _try_event(er, event);

       /* go to next registration */
       if (er->next) {
           event_registration_t *next = er->next;

           thread_mutex_lock(&next->lock);
           thread_mutex_unlock(&er->lock);
           er = next;
       } else {
           break;
       }
    }
    thread_mutex_unlock(&er->lock);
}

static void *event_run_thread (void *arg) {
    int running = 0;

    (void)arg;

    do {
        event_t *event;
        size_t i;

        thread_mutex_lock(&event_lock);
        running = event_running;
        if (event_queue) {
            event = event_queue;
            event_queue = event_queue->next;
            event->next = NULL;
        } else {
            event = NULL;
        }
        thread_mutex_unlock(&event_lock);

        /* sleep if nothing todo and then try again */
        if (!event) {
            thread_sleep(150000);
            continue;
        }

        for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++)
            _try_registrations(event->reglist[i], event);

        event_release(event);
    } while (running);

    return NULL;
}

void event_initialise(void) {
    /* create mutex */
    thread_mutex_create(&event_lock);

    /* initialise everything */
    thread_mutex_lock(&event_lock);
    event_running = 1;
    thread_mutex_unlock(&event_lock);

    /* start thread */
    event_thread = thread_create("events thread", event_run_thread, NULL, THREAD_ATTACHED);
}

void event_shutdown(void) {
    /* stop thread */
    if (!event_running)
        return;

    thread_mutex_lock(&event_lock);
    event_running = 0;
    thread_mutex_unlock(&event_lock);

    /* join thread as soon as it stopped */
    thread_join(event_thread);

    /* shutdown everything */
    thread_mutex_lock(&event_lock);
    event_thread = NULL;
    event_release(event_queue);
    event_queue = NULL;
    thread_mutex_unlock(&event_lock);

    /* destry mutex */
    thread_mutex_destroy(&event_lock);
}


/* basic functions to work with event registrations */
event_registration_t * event_new_from_xml_node(xmlNodePtr node) {
    event_registration_t *ret = calloc(1, sizeof(event_registration_t));
    config_options_t *options;
    int rv;

    if(!ret)
        return NULL;

    ret->refcount = 1;

    /* BEFORE RELEASE 2.5.0 DOCUMENT: Document <event type="..." trigger="..."> */
    ret->type     = (char*)xmlGetProp(node, XMLSTR("type"));
    ret->trigger  = (char*)xmlGetProp(node, XMLSTR("trigger"));

    if (!ret->type || !ret->trigger) {
        ICECAST_LOG_ERROR("Event node isn't complete. Type or Trigger missing.");
        event_registration_release(ret);
        return NULL;
    }

    options = config_parse_options(node);
    if (strcmp(ret->type, EVENT_TYPE_LOG) == 0) {
        rv = event_get_log(ret, options);
    } else if (strcmp(ret->type, EVENT_TYPE_EXEC) == 0) {
        rv = event_get_exec(ret, options);
#ifdef HAVE_CURL
    } else if (strcmp(ret->type, EVENT_TYPE_URL) == 0) {
        rv = event_get_url(ret, options);
#endif
    } else {
        ICECAST_LOG_ERROR("Unknown Event backend %s.", ret->type);
        rv = -1;
    }
    config_clear_options(options);

    if (rv != 0) {
        ICECAST_LOG_ERROR("Can not set up event backend %s for trigger %s", ret->type, ret->trigger);
        event_registration_release(ret);
        return NULL;
    }

    return ret;
}

void event_registration_addref(event_registration_t * er) {
    if(!er)
        return;
    thread_mutex_lock(&er->lock);
    er->refcount++;
    thread_mutex_unlock(&er->lock);
}

void event_registration_release(event_registration_t *er) {
    if(!er)
        return;
    thread_mutex_lock(&er->lock);
    er->refcount--;

    if (er->refcount) {
        thread_mutex_unlock(&er->lock);
        return;
    }

    if (er->next)
        event_registration_release(er->next);

    xmlFree(er->type);
    xmlFree(er->trigger);

    if (er->free)
        er->free(er->state);

    thread_mutex_unlock(&er->lock);
    thread_mutex_destroy(&er->lock);
    free(er);
}

void event_registration_push(event_registration_t **er, event_registration_t *tail) {
    event_registration_t *next, *cur;

    if (!er || !tail)
        return;

    if (!*er) {
        event_registration_addref(*er = tail);
        return;
    }

    event_registration_addref(cur = *er);
    thread_mutex_lock(&cur->lock);
    while (1) {
        next = cur->next;
        if (!cur->next)
            break;

        event_registration_addref(next);
        thread_mutex_unlock(&cur->lock);
        event_registration_release(cur);
        cur = next;
        thread_mutex_lock(&cur->lock);
    }

    event_registration_addref(cur->next = tail);
    thread_mutex_unlock(&cur->lock);
    event_registration_release(cur);
}

/* event signaling */
void event_emit(event_t *event) {
    thread_mutex_lock(&event_lock);
    event_push(&event_queue, event);
    thread_mutex_unlock(&event_lock);
}

/* this function needs to extract all the info from the client, source and mount object
 * as after return the pointers become invalid.
 */
void event_emit_clientevent(const char *trigger, client_t *client, const char *uri) {
    event_t *event = event_new(trigger);
    ice_config_t *config;
    mount_proxy *mount;

    if (!event) {
        ICECAST_LOG_ERROR("Can not create event.");
        return;
    }

    config = config_get_config();
    event_push_reglist(event, config->event);

    mount = config_find_mount(config, uri, MOUNT_TYPE_NORMAL);
    if (mount && mount->mounttype == MOUNT_TYPE_NORMAL)
        event_push_reglist(event, mount->event);

    mount = config_find_mount(config, uri, MOUNT_TYPE_DEFAULT);
    if (mount && mount->mounttype == MOUNT_TYPE_DEFAULT)
        event_push_reglist(event, mount->event);
    config_release_config();

    /* This isn't perfectly clean but is an important speedup:
     * If first element of reglist is NULL none of the above pushed in
     * some registrations. If there are no registrations we can just drop
     * this event now and here.
     * We do this before inserting all the data into the object to avoid
     * all the strdups() and stuff in case they aren't needed.
     */
    if (event->reglist[0] == NULL) {
        /* we have no registrations, drop this event. */
        event_release(event);
        return;
    }

    if (client) {
        const char *tmp;
        event->connection_id = client->con->id;
        event->connection_time = client->con->con_time;
        event->client_admin_command = client->admin_command;
        event->connection_ip = strdup(client->con->ip);
        if (client->role)
            event->client_role = strdup(client->role);
        if (client->username)
            event->client_username = strdup(client->username);
        tmp = httpp_getvar(client->parser, "user-agent");
        if (tmp)
            event->client_useragent = strdup(tmp);
    }

    if (uri)
        event->uri = strdup(uri);

    event_emit(event);
    event_release(event);
}
