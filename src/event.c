/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2023, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include "icecasttypes.h"
#include <igloo/error.h>
#include <igloo/sp.h>

#include "event.h"
#include "event_stream.h"
#include "fastevent.h"
#include "logging.h"
#include "string_renderer.h"
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

static void event_registration_free(igloo_ro_t self);
igloo_RO_PUBLIC_TYPE(event_registration_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(event_registration_free)
        );

static void event_free(igloo_ro_t self);
igloo_RO_PUBLIC_TYPE(event_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(event_free)
        );

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

const char * event_extra_key_name(event_extra_key_t key)
{
    switch (key) {
        case EVENT_EXTRA_KEY_URI: return "uri"; break;
        case EVENT_EXTRA_KEY_CONNECTION_IP: return "connection-ip"; break;
        case EVENT_EXTRA_KEY_CLIENT_ROLE: return "client-role"; break;
        case EVENT_EXTRA_KEY_CLIENT_USERNAME: return "client-username"; break;
        case EVENT_EXTRA_KEY_CLIENT_USERAGENT: return "client-useragent"; break;
        case EVENT_EXTRA_KEY_SOURCE_MEDIA_TYPE: return "source-media-type"; break;
        case EVENT_EXTRA_KEY_SOURCE_INSTANCE_UUID: return "source-instance"; break;
        case EVENT_EXTRA_KEY_DUMPFILE_FILENAME: return "dumpfile-filename"; break;
#ifndef DEVEL_LOGGING
        default: break;
#endif
    }

    return NULL;
}

igloo_error_t event_to_string_renderer(const event_t *event, string_renderer_t *renderer)
{
    static const event_extra_key_t key_list[] = {
        EVENT_EXTRA_KEY_URI,
        EVENT_EXTRA_KEY_SOURCE_MEDIA_TYPE,
        EVENT_EXTRA_KEY_SOURCE_INSTANCE_UUID,
        EVENT_EXTRA_KEY_CONNECTION_IP,
        EVENT_EXTRA_KEY_CLIENT_ROLE,
        EVENT_EXTRA_KEY_CLIENT_USERNAME,
        EVENT_EXTRA_KEY_CLIENT_USERAGENT,
        EVENT_EXTRA_KEY_DUMPFILE_FILENAME,
        EVENT_EXTRA_LIST_END
    };

    string_renderer_add_kv_with_options(renderer, "trigger", event->trigger, STRING_RENDERER_ENCODING_PLAIN, false, false);
    for (size_t i = 0; key_list[i] != EVENT_EXTRA_LIST_END; i++) {
        string_renderer_add_kv_with_options(renderer, event_extra_key_name(key_list[i]), event_extra_get(event, key_list[i]), STRING_RENDERER_ENCODING_PLAIN, false, false);
    }

    if (event->client_data) {
        string_renderer_add_ki_with_options(renderer, "connection-id", event->connection_id, STRING_RENDERER_ENCODING_PLAIN, true, false);
        string_renderer_add_ki_with_options(renderer, "connection-time", event->connection_time, STRING_RENDERER_ENCODING_PLAIN, true, false);
        string_renderer_add_ki_with_options(renderer, "client-admin-command", event->client_admin_command, STRING_RENDERER_ENCODING_PLAIN, true, false);
    }

    return igloo_ERROR_NONE;
}

/* work with event_t* */
static void event_free(igloo_ro_t self)
{
    event_t *event = igloo_ro_to_type(self, event_t);
    size_t i;
    event_t *to_free;

    if (!event)
        return;

    thread_mutex_lock(&event_lock);
    for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++)
        igloo_ro_unref(&(event->reglist[i]));

    free(event->trigger);

    for (i = 0; i < event->extra_fill; i++)
        igloo_sp_unref(&(event->extra_entries[i].value), igloo_instance);
    free(event->extra_entries);

    to_free = event->next;
    thread_mutex_unlock(&event_lock);

    if (to_free)
        igloo_ro_unref(&to_free);
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

    igloo_ro_ref(next, event, event_t);

    thread_cond_broadcast(&cond);
}

static void event_push_reglist(event_t *event, event_registration_t *reglist) {
    size_t i;

    if (!event || !reglist)
        return;

    for (i = 0; i < (sizeof(event->reglist)/sizeof(*event->reglist)); i++) {
        if (!event->reglist[i]) {
            igloo_ro_ref(reglist, &(event->reglist[i]), event_registration_t);
            return;
        }
    }

    ICECAST_LOG_ERROR("Can not push reglist %p into event %p. No space left on event.", reglist, event);
}

static event_t *event_new(const char *trigger) {
    event_t *ret;

    if (!trigger)
        return NULL;

    if (igloo_ro_new_raw(&ret, event_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    ret->trigger = strdup(trigger);
    ret->client_admin_command = ADMIN_COMMAND_ERROR;

    if (!ret->trigger) {
        igloo_ro_unref(&ret);
        return NULL;
    }

    return ret;
}

/* subsystem functions */
static inline void _try_event(event_registration_t *er, event_t *event) {
    /* er is already locked */
    if (!util_is_in_list(er->trigger, event->trigger))
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

        igloo_ro_unref(&event);
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

    igloo_ro_unref(&event_queue_to_free);

    thread_cond_destroy(&cond);
    /* destry mutex */
    thread_mutex_destroy(&event_lock);
}


/* basic functions to work with event registrations */
event_registration_t * event_new_from_xml_node(xmlNodePtr node) {
    event_registration_t *ret;
    config_options_t *options;
    int rv;

    if (igloo_ro_new_raw(&ret, event_registration_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    /* BEFORE RELEASE 2.5.0 DOCUMENT: Document <event type="..." trigger="..."> */
    ret->type     = (char*)xmlGetProp(node, XMLSTR("type"));
    ret->trigger  = (char*)xmlGetProp(node, XMLSTR("trigger"));

    if (!ret->type || !ret->trigger) {
        ICECAST_LOG_ERROR("Event node isn't complete. Type or Trigger missing.");
        igloo_ro_unref(&ret);
        return NULL;
    }

    options = config_parse_options(node);
    if (strcmp(ret->type, EVENT_TYPE_LOG) == 0) {
        rv = event_get_log(ret, options);
    } else if (strcmp(ret->type, EVENT_TYPE_EXEC) == 0) {
        rv = event_get_exec(ret, options);
    } else if (strcmp(ret->type, EVENT_TYPE_TERMINATE) == 0) {
        rv = event_get_terminate(ret, options);
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
        igloo_ro_unref(&ret);
        return NULL;
    }

    return ret;
}

static void event_registration_free(igloo_ro_t self)
{
    event_registration_t *er = igloo_ro_to_type(self, event_registration_t);

    thread_mutex_lock(&er->lock);

    if (er->next)
        igloo_ro_unref(&(er->next));

    xmlFree(er->type);
    xmlFree(er->trigger);

    if (er->free)
        er->free(er->state);

    thread_mutex_unlock(&er->lock);
    thread_mutex_destroy(&er->lock);
}

void event_registration_push(event_registration_t **er, event_registration_t *tail) {
    event_registration_t *next, *cur;

    if (!er || !tail)
        return;

    if (!*er) {
        igloo_ro_ref(tail, er, event_registration_t);
        return;
    }

    igloo_ro_ref(*er, &cur, event_registration_t);
    thread_mutex_lock(&cur->lock);
    while (1) {
        if (!cur->next)
            break;

        if (igloo_ro_ref(cur->next, &next, event_registration_t) != igloo_ERROR_NONE)
            break;

        thread_mutex_unlock(&cur->lock);
        igloo_ro_unref(&cur);
        cur = next;
        thread_mutex_lock(&cur->lock);
    }

    igloo_ro_ref(tail, &(cur->next), event_registration_t);
    thread_mutex_unlock(&cur->lock);
    igloo_ro_unref(&cur);
}

/* event signaling */
void event_emit(event_t *event) {
    fastevent_emit(FASTEVENT_TYPE_SLOWEVENT, FASTEVENT_FLAG_NONE, FASTEVENT_DATATYPE_EVENT, event);
    event_stream_emit_event(event);
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
        extra_add(event, EVENT_EXTRA_KEY_SOURCE_INSTANCE_UUID, source->instance_uuid);
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
    igloo_ro_unref(&event);
}
