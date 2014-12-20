/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <curl/curl.h>

#include "event.h"
#include "logging.h"
#define CATMODULE "event_url"


typedef struct event_url {
    char *url;
    char *action;
    char *userpwd;
    CURL *handle;
    char errormsg[CURL_ERROR_SIZE];
} event_url_t;

#ifdef CURLOPT_PASSWDFUNCTION
/* make sure that prompting at the console does not occur */
static int my_getpass(void *client, char *prompt, char *buffer, int buflen) {
    buffer[0] = '\0';
    return 0;
}
#endif
static size_t handle_returned (void *ptr, size_t size, size_t nmemb, void *stream) {
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
    char *url;
    char *action, *mount, *server, *role, *username, *ip, *agent;
    time_t duration;
    char post[4096];

    action   = util_url_escape(self->action ? self->action : event->trigger);
    mount    = __escape(event->uri, "");
    role     = __escape(event->client_role, "");
    username = __escape(event->client_username, "");
    ip       = __escape(event->connection_ip, "");
    agent    = __escape(event->client_useragent, "-");

    if (event->connection_time) {
        duration = time(NULL) - event->connection_time;
    } else {
        duration = 0;
    }

    config = config_get_config();
    server   = __escape(config->hostname, "");

    snprintf (post, sizeof (post),
            "action=%s&mount=%s&server=%s&port=%d&client=%lu&role=%s&username=%s&ip=%s&agent=%s&duration=%lli&admin=%i",
            action, mount, server, config->port,
            event->connection_id, role, username, ip, agent, (long long int)duration, event->client_admin_command);
    config_release_config();

    free(action);
    free(mount);
    free(server);
    free(role);
    free(username);
    free(ip);
    free(agent);

    if (strchr(self->url, '@') == NULL && self->userpwd) {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, self->userpwd);
    } else {
        curl_easy_setopt(self->handle, CURLOPT_USERPWD, "");
    }

    /* REVIEW: Why do we dup the url here?
     * auth_url.c does it this way. I just copied the code.
     * Maybe there is a reason for it, maybe not?
     * -- Philipp Schafft, 2014-12-07.
     */
    url = strdup(self->url);

    curl_easy_setopt(self->handle, CURLOPT_URL, url);
    curl_easy_setopt(self->handle, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt(self->handle, CURLOPT_WRITEHEADER, NULL);

    if (curl_easy_perform(self->handle))
        ICECAST_LOG_WARN("auth to server %s failed with %s", url, self->errormsg);

    free(url);

    return 0;
}

static void event_url_free(void *state) {
    event_url_t *self = state;
    if (self->handle)
        curl_easy_cleanup(self->handle);
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
            /* BEFORE RELEASE 2.4.2 DOCUMENT: Document supported options:
             * <option name="url" value="..." />
             * <option name="username" value="..." />
             * <option name="password" value="..." />
             * <option name="action" value="..." />
             */
            if (strcmp(options->name, "url") == 0) {
                free(self->url);
                self->url = NULL;
                if (options->value)
                    self->url = strdup(options->value);
            } else if (strcmp(options->name, "username") == 0) {
                username = options->value;
            } else if (strcmp(options->name, "password") == 0) {
                password = options->value;
            } else if (strcmp(options->name, "action") == 0) {
                free(self->url);
                self->action = NULL;
                if (options->value)
                    self->action = strdup(options->value);
            } else {
                ICECAST_LOG_ERROR("Unknown <option> tag with name %s.", options->name);
            }
        } while ((options = options->next));
    }

    self->handle = curl_easy_init();

    /* check if we are in sane state */
    if (!self->url || !self->handle) {
        event_url_free(self);
        return -1;
    }

    curl_easy_setopt(self->handle, CURLOPT_USERAGENT, ICECAST_VERSION_STRING);
    curl_easy_setopt(self->handle, CURLOPT_HEADERFUNCTION, handle_returned);
    curl_easy_setopt(self->handle, CURLOPT_WRITEFUNCTION, handle_returned);
    curl_easy_setopt(self->handle, CURLOPT_WRITEDATA, self->handle);
    curl_easy_setopt(self->handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(self->handle, CURLOPT_TIMEOUT, 15L);
#ifdef CURLOPT_PASSWDFUNCTION
    curl_easy_setopt(self->handle, CURLOPT_PASSWDFUNCTION, my_getpass);
#endif
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
