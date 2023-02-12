/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2023, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <igloo/tap.h>

#include "../md5.h"
#include "../util_string.h"

void test_md5_hash(const char *in, const char *expect, bool positive)
{
    struct MD5Context context;
    unsigned char digest[16];
    char *out;

    MD5Init(&context);

    MD5Update(&context, (const unsigned char *)in, strlen(in));

    MD5Final(digest, &context);

    out = util_bin_to_hex(digest, 16);

    if (positive) {
        igloo_tap_test("md5 positive vector", strcmp(out, expect) == 0);
    } else {
        igloo_tap_test("md5 negative vector", strcmp(out, expect) != 0);
    }

    free(out);
}

static void test_md5(void)
{
    struct vector {
        const char *in;
        const char *out;
    };
    static const struct vector table_pos[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"\n", "68b329da9893e34099c7d8ad5cb9c940"}
    };
    static const struct vector table_neg[] = {
        {"XXX", "d41d8cd98f00b204e9800998ecf8427e"},
        {"YYY", "1234567890abcdef1234567890abcdef"}
    };
    size_t i;

    for (i = 0; i < (sizeof(table_pos)/sizeof(*table_pos)); i++) {
        test_md5_hash(table_pos[i].in, table_pos[i].out, true);
    }

    for (i = 0; i < (sizeof(table_neg)/sizeof(*table_neg)); i++) {
        test_md5_hash(table_neg[i].in, table_neg[i].out, false);
    }
}

int main (void)
{
    igloo_tap_init();
    igloo_tap_exit_on(igloo_TAP_EXIT_ON_FIN, NULL);
    igloo_tap_group_run("md5 vectors", test_md5);
    igloo_tap_fin();

    return EXIT_FAILURE; // return failure as we should never reach this point!
}

