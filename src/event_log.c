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

#include "event.h"
#include "logging.h"
#define CATMODULE "event_log"


typedef struct event_log {
    char *prefix;
    int level;
} event_log_t;

static int event_log_emit(void *state, event_t *event) {
    event_log_t *self = state;

    ICECAST_LOG(self->level, "%s%strigger=\"%s\" uri=\"%s\" "
                             "connection_id=%lu connection_ip=\"%s\" connection_time=%lli "
                             "client_role=\"%s\" client_username=\"%s\" client_useragent=\"%s\" client_admin_command=%i",
                self->prefix ? self->prefix : "", self->prefix ? ": " : "",
                event->trigger,
                event->uri,
                event->connection_id, event->connection_ip, (long long int)event->connection_time,
                event->client_role, event->client_username, event->client_useragent, event->client_admin_command);
    return 0;
}

static void event_log_free(void *state) {
    event_log_t *self = state;
    free(self->prefix);
    free(self);
}

int event_get_log(event_registration_t *er, config_options_t *options) {
    event_log_t *self = calloc(1, sizeof(event_log_t));

    if (!self)
        return -1;

    self->prefix = strdup("Event");
    self->level  = ICECAST_LOGLEVEL_INFO;

    if (options) {
        do {
            if (options->type)
                continue;
            if (!options->name)
                continue;
            /* BEFORE RELEASE 2.5.0 DOCUMENT: Document supported options:
             * <option name="prefix" value="..." />
             * <option name="level" value="..." />
             */
            if (strcmp(options->name, "prefix") == 0) {
                free(self->prefix);
                self->prefix = NULL;
                if (options->value)
                    self->prefix = strdup(options->value);
            } else if (strcmp(options->name, "level") == 0) {
                self->level = ICECAST_LOGLEVEL_INFO;
                if (options->value)
                    self->level = util_str_to_loglevel(options->value);
            } else {
                ICECAST_LOG_ERROR("Unknown <option> tag with name %s.", options->name);
            }
        } while ((options = options->next));
    }

    er->state = self;
    er->emit = event_log_emit;
    er->free = event_log_free;
    return 0;
}
