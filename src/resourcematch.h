/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __RESOURCEMATCH_H__
#define __RESOURCEMATCH_H__

#include <sys/types.h>

typedef enum {
    RESOURCEMATCH_ERROR,
    RESOURCEMATCH_MATCH,
    RESOURCEMATCH_NOMATCH
} resourcematch_result_t;

typedef struct {
    char type;
    char *raw;
    union {
        const char *string;
        long long int lli;
    } result;
} resourcematch_group_t;

typedef struct {
    size_t groups;
    resourcematch_group_t *group;
} resourcematch_extract_t;

resourcematch_result_t resourcematch_match(const char *pattern, const char *string, resourcematch_extract_t **extract);
void resourcematch_extract_free(resourcematch_extract_t *extract);

#endif
