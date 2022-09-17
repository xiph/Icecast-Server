/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include <igloo/error.h>
#include <igloo/sp.h>

#include "event.h"
#include "fastevent.h"
#include "logging.h"
#include "admin.h"
#include "connection.h"
#include "client.h"
#include "source.h"
#include "cfgfile.h"
#include "global.h"  /* for igloo_instance */

#define CATMODULE "event"

static mutex_t event_lock;
static event_t *event_queue = NULL;
static bool event_running = false;
static thread_type *event_thread = NULL;
static cond_t cond;

/* ignores errors */
static void extra_add(event_t *event, event_extra_key_t key, const char *value)
{
    if (!value)
        return;

    if (event->extra_fill == event->extra_size) {
        event_extra_entry_t *n = realloc(event->extra_entries, sizeof(*n)*(event->extra_size + 16));
        if (!n)
            return;
        memset(&(n[event->extra_size]), 0, sizeof(*n)*16);
        event->extra_size += 16;
        event->extra_entries = n;
    }

    if (igloo_sp_replace(value, &(event->extra_entries[event->extra_fill].value), igloo_instance) == igloo_ERROR_NONE) {
        event->extra_entries[event->extra_fill].key = key;
        event->extra_fill++;
    }
}

const char * event_extra_get(const event_t *event, const event_extra_key_t key)
{
    size_t i;

    if (!event || !event->extra_entries)
        return NULL;

    for (i = 0; i < event->extra_fill; i++) {
        if (event->extra_entries[i].key == key)
            return event->extra_entries[i].value;
    }

    return NULL;
}

/* work with event_t* */
static void event_addref(event_t *event) {
    if (!event)
        return;
    thread_mutex_lock(&event_lock);
    event->refcount++;
    thread_mutex_unlock(&event_lock);
}

static void event_release(event_t *event) {
    size_t i;
    event_t *to_free;

    if (!event)
        return;

    thread_mutex_lock(&event_lock);
    event->refcount--;
    if (event->refcount) {
        thread_mutex_unlock(&event_lock);
        return;
    }

    for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++)
        event_registration_release(event->reglist[i]);

    free(event->trigger);

    for (i = 0; i < event->extra_fill; i++)
        igloo_sp_unref(&(event->extra_entries[i].value), igloo_instance);
    free(event->extra_entries);

    to_free = event->next;
    free(event);
    thread_mutex_unlock(&event_lock);

    if (to_free)
        event_release(to_free);
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

    *event = next;

    thread_cond_broadcast(&cond);
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
    bool running = false;

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
            if (!running)
                break;
            thread_cond_wait(&cond);
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
    thread_cond_create(&cond);

    /* initialise everything */
    thread_mutex_lock(&event_lock);
    event_running = true;
    thread_mutex_unlock(&event_lock);

    /* start thread */
    event_thread = thread_create("Events Thread", event_run_thread, NULL, THREAD_ATTACHED);
}

void event_shutdown(void) {
    event_t *event_queue_to_free = NULL;

    /* stop thread */
    if (!event_running)
        return;

    thread_mutex_lock(&event_lock);
    event_running = false;
    thread_mutex_unlock(&event_lock);

    /* join thread as soon as it stopped */
    thread_cond_broadcast(&cond);
    thread_join(event_thread);

    /* shutdown everything */
    thread_mutex_lock(&event_lock);
    event_thread = NULL;
    event_queue_to_free = event_queue;
    event_queue = NULL;
    thread_mutex_unlock(&event_lock);

    event_release(event_queue_to_free);

    thread_cond_destroy(&cond);
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
    fastevent_emit(FASTEVENT_TYPE_SLOWEVENT, FASTEVENT_FLAG_NONE, FASTEVENT_DATATYPE_EVENT, event);
    event_addref(event);
    thread_mutex_lock(&event_lock);
    event_push(&event_queue, event);
    thread_mutex_unlock(&event_lock);
}

void event_emit_va(const char *trigger, ...) {
    event_t *event = event_new(trigger);
    source_t *source = NULL;
    client_t *client = NULL;
    const char *uri = NULL;
    ice_config_t *config;
    const mount_proxy *mount;
    va_list ap;

    if (!event) {
        ICECAST_LOG_ERROR("Can not create event.");
        return;
    }

    va_start(ap, trigger);
    while (true) {
        event_extra_key_t key = va_arg(ap, event_extra_key_t);

        if (key == EVENT_EXTRA_LIST_END) {
            break;
        } else if (key == EVENT_EXTRA_KEY_URI) {
            uri = va_arg(ap, const char *);
        } else if (key == EVENT_EXTRA_SOURCE) {
            source = va_arg(ap, source_t *);
        } else if (key == EVENT_EXTRA_CLIENT) {
            client = va_arg(ap, client_t *);
        }
    }
    va_end(ap);

    if (source && !uri)
        uri = source->mount;

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
#ifndef FASTEVENT_ENABLED
    if (event->reglist[0] == NULL) {
        /* we have no registrations, drop this event. */
        event_release(event);
        return;
    }
#endif

    if (uri)
        extra_add(event, EVENT_EXTRA_KEY_URI, uri);

    va_start(ap, trigger);
    while (true) {
        event_extra_key_t key = va_arg(ap, event_extra_key_t);

        if (key == EVENT_EXTRA_LIST_END) {
            break;
        } else if (key == EVENT_EXTRA_SOURCE || key == EVENT_EXTRA_CLIENT) {
            /* shift one arg off */
            va_arg(ap, const void *);
        } else {
            const char *value = va_arg(ap, const char *);

            extra_add(event, key, value);
        }
    }
    va_end(ap);

    if (source) {
        if (source->format && source->format->contenttype) {
            extra_add(event, EVENT_EXTRA_KEY_SOURCE_MEDIA_TYPE, source->format->contenttype);
        }
    }

    if (client) {
        event->client_data = true;
        event->connection_id = client->con->id;
        event->connection_time = client->con->con_time;
        event->client_admin_command = client->admin_command;
        extra_add(event, EVENT_EXTRA_KEY_CONNECTION_IP, client->con->ip);
        extra_add(event, EVENT_EXTRA_KEY_CLIENT_ROLE, client->role);
        extra_add(event, EVENT_EXTRA_KEY_CLIENT_USERNAME, client->username);
        extra_add(event, EVENT_EXTRA_KEY_CLIENT_USERAGENT, httpp_getvar(client->parser, "user-agent"));
    }

    event_emit(event);
    event_release(event);
}
