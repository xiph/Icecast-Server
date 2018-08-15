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

#include "ctest_lib.h"

#include "../src/buffer.h"
#include "../src/refobject.h"

static void test_create_ref_unref(void)
{
    buffer_t *a;

    a = buffer_new(-1, NULL, NULL, REFOBJECT_NULL);
    ctest_test("buffer created", a != NULL);
    ctest_test("un-referenced", refobject_unref(a) == 0);

    a = buffer_new_simple();
    ctest_test("buffer created", a != NULL);
    ctest_test("un-referenced", refobject_unref(a) == 0);

}

static void test_name(void)
{
    buffer_t *a;
    const char *name = "test object name";
    const char *ret;

    a = buffer_new(-1, NULL, name, REFOBJECT_NULL);
    ctest_test("buffer created", a != NULL);

    ret = refobject_get_name(a);
    ctest_test("get name", ret != NULL);
    ctest_test("name match", strcmp(name, ret) == 0);

    ctest_test("un-referenced", refobject_unref(a) == 0);
}

static void test_userdata(void)
{
    buffer_t *a;
    int tmp = 0;
    void *userdata = &tmp;
    void *ret;

    a = buffer_new(-1, NULL, NULL, REFOBJECT_NULL);
    ctest_test("buffer created", a != NULL);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == NULL);
    ctest_test("set userdata", refobject_set_userdata(a, userdata) == 0);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == userdata);
    ctest_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == NULL);

    ctest_test("un-referenced", refobject_unref(a) == 0);

    a = buffer_new(-1, userdata, NULL, REFOBJECT_NULL);
    ctest_test("buffer created", a != NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == userdata);
    ctest_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == NULL);
    ctest_test("un-referenced", refobject_unref(a) == 0);
}

static void test_associated(void)
{
    refobject_t a;
    buffer_t *b;

    a = refobject_new(sizeof(refobject_base_t), NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));


    b = buffer_new(-1, NULL, NULL, a);
    ctest_test("buffer created with associated", !REFOBJECT_IS_NULL(b));

    ctest_test("un-referenced (1 of 2)", refobject_unref(b) == 0);
    ctest_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static void test_empty(void)
{
    buffer_t *a;
    const void *data = &data;
    size_t length = 5;
    const char *string;
    int ret;

    a = buffer_new_simple();
    ctest_test("buffer created", a != NULL);

    ret = buffer_get_data(a, &data, &length);
    ctest_test("got data and length from buffer", ret == 0);
    if (ret == 0) {
        ctest_test("data is updated", data != &data);
        ctest_test("length is zero", length == 0);
    }

    data = &data;
    ret = buffer_get_data(a, &data, NULL);
    ctest_test("got data from buffer", ret == 0);
    if (ret == 0) {
        ctest_test("data is updated", data != &data);
    }

    length = 5;
    ret = buffer_get_data(a, NULL, &length);
    ctest_test("got length from buffer", ret == 0);
    if (ret == 0) {
        ctest_test("length is zero", length == 0);
    }

    ret = buffer_get_string(a, &string);
    ctest_test("got string from buffer", ret == 0);
    if (ret == 0) {
        ctest_test("string is non-NULL", string != NULL);
        if (string != NULL) {
            ctest_test("string is empty", *string == 0);
        }
    }

    ctest_test("un-referenced", refobject_unref(a) == 0);
}

int main (void)
{
    ctest_init();


    test_create_ref_unref();

    test_name();
    test_userdata();
    test_associated();

    test_empty();

    ctest_fin();

    return 0;
}
