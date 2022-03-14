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

#include <string.h>
#include <stdarg.h>
#include <stdlib.h> /* for EXIT_FAILURE */
#include <stdio.h>

#include <igloo/tap.h>

#include "../src/buffer.h"
#include "../src/refobject.h"

static void ctest_diagnostic_printf(const char *format, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);

    vsnprintf(buf, sizeof(buf), format, ap);

    va_end(ap);

    igloo_tap_diagnostic(buf);
}

static void test_create_ref_unref(void)
{
    buffer_t *a;

    a = buffer_new(-1, NULL, NULL, REFOBJECT_NULL);
    igloo_tap_test("buffer created", a != NULL);
    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);
    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

}

static void test_name(void)
{
    buffer_t *a;
    const char *name = "test object name";
    const char *ret;

    a = buffer_new(-1, NULL, name, REFOBJECT_NULL);
    igloo_tap_test("buffer created", a != NULL);

    ret = refobject_get_name(a);
    igloo_tap_test("get name", ret != NULL);
    igloo_tap_test("name match", strcmp(name, ret) == 0);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_userdata(void)
{
    buffer_t *a;
    int tmp = 0;
    void *userdata = &tmp;
    void *ret;

    a = buffer_new(-1, NULL, NULL, REFOBJECT_NULL);
    igloo_tap_test("buffer created", a != NULL);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == NULL);
    igloo_tap_test("set userdata", refobject_set_userdata(a, userdata) == 0);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == userdata);
    igloo_tap_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == NULL);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

    a = buffer_new(-1, userdata, NULL, REFOBJECT_NULL);
    igloo_tap_test("buffer created", a != NULL);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == userdata);
    igloo_tap_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == NULL);
    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_associated(void)
{
    refobject_base_t *a;
    buffer_t *b;

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));


    b = buffer_new(-1, NULL, NULL, a);
    igloo_tap_test("buffer created with associated", !REFOBJECT_IS_NULL(b));

    igloo_tap_test("un-referenced (1 of 2)", refobject_unref(b) == 0);
    igloo_tap_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static void test_empty(void)
{
    buffer_t *a;
    const void *data = &data;
    size_t length = 5;
    const char *string;
    int ret;

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);

    ret = buffer_get_data(a, &data, &length);
    igloo_tap_test("got data and length from buffer", ret == 0);
    if (ret == 0) {
        igloo_tap_test("data is updated", data != &data);
        igloo_tap_test("length is zero", length == 0);
    }

    data = &data;
    ret = buffer_get_data(a, &data, NULL);
    igloo_tap_test("got data from buffer", ret == 0);
    if (ret == 0) {
        igloo_tap_test("data is updated", data != &data);
    }

    length = 5;
    ret = buffer_get_data(a, NULL, &length);
    igloo_tap_test("got length from buffer", ret == 0);
    if (ret == 0) {
        igloo_tap_test("length is zero", length == 0);
    }

    ret = buffer_get_string(a, &string);
    igloo_tap_test("got string from buffer", ret == 0);
    if (ret == 0) {
        igloo_tap_test("string is non-NULL", string != NULL);
        if (string != NULL) {
            igloo_tap_test("string is empty", *string == 0);
        }
    }

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_string(void)
{
    buffer_t *a;
    const char *hw = "Hello World!";
    const char *count = "0 1 2 3 4";
    const char *combined = "Hello World!" "0 1 2 3 4";
    const char *string = NULL;
    int ret;

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);
    igloo_tap_test("pushed string", buffer_push_string(a, hw) == 0);
    ret = buffer_get_string(a, &string);
    igloo_tap_test("got strong", ret == 0);
    if (ret == 0) {
        igloo_tap_test("string is non-NULL", string != NULL);
        if (string != NULL) {
            igloo_tap_test("string matches input", strcmp(string, hw) == 0);
        }
    }

    igloo_tap_test("pushed string", buffer_push_string(a, count) == 0);
    string = NULL;
    ret = buffer_get_string(a, &string);
    igloo_tap_test("got strong", ret == 0);
    if (ret == 0) {
        igloo_tap_test("string is non-NULL", string != NULL);
        if (string != NULL) {
            igloo_tap_test("string matches combined input", strcmp(string, combined) == 0);
        }
    }

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_binary(void)
{
    buffer_t *a;
    char pattern_a[8] = {0x01, 0x10, 0x80, 0xFF,  0x00, 0x55, 0xEE, 0xAA};
    char pattern_b[9] = {0x02, 0x03, 0xF0, 0x80,  0x0F, 0x04, 0x1A, 0x7F, 0x33};
    int ret;
    size_t length;
    const void *data;

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);

    igloo_tap_test("pushed data pattern a", buffer_push_data(a, pattern_a, sizeof(pattern_a)) == 0);
    length = sizeof(pattern_a) + 42;
    data = &data;
    ret = buffer_get_data(a, &data, &length);
    igloo_tap_test("got data", ret == 0);
    if (ret == 0) {
        igloo_tap_test("correct length was returned", length == sizeof(pattern_a));
        igloo_tap_test("data is non-NULL", data != NULL);
        igloo_tap_test("data has been set", data != &data);
        if (length == sizeof(pattern_a) && data != NULL && data != &data) {
            igloo_tap_test("data matches pattern", memcmp(data, pattern_a, sizeof(pattern_a)) == 0);
        }
    }

    igloo_tap_test("pushed data pattern b", buffer_push_data(a, pattern_b, sizeof(pattern_b)) == 0);
    length = sizeof(pattern_a) + sizeof(pattern_b) + 42;
    data = &data;
    ret = buffer_get_data(a, &data, &length);
    igloo_tap_test("got data", ret == 0);
    if (ret == 0) {
        igloo_tap_test("correct length was returned", length == (sizeof(pattern_a) + sizeof(pattern_b)));
        igloo_tap_test("data is non-NULL", data != NULL);
        igloo_tap_test("data has been set", data != &data);
        if (length == (sizeof(pattern_a) + sizeof(pattern_b)) && data != NULL && data != &data) {
            igloo_tap_test("data matches combined pattern", memcmp(data, pattern_a, sizeof(pattern_a)) == 0 && memcmp(data + sizeof(pattern_a), pattern_b, sizeof(pattern_b)) == 0);
        }
    }


    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test__compare_to_string(buffer_t *a, const char *testname, const char *pattern)
{
    const char *string = NULL;
    int ret;

    ret = buffer_get_string(a, &string);
    igloo_tap_test("got strong", ret == 0);
    if (ret == 0) {
        igloo_tap_test("string is non-NULL", string != NULL);
        if (string != NULL) {
            igloo_tap_test(testname, strcmp(string, pattern) == 0);
            ctest_diagnostic_printf("string=\"%s\", pattern=\"%s\"", string, pattern);
        }
    }
}

static void test_shift(void)
{
    buffer_t *a;
    const char *pattern = "AABBBCC";

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);

    igloo_tap_test("pushed string", buffer_push_string(a, pattern) == 0);
    test__compare_to_string(a, "string matches input", pattern);
    igloo_tap_test("shifted data by 0 bytes", buffer_shift(a, 0) == 0);
    test__compare_to_string(a, "string matches input (no shift happened)", pattern);
    igloo_tap_test("shifted data by 2 bytes", buffer_shift(a, 2) == 0);
    test__compare_to_string(a, "string matches shifted input", pattern + 2);
    igloo_tap_test("shifted data by 3 bytes", buffer_shift(a, 3) == 0);
    test__compare_to_string(a, "string matches shifted input", pattern + 2 + 3);
    igloo_tap_test("shifted data by 3 bytes", buffer_shift(a, 2) == 0);
    test__compare_to_string(a, "string matches shifted input", pattern + 2 + 3 + 2);
    igloo_tap_test("shifted data beyond end (42 bytes)", buffer_shift(a, 42) != 0);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_length(void)
{
    buffer_t *a;
    const char *pattern = "AABBBCC";
    const char *match_a = "AABBB";
    const char *match_b = "AABB";
    const char *match_c = "";

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);

    igloo_tap_test("pushed string", buffer_push_string(a, pattern) == 0);
    test__compare_to_string(a, "string matches input", pattern);
    igloo_tap_test("Set length to match pattern a", buffer_set_length(a, strlen(match_a)) == 0);
    test__compare_to_string(a, "string matches pattern a", match_a);
    igloo_tap_test("Set length to match pattern b", buffer_set_length(a, strlen(match_b)) == 0);
    test__compare_to_string(a, "string matches pattern a", match_b);
    igloo_tap_test("Set length to match pattern c", buffer_set_length(a, strlen(match_c)) == 0);
    test__compare_to_string(a, "string matches pattern a", match_c);
    igloo_tap_test("Set length to match pattern a (again)", buffer_set_length(a, strlen(match_a)) != 0);
    test__compare_to_string(a, "string still matches pattern c", match_c);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_printf(void)
{
    buffer_t *a;
    const char *str = "Hello World!";
    const int num = -127;
    const char *match_a = ":Hello World!:";
    const char *match_b = ":Hello World!:<-127 >";
    const char *match_c = ":Hello World!:<-127 >? +127?";

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer created", a != NULL);

    igloo_tap_test("Set length to match pattern a", buffer_push_printf(a, ":%s:", str) == 0);
    test__compare_to_string(a, "string matches pattern a", match_a);
    igloo_tap_test("Set length to match pattern a", buffer_push_printf(a, "<%-5i>", num) == 0);
    test__compare_to_string(a, "string matches pattern b", match_b);
    igloo_tap_test("Set length to match pattern a", buffer_push_printf(a, "?%+5i?", -num) == 0);
    test__compare_to_string(a, "string matches pattern c", match_c);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_push_buffer(void)
{
    buffer_t *a;
    buffer_t *b;
    const char *pattern = "AABBBCC";
    const char *match_a = "AABBBCCAABBBCC";

    a = refobject_new(buffer_t);
    igloo_tap_test("buffer a created", a != NULL);
    b = refobject_new(buffer_t);
    igloo_tap_test("buffer b created", b != NULL);

    igloo_tap_test("pushed string", buffer_push_string(a, pattern) == 0);
    test__compare_to_string(a, "string matches input", pattern);

    igloo_tap_test("pushed buffer a to b", buffer_push_buffer(b, a) == 0);
    test__compare_to_string(b, "string matches input", pattern);

    igloo_tap_test("pushed buffer a to b", buffer_push_buffer(b, a) == 0);
    test__compare_to_string(b, "string matches pattern a", match_a);

    igloo_tap_test("un-referenced b", refobject_unref(b) == 0);
    igloo_tap_test("un-referenced a", refobject_unref(a) == 0);
}

int main (void)
{
    igloo_tap_init();
    igloo_tap_exit_on(igloo_TAP_EXIT_ON_FIN, NULL);

    igloo_tap_group_run("create-ref-unref", test_create_ref_unref);

    igloo_tap_group_run("name", test_name);
    igloo_tap_group_run("userdata", test_userdata);
    igloo_tap_group_run("associated", test_associated);

    igloo_tap_group_run("empty", test_empty);
    igloo_tap_group_run("string", test_string);
    igloo_tap_group_run("binary", test_binary);

    igloo_tap_group_run("shift", test_shift);
    igloo_tap_group_run("length", test_length);

    igloo_tap_group_run("printf", test_printf);
    igloo_tap_group_run("push_buffer", test_push_buffer);

    igloo_tap_fin();

    return EXIT_FAILURE; // return failure as we should never reach this point!
}
