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

#include <stddef.h> /* for NULL */
#include <stdio.h> /* for snprintf() */

#include "ctest_lib.h"

#include "../src/resourcematch.h"

struct test {
    const char *pattern;
    const char *string;
    resourcematch_result_t expected_result;
};

static const struct test tests[] = {
    {NULL,      NULL,       RESOURCEMATCH_ERROR},
    {"",        NULL,       RESOURCEMATCH_ERROR},
    {NULL,      "",         RESOURCEMATCH_ERROR},
    {"",        "",         RESOURCEMATCH_MATCH},
    {"a",       "a",        RESOURCEMATCH_MATCH},
    {"aa",      "b",        RESOURCEMATCH_NOMATCH},
    {"aa",      "ab",       RESOURCEMATCH_NOMATCH},
    {"aa",      "ba",       RESOURCEMATCH_NOMATCH},
    {"aa",      "aa",       RESOURCEMATCH_MATCH},
    {"a/a",     "a/a",      RESOURCEMATCH_MATCH},
    {"a/%%a",   "a/%a",     RESOURCEMATCH_MATCH},

    {"a/%i",    "a/0",      RESOURCEMATCH_MATCH},
    {"a/%i",    "a/1",      RESOURCEMATCH_MATCH},

    {"a/%i",    "a/12",     RESOURCEMATCH_MATCH},
    {"a/%i",    "a/0x12",   RESOURCEMATCH_MATCH},
    {"a/%i",    "a/012",    RESOURCEMATCH_MATCH},

    {"a/%d",    "a/12",     RESOURCEMATCH_MATCH},
    {"a/%d",    "a/0x12",   RESOURCEMATCH_NOMATCH},
    {"a/%d",    "a/012",    RESOURCEMATCH_MATCH},

    {"a/%x",    "a/12",     RESOURCEMATCH_MATCH},
    {"a/%x",    "a/0x12",   RESOURCEMATCH_MATCH},
    {"a/%x",    "a/012",    RESOURCEMATCH_MATCH},

    {"a/%o",    "a/12",     RESOURCEMATCH_MATCH},
    {"a/%o",    "a/0x12",   RESOURCEMATCH_NOMATCH},
    {"a/%o",    "a/012",    RESOURCEMATCH_MATCH},

    {"a/%i/b",  "a/X/b",    RESOURCEMATCH_NOMATCH},

    {"a/%d/b",  "a/12/b",   RESOURCEMATCH_MATCH},
    {"a/%d/b",  "a/0x12/b", RESOURCEMATCH_NOMATCH},
    {"a/%d/b",  "a/012/b",  RESOURCEMATCH_MATCH},

    {"a/%x/b",  "a/12/b",   RESOURCEMATCH_MATCH},
    {"a/%x/b",  "a/0x12/b", RESOURCEMATCH_MATCH},
    {"a/%x/b",  "a/012/b",  RESOURCEMATCH_MATCH},

    {"a/%o/b",  "a/12/b",   RESOURCEMATCH_MATCH},
    {"a/%o/b",  "a/0x12/b", RESOURCEMATCH_NOMATCH},
    {"a/%o/b",  "a/012/b",  RESOURCEMATCH_MATCH},

    {"a/%i/%i/b", "a/1/2/b", RESOURCEMATCH_MATCH}
};

static const char *res2str(resourcematch_result_t res)
{
    switch (res) {
        case RESOURCEMATCH_ERROR:
            return "error";
        break;
        case RESOURCEMATCH_MATCH:
            return "match";
        break;
        case RESOURCEMATCH_NOMATCH:
            return "nomatch";
        break;
        default:
            return "<unknown>";
        break;
    }
}

static inline resourcematch_result_t run_test_base(const struct test *test, resourcematch_extract_t **extract)
{
    char name[128];
    resourcematch_result_t ret;
    int ok = 1;

    ret = resourcematch_match(test->pattern, test->string, extract);

    //printf(" expected %s, got %s", res2str(test->expected_result), res2str(ret));

    if (extract) {
        if (test->expected_result == RESOURCEMATCH_MATCH) {
            if (*extract) {
                ctest_diagnostic(" got extract");
            } else {
                ctest_diagnostic(" got no extract");
                ok = 0;
            }
        }
    }

    snprintf(name, sizeof(name), "pattern \"%s\" and string \"%s\" %s extract", test->pattern, test->string, extract ? "with" : "without");
    ctest_test(name, test->expected_result == ret && ok);

    return ret;
}

static inline void print_extract_group(resourcematch_extract_t *extract, size_t idx)
{
    switch (extract->group[idx].type) {
        case 'i':
        case 'd':
        case 'x':
        case 'o':
            ctest_diagnostic_printf("   Group %zu, type \"%c\": value is %lli", idx, extract->group[idx].type, extract->group[idx].result.lli);
        break;
        default:
            ctest_diagnostic_printf("   Group %zu, type \"%c\": <raw value at %p>", idx, extract->group[idx].type, extract->group[idx].raw);
        break;
    }
}

static inline void print_extract(resourcematch_extract_t *extract)
{
    size_t i;

    ctest_diagnostic_printf("  Extract with %zu groups:", extract->groups);

    for (i = 0; i < extract->groups; i++) {
        print_extract_group(extract, i);
    }
}

static void run_test(const struct test *test)
{
    resourcematch_result_t ret;
    resourcematch_extract_t *extract = NULL;

    run_test_base(test, NULL);

    ret = run_test_base(test, &extract);
    if (extract) {
        if (ret == RESOURCEMATCH_MATCH)
            print_extract(extract);

        resourcematch_extract_free(extract);
    }
}

int main (void)
{
    size_t i;

    ctest_init();
    for (i = 0; i < (sizeof(tests)/sizeof(*tests)); i++) {
        run_test(&(tests[i]));
    }
    ctest_fin();

    return 0;
}
