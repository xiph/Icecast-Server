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

#include "ctest_lib.h"

static size_t   ctest_g_test_num;
static int      ctest_g_bailed_out;

void ctest_init(void)
{
    ctest_g_test_num = 0;
    ctest_g_bailed_out = 0;
}

void ctest_fin(void)
{
    printf("1..%zu\n", ctest_g_test_num);
}

void ctest_test(const char *desc, int res)
{
    const char *prefix = NULL;

    if (ctest_bailed_out())
        return;

    ctest_g_test_num++;

    if (res) {
        prefix = "ok";
    } else {
        prefix = "not ok";
    }

    if (desc) {
        printf("%s %zu %s\n", prefix, ctest_g_test_num, desc);
    } else {
        printf("%s %zu\n", prefix, ctest_g_test_num);
    }
}

void ctest_diagnostic(const char *line)
{
    printf("# %s\n", line);
}

void ctest_bail_out(const char *reason)
{
    ctest_g_bailed_out = 1;
    if (reason) {
        printf("Bail out! %s\n", reason);
    } else {
        printf("Bail out!\n");
    }
}

int  ctest_bailed_out(void)
{
    return ctest_g_bailed_out;
}
