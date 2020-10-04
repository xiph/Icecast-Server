/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * This file contains functions for rendering XML as JSON.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xml2json.h"
#include "json.h"

#include "logging.h"
#define CATMODULE "xml2json"
