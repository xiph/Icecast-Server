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

#ifndef __COMPAT_H__
#define __COMPAT_H__

/* compat.h
 * 
 * This file contains most of the ugliness for header portability
 * and common types across various systems like Win32, Linux and
 * Solaris.
 */

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

/* Make sure we define 64 bit types */
#ifdef _WIN32
#  define PATH_SEPARATOR "\\"
#  define size_t unsigned int
#  define ssize_t int
#  define int64_t __int64
#  define uint64_t unsigned __int64
#  define uint32_t unsigned int
#else
#  define PATH_SEPARATOR "/"
#  if defined(HAVE_STDINT_H)
#    include <stdint.h>
#  elif defined(HAVE_INTTYPES_H)
#    include <inttypes.h>
#  endif
#endif

#ifdef _WIN32
#define FORMAT_INT64      "%I64d"
#define FORMAT_UINT64     "%I64u"
#else
#define FORMAT_INT64      "%lld"
#define FORMAT_UINT64     "%llu"
#endif

#endif /* __COMPAT_H__ */

