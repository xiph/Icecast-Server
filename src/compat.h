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

/* compat.h
 * 
 * This file contains most of the ugliness for header portability
 * and common types across various systems like Win32, Linux and
 * Solaris.
 */

/* Make sure we define 64 bit types */
#ifdef _WIN32
#  define int64_t __int64
#  define uint64_t unsigned __int64
#  define uint32_t unsigned int
#else
#  ifdef HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif
