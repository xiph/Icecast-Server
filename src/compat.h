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
#else
#  ifdef HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif
