/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <stdlib.h>
#include <errno.h>

#include "resourcematch.h"

static size_t count_groups(const char *pattern)
{
    size_t ret = 0;

    while (*pattern) {
        for (; *pattern && *pattern != '%'; pattern++);

        if (!*pattern) {
            return ret;
        }

        pattern++;

        if (!*pattern)
            return ret;

        if (*pattern != '%')
            ret++;

        pattern++;
    }

    return ret;
}

static resourcematch_extract_t * allocate_extract(const char *pattern)
{
    size_t groups = count_groups(pattern);
    resourcematch_extract_t *ret;

    ret = calloc(1, sizeof(*ret));
    if (!ret)
        return NULL;

    ret->groups = groups;
    ret->group = calloc(groups, sizeof(*ret->group));

    return ret;
}

static void strip_common_prefix(const char **pattern, const char **string)
{
    const char *p = *pattern;
    const char *s = *string;

    for (; *p && *p != '%' && *p == *s; p++, s++);

    *pattern = p;
    *string = s;
}

static inline void setup_group(resourcematch_extract_t *extract, size_t idx, char type)
{
    if (!extract)
        return;

    extract->group[idx].type = type;
    extract->group[idx].raw = NULL;
}

static inline resourcematch_result_t match_lli(const char **string, resourcematch_extract_t *extract, size_t idx, int base)
{
    long long int ret;
    char *endptr;

    errno = 0;
    ret = strtoll(*string, &endptr, base);
    if (errno != 0)
        return RESOURCEMATCH_ERROR;

    if (extract) {
        extract->group[idx].result.lli = ret;
    }

    *string = endptr;

    return RESOURCEMATCH_MATCH;
}

resourcematch_result_t resourcematch_match(const char *pattern, const char *string, resourcematch_extract_t **extract)
{
    resourcematch_result_t ret;
    resourcematch_extract_t *matches = NULL;
    size_t idx = 0;

    if (!pattern || !string)
        return RESOURCEMATCH_ERROR;

    if (extract) {
        matches = allocate_extract(pattern);
        if (!matches)
            return RESOURCEMATCH_NOMATCH;
    }

    while (1) {
        strip_common_prefix(&pattern, &string);

        if (!*pattern && !*string) {
            if (extract)
                *extract = matches;

            return RESOURCEMATCH_MATCH;
        } else if (!*pattern || !*string) {
            if (extract)
                resourcematch_extract_free(matches);

            return RESOURCEMATCH_NOMATCH;
        }

        if (*pattern != '%') {
            if (extract)
                resourcematch_extract_free(matches);

            return RESOURCEMATCH_NOMATCH;
        }

        pattern++;

        switch (*pattern) {
            case '%':
                if (*string == '%') {
                    string++;
                } else {
                    if (extract)
                        resourcematch_extract_free(matches);

                    return RESOURCEMATCH_NOMATCH;
                }
            break;
#define _test_int(type,base) \
            case (type): \
                setup_group(matches, idx, *pattern); \
\
                ret = match_lli(&string, matches, idx, (base)); \
                if (ret != RESOURCEMATCH_MATCH) { \
                    if (extract) \
                        resourcematch_extract_free(matches); \
\
                    return ret; \
                } \
                idx++; \
            break;

            _test_int('i', 0);
            _test_int('d', 10);
            _test_int('x', 16);
            _test_int('o', 8);

            default:
                if (extract)
                    resourcematch_extract_free(matches);

                return RESOURCEMATCH_ERROR;
            break;
        }

        pattern++;
    }
}

void resourcematch_extract_free(resourcematch_extract_t *extract)
{
    size_t i;

    if (!extract)
        return;

    for (i = 0; i < extract->groups; i++) {
        free(extract->group[i].raw);
    }

    free(extract->group);
    free(extract);
}
