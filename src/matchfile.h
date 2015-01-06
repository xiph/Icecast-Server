/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __MATCHFILE_H__
#define __MATCHFILE_H__

struct matchfile_tag;
typedef struct matchfile_tag matchfile_t;

matchfile_t *matchfile_new(const char *filename);
int          matchfile_addref(matchfile_t *file);
int          matchfile_release(matchfile_t *file);
int          matchfile_match(matchfile_t *file, char *key);

/* returns 1 for allow or pass and 0 for deny */
int          matchfile_match_allow_deny(matchfile_t *allow, matchfile_t *deny, char *key);

#endif  /* __MATCHFILE_H__ */
