/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2024     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <igloo/typedef.h>
#include "icecasttypes.h"

#include <igloo/error.h>
#include <igloo/ro.h>
#include <igloo/sp.h>
#include <igloo/cs.h>
#include <igloo/uuid.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"

#include "event_stream.h"
#include "string_renderer.h"
#include "json.h"
#include "util.h"
#include "fserve.h"
#include "global.h"
#include "event.h"
#include "client.h"
#include "connection.h"
#include "source.h"
#include "errors.h"
#include "format_mp3.h"
#include "logging.h"
#define CATMODULE "event-stream"

struct event_stream_event_tag {
    igloo_ro_full_t __parent;

    bool removed; // removed from the queue, clients referencing this are fallen too far behind

    const char * uuid;
    const char * mount;
    const char * source_instance_uuid;
    const char * rendered;
    size_t rendered_length;

    event_t *event;
    vorbis_comment *vc;

    event_stream_event_t *next;
};

typedef struct {
    const char *mount;
    const char *current_buffer;
    event_stream_event_t *current_event;
    size_t todo;
    bool events_global;
    bool events_any_mount;
} event_stream_clientstate_t;

static void event_stream_event_free(igloo_ro_t self);
static void *event_stream_thread_function(void *arg);
static void event_stream_event_render(event_stream_event_t *event);

igloo_RO_PUBLIC_TYPE(event_stream_event_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(event_stream_event_free),
        );

static mutex_t                  event_stream_event_mutex; // protects: event_queue, event_queue_next, event_stream_thread, alive
static event_stream_event_t    *event_queue;
static event_stream_event_t   **event_queue_next = &event_queue;
static thread_type             *event_stream_thread;
static cond_t                   event_stream_cond;
static avl_tree                *client_tree;
static bool                     alive;

static void event_stream_clientstate_free(client_t *client)
{
    event_stream_clientstate_t *state = client->format_data;
    client->format_data = NULL;

    if (!state)
        return;

    igloo_ro_unref(&(state->current_event));
    igloo_sp_unref(&(state->mount), igloo_instance);

    free(state);
}

static void event_stream_event_free(igloo_ro_t self)
{
    event_stream_event_t *event = igloo_ro_to_type(self, event_stream_event_t);
    igloo_sp_unref(&(event->uuid), igloo_instance);
    igloo_sp_unref(&(event->mount), igloo_instance);
    igloo_sp_unref(&(event->source_instance_uuid), igloo_instance);
    igloo_sp_unref(&(event->rendered), igloo_instance);
    igloo_ro_unref(&(event->event));
    igloo_ro_unref(&(event->next));
}

static event_stream_event_t * event_stream_event_new(void)
{
    event_stream_event_t *event;

    if (igloo_ro_new_raw(&event, event_stream_event_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    if (igloo_uuid_new_random_sp(&(event->uuid), igloo_instance) != igloo_ERROR_NONE) {
        igloo_ro_unref(&event);
        return NULL;
    }

    return event;
}

static void event_stream_queue(event_stream_event_t *event)
{
    event_stream_event_render(event);

    thread_mutex_lock(&event_stream_event_mutex);
    *event_queue_next = event;
    event_queue_next = &(event->next);
    thread_mutex_unlock(&event_stream_event_mutex);

    thread_cond_broadcast(&event_stream_cond);
    ICECAST_LOG_INFO("event queued");
}

static int _free_client(void *key)
{
    client_t *client = (client_t *)key;
    client_destroy(client);
    return 1;
}

void event_stream_initialise(void)
{
    thread_mutex_create(&event_stream_event_mutex);
    thread_cond_create(&event_stream_cond);
    client_tree = avl_tree_new(client_compare, NULL);
    alive = true;
}

void event_stream_shutdown(void)
{
    thread_mutex_lock(&event_stream_event_mutex);
    alive = false;
    thread_mutex_unlock(&event_stream_event_mutex);

    thread_cond_broadcast(&event_stream_cond);

    if (event_stream_thread)
        thread_join(event_stream_thread);

    avl_tree_free(client_tree, _free_client);

    thread_mutex_lock(&event_stream_event_mutex);
    igloo_ro_unref(&event_queue);
    thread_mutex_unlock(&event_stream_event_mutex);

    thread_mutex_destroy(&event_stream_event_mutex);
    thread_cond_destroy(&event_stream_cond);
}

static bool event_stream_match_event_with_client(client_t *client, event_stream_event_t *event)
{
    event_stream_clientstate_t *state = client->format_data;

    if (event->mount) {
        if (!state->events_any_mount) {
            if (!state->mount)
                return false;
            if (strcmp(state->mount, event->mount) != 0)
                return false;
        }
    } else {
        if (!state->events_global)
            return false;
    }

    return true;
}

static void event_stream_add_client_inner(client_t *client, void *ud)
{
    event_stream_clientstate_t *state = calloc(1, sizeof(event_stream_clientstate_t));
    const char *mount = httpp_get_param(client->parser, "mount");
    const char *request_global = httpp_get_param(client->parser, "request-global");
    const char *last_event_id = httpp_getvar(client->parser, "last-event-id");

    (void)ud;

    if (!state) {
        client_destroy(client);
        return;
    }

    if (mount)
        igloo_sp_replace(mount, &(state->mount), igloo_instance);

    state->events_any_mount = !mount;

    if (request_global)
        igloo_cs_to_bool(request_global, &(state->events_global));

    thread_mutex_lock(&event_stream_event_mutex);
    { /* find the best possible event! */
        event_stream_event_t * next = event_queue;
        event_stream_event_t * event = NULL;

        while (next) {
            event = next;
            next = event->next;

            if (last_event_id && strcmp(event->uuid, last_event_id) == 0) {
                break;
            }
        }

        igloo_ro_ref(event, &(state->current_event), event_stream_event_t);

        /* emulate the the state of us just being done */
        state->current_buffer = "";
        state->todo = 0;
    }
    thread_mutex_unlock(&event_stream_event_mutex);

    client->format_data = state;
    client->free_client_data = event_stream_clientstate_free;

    avl_tree_wlock(client_tree);
    avl_insert(client_tree, client);
    avl_tree_unlock(client_tree);

    thread_mutex_lock(&event_stream_event_mutex);
    if (!event_stream_thread) {
        event_stream_thread = thread_create("Event Stream Thread", event_stream_thread_function, (void *)NULL, THREAD_ATTACHED);
    }
    thread_mutex_unlock(&event_stream_event_mutex);

    thread_cond_broadcast(&event_stream_cond);
}

void event_stream_add_client(client_t *client)
{
    ssize_t len = util_http_build_header(client->refbuf->data, PER_CLIENT_REFBUF_SIZE, 0,
            0, 200, NULL,
            "text/event-stream", NULL,
            "", NULL, client
            );
    if (len < 1 || len > PER_CLIENT_REFBUF_SIZE)
        return;

    client->refbuf->len = len;

    fserve_add_client_callback(client, event_stream_add_client_inner, NULL);
}

static void event_stream_set_source(event_stream_event_t *event, source_t *source)
{
    igloo_sp_replace(source->mount, &(event->mount), igloo_instance);
    igloo_sp_replace(source->instance_uuid, &(event->source_instance_uuid), igloo_instance);
}

void event_stream_emit_event(event_t *event)
{
    event_stream_event_t *el = event_stream_event_new();
    if (!el)
        return;

    igloo_ro_ref_replace(event, &(el->event), event_t);
    igloo_sp_replace(event_extra_get(event, EVENT_EXTRA_KEY_URI), &(el->mount), igloo_instance);

    event_stream_queue(el);
}

void event_stream_emit_vc(source_t *source, vorbis_comment *vc)
{
    event_stream_event_t *el = event_stream_event_new();
    if (!el)
        return;

    event_stream_set_source(el, source);

    /* we only have a temp reference to vc, so we set it, render, and then unset before we queue. */
    el->vc = vc;
    event_stream_event_render(el);
    el->vc = NULL;

    event_stream_queue(el);
}

static void event_stream_send_to_client(client_t *client)
{
    event_stream_clientstate_t *state = client->format_data;
    bool going = true;

    ICECAST_LOG_DINFO("Sending to client %p {.con.id = %llu}, with state %p", client, (long long unsigned int)client->con->id, state);

    do {
        if (!state->current_buffer) {
            if (event_stream_match_event_with_client(client, state->current_event)) {
                state->current_buffer = state->current_event->rendered;
                state->todo           = state->current_event->rendered_length;
            } else {
                state->todo = 0;
            }
        }

        if (state->todo) {
            ssize_t done = client_send_bytes(client, state->current_buffer, state->todo);
            if (done < 0) {
                return;
            } else {
                state->todo -= done;
                state->current_buffer += done;
            }
        }

        if (!state->todo) {
            if (state->current_event->next) {
                igloo_ro_ref_replace(state->current_event->next, &(state->current_event), event_stream_event_t);
                state->current_buffer = NULL;
            } else {
                going = false;
            }
        }
    } while (going);
}

static void event_stream_cleanup_queue(void)
{
    thread_mutex_lock(&event_stream_event_mutex);
    {
        static const size_t to_keep = 32;
        event_stream_event_t *cur;
        size_t count = 0;

        cur = event_queue;
        while (cur) {
            count++;
            cur = cur->next;
        }

        if (count > to_keep) {
            for (size_t to_remove = count - to_keep; to_remove; to_remove--) {
                cur = event_queue;
                event_queue = cur->next;
                cur->removed = 1;
                cur->next = NULL;
            }
        }
    }
    thread_mutex_unlock(&event_stream_event_mutex);
}

static void *event_stream_thread_function(void *arg)
{
    bool running = true;

    ICECAST_LOG_INFO("Good morning!");

    do {
        thread_cond_timedwait(&event_stream_cond, 1000);

        event_stream_cleanup_queue();

        {
            avl_tree_wlock(client_tree);
            avl_node *next;

            next = avl_get_first(client_tree);
            while (next) {
                avl_node *node = next;
                client_t *client = node->key;

                next = avl_get_next(next);

                event_stream_send_to_client(client);
                {
                    event_stream_clientstate_t *state = client->format_data;
                    if (state->current_event->removed) {
                        ICECAST_LOG_INFO("Client %p %lu (%s) has fallen too far behind, removing",
                                client, client->con->id, client->con->ip);
                        client->con->error = 1;
                    }
                }
                if (client->con->error) {
                    avl_delete(client_tree, (void *) client, _free_client);
                }
            }
            avl_tree_unlock(client_tree);
        }

        thread_mutex_lock(&event_stream_event_mutex);
        running = alive;

        thread_mutex_unlock(&event_stream_event_mutex);
    } while (running);

    ICECAST_LOG_INFO("Good evening!");
    return NULL;
}

static void add_number(json_renderer_t *json, const char *key, intmax_t val)
{
    if (val < 1)
        return;

    json_renderer_write_key(json, key, JSON_RENDERER_FLAGS_NONE);
    json_renderer_write_uint(json, val);
}

static void event_stream_event_render(event_stream_event_t *event)
{
    string_renderer_t * renderer;
    json_renderer_t *json;
    char *body;
    bool has_type = false;
    bool has_crude = false;

    if (event->rendered)
        return;

    if (igloo_ro_new(&renderer, string_renderer_t, igloo_instance) != igloo_ERROR_NONE)
        return;
    json = json_renderer_create(JSON_RENDERER_FLAGS_NONE);
    if (!json) {
        igloo_ro_unref(&renderer);
        return;
    }

    json_renderer_begin(json, JSON_ELEMENT_TYPE_OBJECT);
    if (event->event) {
        event_t *uevent = event->event;

        has_type = true;
        has_crude = true;

        json_renderer_write_key(json, "type", JSON_RENDERER_FLAGS_NONE);
        json_renderer_write_string(json, "event", JSON_RENDERER_FLAGS_NONE);

        json_renderer_write_key(json, "crude", JSON_RENDERER_FLAGS_NONE);
        json_renderer_begin(json, JSON_ELEMENT_TYPE_OBJECT);
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
            json_renderer_write_key(json, "trigger", JSON_RENDERER_FLAGS_NONE);
            json_renderer_write_string(json, uevent->trigger, JSON_RENDERER_FLAGS_NONE);

            for (size_t i = 0; key_list[i] != EVENT_EXTRA_LIST_END; i++) {
                const char *value = event_extra_get(uevent, key_list[i]);

                if (!value)
                    continue;

                json_renderer_write_key(json, event_extra_key_name(key_list[i]), JSON_RENDERER_FLAGS_NONE);
                json_renderer_write_string(json, value, JSON_RENDERER_FLAGS_NONE);
            }

            add_number(json, "connection-id", uevent->connection_id);
            add_number(json, "connection-time", uevent->connection_time);
            add_number(json, "client-admin-command", uevent->client_admin_command);
        }
        json_renderer_end(json);
    }

    if (event->mount) {
        json_renderer_write_key(json, "mount", JSON_RENDERER_FLAGS_NONE);
        json_renderer_write_string(json, event->mount, JSON_RENDERER_FLAGS_NONE);
    }

    if (event->vc) {
        if (!has_type) {
            json_renderer_write_key(json, "type", JSON_RENDERER_FLAGS_NONE);
            json_renderer_write_string(json, "vc", JSON_RENDERER_FLAGS_NONE);
            has_type = true;
        }

        json_renderer_write_key(json, "vc", JSON_RENDERER_FLAGS_NONE);
        json_renderer_begin(json, JSON_ELEMENT_TYPE_OBJECT);
        {
            /* ok, this part is tricky, we first need to figure out all the keys.
             * we however cheat here by assuming a few things.
             * TODO: Fix this.
             */
            struct {
                char name[64];
                size_t count;
            } keys[64];
            memset(keys, 0, sizeof(keys));

            /* it is an int in libvorbis, not a size_t */
            for (int i = 0; i < event->vc->comments; i++) {
                const char *comment = event->vc->user_comments[i];
                const char *keyend = strchr(comment, '=');
                bool found = false;
                size_t keylen;

                if (!keyend)
                    continue;
                keylen = keyend - comment;
                if (keylen >= sizeof(keys->name))
                    continue;

                for (size_t j = 0; j < (sizeof(keys)/sizeof(*keys)); j++) {
                    char *name = keys[j].name;

                    if (*name) {
                        if (strncasecmp(comment, name, keylen) == 0) {
                            found = true;
                            keys[j].count++;
                        }
                    }
                }

                if (found)
                    break;

                for (size_t j = 0; j < (sizeof(keys)/sizeof(*keys)); j++) {
                    char *name = keys[j].name;

                    if (!*name) {
                        memcpy(name, comment, keylen);
                        name[keylen] = 0;
                        keys[j].count = 1;
                        igloo_cs_to_upper(name);
                        break;
                    }
                }
            }

            /* Now we have a list of all keys... */
            for (size_t j = 0; j < (sizeof(keys)/sizeof(*keys)); j++) {
                const char *name = keys[j].name;
                if (!*name)
                    continue;

                json_renderer_write_key(json, name, JSON_RENDERER_FLAGS_NONE);
                json_renderer_begin(json, JSON_ELEMENT_TYPE_ARRAY);
                for (size_t i = 0; i < keys[j].count; i++) {
                    const char *value = vorbis_comment_query(event->vc, name, i);
                    json_renderer_write_string(json, value, JSON_RENDERER_FLAGS_NONE);
                }
                json_renderer_end(json);
            }
        }
        json_renderer_end(json);

        if (!has_crude) {
            has_crude = true;
            json_renderer_write_key(json, "crude", JSON_RENDERER_FLAGS_NONE);
            json_renderer_begin(json, JSON_ELEMENT_TYPE_OBJECT);
            {
                static const char * display_title_keys[] = {"TITLE", MP3_METADATA_TITLE};
                const char *display_title = NULL;
                for (size_t i = 0; i < (sizeof(display_title_keys)/sizeof(*display_title_keys)); i++) {
                    display_title = vorbis_comment_query(event->vc, display_title_keys[i], 0);
                    if (display_title)
                        break;
                }
                json_renderer_write_key(json, "display-title", JSON_RENDERER_FLAGS_NONE);
                if (display_title) {
                    json_renderer_write_string(json, display_title, JSON_RENDERER_FLAGS_NONE);
                } else {
                    json_renderer_write_null(json);
                }
                if (event->source_instance_uuid) {
                    json_renderer_write_key(json, "source-instance", JSON_RENDERER_FLAGS_NONE);
                    json_renderer_write_string(json, event->source_instance_uuid, JSON_RENDERER_FLAGS_NONE);
                }
            }
            json_renderer_end(json);
        }
    }

    json_renderer_end(json);

    body = json_renderer_finish(&json);

    string_renderer_start_list(renderer, "\r\n", ": ", false, false, STRING_RENDERER_ENCODING_PLAIN);
    string_renderer_add_kv(renderer, "id", event->uuid);
    string_renderer_add_kv(renderer, "data", body);
    string_renderer_end_list(renderer);
    string_renderer_add_string(renderer, "\r\n\r\n");

    free(body);

    igloo_sp_replace(string_renderer_to_string_zero_copy(renderer), &(event->rendered), igloo_instance);
    event->rendered_length = strlen(event->rendered);
    igloo_ro_unref(&renderer);
}
