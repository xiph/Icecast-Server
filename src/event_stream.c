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

#include <igloo/ro.h>
#include <igloo/error.h>

#include "event_stream.h"
#include "string_renderer.h"
#include "json.h"
#include "global.h"
#include "errors.h"
#include "logging.h"
#define CATMODULE "event-stream"

