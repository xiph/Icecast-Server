/* Icecast
 *
 *  Copyright 2000-2004 Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 *  Copyright 2012-2022 Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 */

/* functions defined in here work on strings and do not depend on anything but:
 * A) standard C runtime,
 * B) libigloo,
 * C) other functions defined here.
 *
 * All those functions are candidates for migration to libigloo.
 */

#ifndef __UTIL_STRING_H__
#define __UTIL_STRING_H__

#include <stdbool.h>

char *util_base64_encode(const char *data, size_t len);
char *util_base64_decode(const char *input);
char *util_bin_to_hex(unsigned char *data, int len);
char *util_url_unescape(const char *src);
char *util_url_escape(const char *src);

int util_replace_string(char **dst, const char *src);
bool util_replace_string_url_escape(char **dst, const char *src); /* returns true on success */
int util_strtolower(char *str);

/* Supports wildcards, supports negatives matches. */
bool util_is_in_list(const char *list, const char *needle);

#endif  /* __UTIL_STRING_H__ */
