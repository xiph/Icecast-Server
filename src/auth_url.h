/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

#ifndef __AUTH_URL_H__
#define __AUTH_URL_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

int auth_get_url_auth (auth_t *authenticator, config_options_t *options);

#endif


