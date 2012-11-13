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
 * Copyright 2012,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __ROARAPI_H__
#define __ROARAPI_H__

#include "cfgfile.h"

#ifdef HAVE_ROARAUDIO

// Bad workaround on for win32:
// Both icecast and libroar define socklen_t as borken win* does not provide it.
#ifndef HAVE_SOCKLEN_T
#define ROAR_HAVE_T_SOCKLEN_T
#endif
#include <roaraudio.h>
#endif

void roarapi_initialize(void);
void roarapi_shutdown(void);
void roarapi_lock(void);
void roarapi_unlock(void);

#endif  /* __ROARAPI_H__ */
