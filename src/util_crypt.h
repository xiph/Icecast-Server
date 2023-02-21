/* Icecast
 *
 *  Copyright 2023      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 */

#ifndef __UTIL_CRYPT_H__
#define __UTIL_CRYPT_H__

#include <stdbool.h>

char * util_crypt_hash(const char *pw);
bool   util_crypt_check(const char *plain, const char *crypted);
bool   util_crypt_is_supported(const char *prefix);
bool   util_crypt_is_new_secure(void);

/* Exported for tests only!: */
char * util_crypt_hash_oldstyle(const char *pw);

#endif  /* __UTIL_CRYPT_H__ */
