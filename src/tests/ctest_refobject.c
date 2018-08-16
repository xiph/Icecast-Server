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

#include "../refobject.h"

static void test_ptr(void)
{
    refobject_t a;

    a = REFOBJECT_NULL;
    ctest_test("NULL is NULL", REFOBJECT_IS_NULL(a));

    if (!REFOBJECT_IS_NULL(a))
        ctest_bailed_out();
}

static void test_create_ref_unref(void)
{
    refobject_t a;

    a = refobject_new(sizeof(refobject_base_t), NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));

    ctest_test("referenced", refobject_ref(a) == 0);
    ctest_test("un-referenced (1 of 2)", refobject_unref(a) == 0);
    ctest_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static void test_sizes(void)
{
    refobject_t a;

    a = refobject_new(sizeof(refobject_base_t) + 1024, NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created with size=sizeof(refobject_base_t) + 1024", !REFOBJECT_IS_NULL(a));
    ctest_test("un-referenced", refobject_unref(a) == 0);

    a = refobject_new(sizeof(refobject_base_t) + 131072, NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created with size=sizeof(refobject_base_t) + 131072", !REFOBJECT_IS_NULL(a));
    ctest_test("un-referenced", refobject_unref(a) == 0);

    if (sizeof(refobject_base_t) >= 1) {
        a = refobject_new(sizeof(refobject_base_t) - 1, NULL, NULL, NULL, REFOBJECT_NULL);
        ctest_test("refobject created with size=sizeof(refobject_base_t) - 1", REFOBJECT_IS_NULL(a));
        if (!REFOBJECT_IS_NULL(a)) {
            ctest_test("un-referenced", refobject_unref(a) == 0);
        }
    }

    a = refobject_new(0, NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created with size=0", REFOBJECT_IS_NULL(a));
    if (!REFOBJECT_IS_NULL(a)) {
        ctest_test("un-referenced", refobject_unref(a) == 0);
    }
}

static void test_name(void)
{
    refobject_t a;
    const char *name = "test object name";
    const char *ret;

    a = refobject_new(sizeof(refobject_base_t), NULL, NULL, name, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));

    ret = refobject_get_name(a);
    ctest_test("get name", ret != NULL);
    ctest_test("name match", strcmp(name, ret) == 0);

    ctest_test("un-referenced", refobject_unref(a) == 0);
}

static void test_userdata(void)
{
    refobject_t a;
    int tmp = 0;
    void *userdata = &tmp;
    void *ret;

    a = refobject_new(sizeof(refobject_base_t), NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));

    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == NULL);
    ctest_test("set userdata", refobject_set_userdata(a, userdata) == 0);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == userdata);
    ctest_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    ctest_test("get userdata", ret == NULL);

    ctest_test("un-referenced", refobject_unref(a) == 0);

    a = refobject_new(sizeof(refobject_base_t), NULL, userdata, NULL, REFOBJECT_NULL);
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
    refobject_t a, b;

    a = refobject_new(sizeof(refobject_base_t), NULL, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));

    b = refobject_new(sizeof(refobject_base_t), NULL, NULL, NULL, a);
    ctest_test("refobject created with associated", !REFOBJECT_IS_NULL(b));

    ctest_test("un-referenced (1 of 2)", refobject_unref(b) == 0);
    ctest_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static size_t test_freecb__called;
static void test_freecb__freecb(refobject_t self, void **userdata)
{
    test_freecb__called++;
}

static void test_freecb(void)
{
    refobject_t a;

    test_freecb__called = 0;
    a = refobject_new(sizeof(refobject_base_t), test_freecb__freecb, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));
    ctest_test("un-referenced", refobject_unref(a) == 0);
    ctest_test("freecb called", test_freecb__called == 1);

    test_freecb__called = 0;
    a = refobject_new(sizeof(refobject_base_t), test_freecb__freecb, NULL, NULL, REFOBJECT_NULL);
    ctest_test("refobject created", !REFOBJECT_IS_NULL(a));
    ctest_test("referenced", refobject_ref(a) == 0);
    ctest_test("freecb uncalled", test_freecb__called == 0);
    ctest_test("un-referenced (1 of 2)", refobject_unref(a) == 0);
    ctest_test("freecb uncalled", test_freecb__called == 0);
    ctest_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
    ctest_test("freecb called", test_freecb__called == 1);
}

int main (void)
{
    ctest_init();

    test_ptr();

    if (ctest_bailed_out()) {
        ctest_fin();
        return 1;
    }

    test_create_ref_unref();

    test_sizes();

    test_name();
    test_userdata();
    test_associated();
    test_freecb();

    ctest_fin();

    return 0;
}
