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

#include "event.h"
#include "global.h"  /* for igloo_instance */
#include "string_renderer.h"
#include "util.h"
#include "cfgfile.h"
#include "logging.h"
#define CATMODULE "event_log"


typedef struct event_log {
    char *prefix;
    int level;
} event_log_t;


static int event_log_emit(void *state, event_t *event) {
    event_log_t *self = state;
    string_renderer_t * renderer;

    if (igloo_ro_new(&renderer, string_renderer_t, igloo_instance) != igloo_ERROR_NONE)
        return 0;

    string_renderer_start_list(renderer, " ", "=", false, false, STRING_RENDERER_ENCODING_H_ALT_SPACE);
    event_to_string_renderer(event, renderer);
    string_renderer_end_list(renderer);

    ICECAST_LOG(self->level, ICECAST_LOGFLAG_NONE,
                             "%s%s%s",
                self->prefix ? self->prefix : "", self->prefix ? ": " : "",
                string_renderer_to_string_zero_copy(renderer));

    igloo_ro_unref(&renderer);
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
                util_replace_string(&(self->prefix), options->value);
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
