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
#include <stdlib.h> /* for EXIT_FAILURE */

#include <igloo/tap.h>

#include "../refobject.h"

static void test_ptr(void)
{
    refobject_t a;

    a = REFOBJECT_NULL;
    igloo_tap_test("NULL is NULL", REFOBJECT_IS_NULL(a));

    if (!REFOBJECT_IS_NULL(a))
        igloo_tap_bail_out(NULL);
}

static void test_create_ref_unref(void)
{
    refobject_base_t *a;

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    igloo_tap_test("referenced", refobject_ref(a) == 0);
    igloo_tap_test("un-referenced (1 of 2)", refobject_unref(a) == 0);
    igloo_tap_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static void test_typename(void)
{
    refobject_base_t *a;
    const char *typename;

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    typename = REFOBJECT_GET_TYPENAME(a);
    igloo_tap_test("got typename", typename != NULL);
    igloo_tap_test("typename matches", strcmp(typename, "refobject_base_t") == 0);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_valid(void)
{
    refobject_base_t *a;

    typedef struct {
        refobject_base_t __base;
    } ctest_test_type_t;

    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_t);

    igloo_tap_test("NULL is not valid", !REFOBJECT_IS_VALID(REFOBJECT_NULL, refobject_base_t));

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    igloo_tap_test("is valid", REFOBJECT_IS_VALID(a, refobject_base_t));
    igloo_tap_test("is valid as diffrent type", !REFOBJECT_IS_VALID(a, ctest_test_type_t));

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_sizes(void)
{
    refobject_t a;

    typedef struct {
        refobject_base_t __base;
        char padding[1024];
    } ctest_test_type_a_t;
    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_a_t,
            REFOBJECT_DEFINE_TYPE_NEW_NOOP()
            );

    typedef struct {
        refobject_base_t __base;
        char padding[131072];
    } ctest_test_type_b_t;
    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_b_t,
            REFOBJECT_DEFINE_TYPE_NEW_NOOP()
            );

    typedef struct {
        char padding[sizeof(refobject_base_t) - 1];
    } ctest_test_type_c_t;
    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_c_t,
            REFOBJECT_DEFINE_TYPE_NEW_NOOP()
            );

    typedef struct {
        char padding[0];
    } ctest_test_type_d_t;
    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_d_t,
            REFOBJECT_DEFINE_TYPE_NEW_NOOP()
            );

    a = REFOBJECT_FROM_TYPE(refobject_new(ctest_test_type_a_t));
    igloo_tap_test("refobject created with size=sizeof(refobject_base_t) + 1024", !REFOBJECT_IS_NULL(a));
    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

    a = REFOBJECT_FROM_TYPE(refobject_new(ctest_test_type_b_t));
    igloo_tap_test("refobject created with size=sizeof(refobject_base_t) + 131072", !REFOBJECT_IS_NULL(a));
    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

    a = REFOBJECT_FROM_TYPE(refobject_new(ctest_test_type_c_t));
    igloo_tap_test("refobject created with size=sizeof(refobject_base_t) - 1", REFOBJECT_IS_NULL(a));
    if (!REFOBJECT_IS_NULL(a)) {
        igloo_tap_test("un-referenced", refobject_unref(a) == 0);
    }

    a = REFOBJECT_FROM_TYPE(refobject_new(ctest_test_type_d_t));
    igloo_tap_test("refobject created with size=0", REFOBJECT_IS_NULL(a));
    if (!REFOBJECT_IS_NULL(a)) {
        igloo_tap_test("un-referenced", refobject_unref(a) == 0);
    }
}

static void test_name(void)
{
    refobject_base_t *a;
    const char *name = "test object name";
    const char *ret;

    a = refobject_new_ext(refobject_base_t, NULL, name, REFOBJECT_NULL);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    ret = refobject_get_name(a);
    igloo_tap_test("get name", ret != NULL);
    igloo_tap_test("name match", strcmp(name, ret) == 0);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);
}

static void test_userdata(void)
{
    refobject_base_t *a;
    int tmp = 0;
    void *userdata = &tmp;
    void *ret;

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == NULL);
    igloo_tap_test("set userdata", refobject_set_userdata(a, userdata) == 0);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == userdata);
    igloo_tap_test("clearing userdata", refobject_set_userdata(a, NULL) == 0);
    ret = refobject_get_userdata(a);
    igloo_tap_test("get userdata", ret == NULL);

    igloo_tap_test("un-referenced", refobject_unref(a) == 0);

    a = refobject_new_ext(refobject_base_t, userdata, NULL, REFOBJECT_NULL);
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
    refobject_base_t *a, *b;

    a = refobject_new(refobject_base_t);
    igloo_tap_test("refobject created", !REFOBJECT_IS_NULL(a));

    b = refobject_new_ext(refobject_base_t, NULL, NULL, a);
    igloo_tap_test("refobject created with associated", !REFOBJECT_IS_NULL(b));

    igloo_tap_test("un-referenced (1 of 2)", refobject_unref(b) == 0);
    igloo_tap_test("un-referenced (2 of 2)", refobject_unref(a) == 0);
}

static size_t test_freecb__called;
static void test_freecb__freecb(refobject_t self, void **userdata)
{
    test_freecb__called++;
}

static void test_freecb(void)
{
    typedef struct {
        refobject_base_t __base;
    } ctest_test_type_t;
    ctest_test_type_t *a;

    REFOBJECT_DEFINE_PRIVATE_TYPE(ctest_test_type_t,
            REFOBJECT_DEFINE_TYPE_FREE(test_freecb__freecb),
            REFOBJECT_DEFINE_TYPE_NEW_NOOP()
            );

    test_freecb__called = 0;
    a = refobject_new(ctest_test_type_t);
    igloo_tap_test("refobject created", a != NULL);
    igloo_tap_test("un-referenced", refobject_unref(REFOBJECT_FROM_TYPE(a)) == 0);
    igloo_tap_test("freecb called", test_freecb__called == 1);

    test_freecb__called = 0;
    a = refobject_new(ctest_test_type_t);
    igloo_tap_test("refobject created", a != NULL);
    igloo_tap_test("referenced", refobject_ref(REFOBJECT_FROM_TYPE(a)) == 0);
    igloo_tap_test("freecb uncalled", test_freecb__called == 0);
    igloo_tap_test("un-referenced (1 of 2)", refobject_unref(REFOBJECT_FROM_TYPE(a)) == 0);
    igloo_tap_test("freecb uncalled", test_freecb__called == 0);
    igloo_tap_test("un-referenced (2 of 2)", refobject_unref(REFOBJECT_FROM_TYPE(a)) == 0);
    igloo_tap_test("freecb called", test_freecb__called == 1);
}

int main (void)
{
    igloo_tap_init();
    igloo_tap_exit_on(igloo_TAP_EXIT_ON_FIN|igloo_TAP_EXIT_ON_BAIL_OUT, NULL);

    igloo_tap_group_run("ptr", test_ptr);

    igloo_tap_group_run("ref-unref", test_create_ref_unref);

    igloo_tap_group_run("typename", test_typename);
    igloo_tap_group_run("valid", test_valid);

    igloo_tap_group_run("sizes", test_sizes);

    igloo_tap_group_run("name", test_name);
    igloo_tap_group_run("userdata", test_userdata);
    igloo_tap_group_run("associated", test_associated);
    igloo_tap_group_run("freecb", test_freecb);

    igloo_tap_fin();

    return EXIT_FAILURE; // return failure as we should never reach this point!
}
