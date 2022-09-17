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
    char *action, *mount, *server, *role, *username, *ip, *agent, *media_type;
    time_t duration;
    char post[4096];

    action      = util_url_escape(self->action ? self->action : event->trigger);
    mount       = __escape(event_extra_get(event, EVENT_EXTRA_KEY_URI), "");
    role        = __escape(event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_ROLE), "");
    username    = __escape(event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERNAME), "");
    ip          = __escape(event_extra_get(event, EVENT_EXTRA_KEY_CONNECTION_IP), "");
    agent       = __escape(event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERAGENT), "-");
    media_type  = __escape(event_extra_get(event, EVENT_EXTRA_KEY_CLIENT_USERAGENT), "-");

    if (event->connection_time) {
        duration = time(NULL) - event->connection_time;
    } else {
        duration = 0;
    }

    config = config_get_config();
    server   = __escape(config->hostname, "");

    snprintf (post, sizeof (post),
            "action=%s&mount=%s&server=%s&port=%d&client=%lu&role=%s&username=%s&ip=%s&agent=%s&duration=%lli&admin=%i&source-media-type=%s",
            action, mount, server, config->port,
            event->connection_id, role, username, ip, agent, (long long int)duration, event->client_admin_command,
            media_type
            );
    config_release_config();

    free(action);
    free(mount);
    free(server);
    free(role);
    free(username);
    free(ip);
    free(agent);
    free(media_type);

    if (strchr(self->url, '@') == NULL && self->userpwd) {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, self->userpwd);
    } else {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, "");
    }

    curl_easy_setopt(self->handle, CURLOPT_URL, self->url);
    curl_easy_setopt(self->handle, CURLOPT_POSTFIELDS, post);

    if (curl_easy_perform(self->handle))
        ICECAST_LOG_WARN("auth to server %s failed with %s", self->url, self->errormsg);

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
