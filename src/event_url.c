/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "icecasttypes.h"
#include <igloo/ro.h>
#include <igloo/error.h>

#include "global.h"  /* for igloo_instance */
#include "string_renderer.h"
#include "curl.h"
#include "event.h"
#include "cfgfile.h"
#include "util.h"
#include "logging.h"
#define CATMODULE "event_url"


typedef struct event_url {
    char *url;
    char *action;
    char *userpwd;
    CURL *handle;
    char errormsg[CURL_ERROR_SIZE];
} event_url_t;

static size_t handle_returned (void *ptr, size_t size, size_t nmemb, void *stream) {
    (void)ptr, (void)stream;
    return size * nmemb;
}

static inline char *__escape(const char *src, const char *default_value) {
    if (src)
        return util_url_escape(src);

    return strdup(default_value);
}

static int event_url_emit(void *state, event_t *event) {
    event_url_t *self = state;
    ice_config_t *config;
    time_t duration;
    string_renderer_t * renderer;

    if (igloo_ro_new(&renderer, string_renderer_t, igloo_instance) != igloo_ERROR_NONE)
        return 0;

    if (event->client_data) {
        duration = time(NULL) - event->connection_time;
    } else {
        duration = 0;
    }

    string_renderer_start_list_formdata(renderer);
    /* Old style */
    string_renderer_add_kv_with_options(renderer, "action", self->action ? self->action : event->trigger, STRING_RENDERER_ENCODING_PLAIN, false, false);
    string_renderer_add_kv_with_options(renderer, "mount", event_extra_get(event, EVENT_EXTRA_KEY_URI), STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_ki_with_options(renderer, "client", event->connection_id, STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_kv_with_options(renderer, "role", event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_ROLE), STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_kv_with_options(renderer, "username", event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERNAME), STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_kv_with_options(renderer, "ip", event_extra_get(event, EVENT_EXTRA_KEY_CONNECTION_IP), STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_kv_with_options(renderer, "agent", event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERAGENT) ? event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERAGENT) : "-", STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_ki_with_options(renderer, "duration", duration, STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_ki_with_options(renderer, "admin", event->client_admin_command, STRING_RENDERER_ENCODING_PLAIN, true, true);

    /* new style */
    event_to_string_renderer(event, renderer);

    /* common */
    config = config_get_config();
    string_renderer_add_kv_with_options(renderer, "server", config->hostname, STRING_RENDERER_ENCODING_PLAIN, true, true);
    string_renderer_add_ki_with_options(renderer, "port", config->port, STRING_RENDERER_ENCODING_PLAIN, true, true);
    config_release_config();

    string_renderer_end_list(renderer);


    if (strchr(self->url, '@') == NULL && self->userpwd) {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, self->userpwd);
    } else {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, "");
    }

    curl_easy_setopt(self->handle, CURLOPT_URL, self->url);
    curl_easy_setopt(self->handle, CURLOPT_POSTFIELDS, string_renderer_to_string_zero_copy(renderer));

    if (curl_easy_perform(self->handle))
        ICECAST_LOG_WARN("auth to server %s failed with %s", self->url, self->errormsg);

    igloo_ro_unref(&renderer);

    return 0;
}

static void event_url_free(void *state) {
    event_url_t *self = state;
    icecast_curl_free(self->handle);
    free(self->url);
    free(self->action);
    free(self->userpwd);
    free(self);
}

int event_get_url(event_registration_t *er, config_options_t *options) {
    event_url_t *self = calloc(1, sizeof(event_url_t));
    const char *username = NULL;
    const char *password = NULL;

    if (!self)
        return -1;

    if (options) {
        do {
            if (options->type)
                continue;
            if (!options->name)
                continue;
            /* BEFORE RELEASE 2.5.0 DOCUMENT: Document supported options:
             * <option name="url" value="..." />
             * <option name="username" value="..." />
             * <option name="password" value="..." />
             * <option name="action" value="..." />
             */
            if (strcmp(options->name, "url") == 0) {
                util_replace_string(&(self->url), options->value);
            } else if (strcmp(options->name, "username") == 0) {
                username = options->value;
            } else if (strcmp(options->name, "password") == 0) {
                password = options->value;
            } else if (strcmp(options->name, "action") == 0) {
                util_replace_string(&(self->action), options->value);
            } else {
                ICECAST_LOG_ERROR("Unknown <option> tag with name %s.", options->name);
            }
        } while ((options = options->next));
    }

    self->handle = icecast_curl_new(NULL, NULL);

    /* check if we are in sane state */
    if (!self->url || !self->handle) {
        event_url_free(self);
        return -1;
    }

    curl_easy_setopt(self->handle, CURLOPT_HEADERFUNCTION, handle_returned);
    curl_easy_setopt(self->handle, CURLOPT_ERRORBUFFER, self->errormsg);

    if (username && password) {
        size_t len = strlen(username) + strlen(password) + 2;
        self->userpwd = malloc(len);
        if (!self->userpwd) {
            event_url_free(self);
            return -1;
        }
        snprintf(self->userpwd, len, "%s:%s", username, password);
    }

    er->state = self;
    er->emit = event_url_emit;
    er->free = event_url_free;
    return 0;
}
