/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2024     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __EVENT_STREAM_H__
#define __EVENT_STREAM_H__

#include "icecasttypes.h"

igloo_RO_FORWARD_TYPE(event_stream_event_t);

void event_stream_initialise(void);
void event_stream_shutdown(void);

void event_stream_add_client(client_t *client);
void event_stream_emit_event(event_t *event);

#endif
