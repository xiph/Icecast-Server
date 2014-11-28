/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __AUTH_ANONYMOUS_H__
#define __AUTH_ANONYMOUS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

int auth_get_anonymous_auth (auth_t *auth, config_options_t *options);

#endif
