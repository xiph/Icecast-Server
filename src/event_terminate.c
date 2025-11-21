/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include "event.h"
#include "global.h"
#include "logging.h"
#define CATMODULE "event-terminate"

static int event_terminate_emit(void *state, event_t *event)
{
    ICECAST_LOG_INFO("Terminating due to event (%s)", event->trigger);
    global.running = ICECAST_HALTING;
    return 0;
}

int event_get_terminate(event_registration_t *er, config_options_t *options)
{
    er->emit = event_terminate_emit;
    er->free = NULL;
    return 0;
}
