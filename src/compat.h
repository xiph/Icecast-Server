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
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __COMPAT_H__
#define __COMPAT_H__

/* compat.h
 * 
 * This file contains most of the ugliness for header portability
 * and common types across various systems like Win32, Linux and
 * Solaris.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else
#  ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else
#    include <time.h>
#  endif
#endif

#ifdef _WIN32
#  define PATH_SEPARATOR "\\"
#else
#  define PATH_SEPARATOR "/"
#endif

/* Make sure we define 64 bit types */
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#if defined(_WIN32) && !defined(HAVE_STDINT_H) && !defined(HAVE_INTTYPES_H)
#  define size_t unsigned int
#  define ssize_t int
#  define int64_t __int64
#  define uint64_t unsigned __int64
#  define int32_t __int32
#  define uint32_t unsigned __int32
#  define PRIu64  "I64u"
#  define PRId64  "I64d"
#endif

/* some defaults if not provided above */
#ifndef SCNdMAX
#  define SCNdMAX "lld"
#endif

#endif /* __COMPAT_H__ */

