/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h> /* for EXIT_FAILURE */
#include <string.h> /* for strcmp() */

#include "../icecasttypes.h"

#include <igloo/tap.h>
#include <igloo/igloo.h>
#include <igloo/ro.h>

#include "../string_renderer.h"

static igloo_ro_t g_instance;

static void basic_test(void)
{
    string_renderer_t * renderer;
    const char *res;

    igloo_tap_test_success("igloo_ro_new renderer", igloo_ro_new(&renderer, string_renderer_t, g_instance));

    igloo_tap_test_success("string_renderer_add_string renderer \"te\"", string_renderer_add_string(renderer, "te"));
    igloo_tap_test_success("string_renderer_add_string renderer \"st\"", string_renderer_add_string(renderer, "st"));

    res = string_renderer_to_string_zero_copy(renderer);
    igloo_tap_test("string_renderer_to_string_zero_copy renderer returns non-NULL", res != NULL);
    if (res) {
        igloo_tap_test("string_renderer_to_string_zero_copy renderer returns \"test\"", strcmp(res, "test") == 0);
    }

    igloo_tap_test_success("string_renderer_add_int renderer 133742", string_renderer_add_int(renderer, 133742));
    igloo_tap_test_success("string_renderer_start_list_formdata renderer", string_renderer_start_list_formdata(renderer));
    igloo_tap_test_success("string_renderer_add_kv renderer \"key\" \"val ue\"", string_renderer_add_kv(renderer, "key", "val ue"));
    igloo_tap_test_success("string_renderer_add_ki renderer \"num\", -31415", string_renderer_add_ki(renderer, "num", -31415));
    igloo_tap_test_success("string_renderer_end_list renderer", string_renderer_end_list(renderer));

    res = string_renderer_to_string_zero_copy(renderer);
    igloo_tap_test("string_renderer_to_string_zero_copy renderer returns non-NULL", res != NULL);
    if (res) {
        igloo_tap_test("string_renderer_to_string_zero_copy renderer returns \"test133742key=val%20ue&num=-31415\"", strcmp(res, "test133742key=val%20ue&num=-31415") == 0);
    }

    igloo_tap_test_success("string_renderer_add_string_with_options renderer \"te!s t\" false STRING_RENDERER_ENCODING_H", string_renderer_add_string_with_options(renderer, "te!s t", false, STRING_RENDERER_ENCODING_H));
    igloo_tap_test_success("string_renderer_add_string_with_options renderer \"te!s t\" false STRING_RENDERER_ENCODING_H_ALT", string_renderer_add_string_with_options(renderer, "te!s t", false, STRING_RENDERER_ENCODING_H_ALT));
    igloo_tap_test_success("string_renderer_add_string_with_options renderer \"te!s t\" false STRING_RENDERER_ENCODING_H_SPACE", string_renderer_add_string_with_options(renderer, "te!s t", false, STRING_RENDERER_ENCODING_H_SPACE));
    igloo_tap_test_success("string_renderer_add_string_with_options renderer \"te!s t\" false STRING_RENDERER_ENCODING_H_ALT_SPACE", string_renderer_add_string_with_options(renderer, "te!s t", false, STRING_RENDERER_ENCODING_H_ALT_SPACE));
    igloo_tap_test_success("string_renderer_add_string_with_options renderer NULL true STRING_RENDERER_ENCODING_H", string_renderer_add_string_with_options(renderer, NULL, true, STRING_RENDERER_ENCODING_H));

    res = string_renderer_to_string_zero_copy(renderer);
    igloo_tap_test("string_renderer_to_string_zero_copy renderer returns non-NULL", res != NULL);
    if (res) {
        igloo_tap_test("string_renderer_to_string_zero_copy renderer returns \"test133742key=val%20ue&num=-31415te\\x21s\\x20t\"te\\x21s\\x20t\"te\\x21s t\"te\\x21s t\"-\"", strcmp(res, "test133742key=val%20ue&num=-31415te\\x21s\\x20t\"te\\x21s\\x20t\"te\\x21s t\"te\\x21s t\"-") == 0);
    }

    igloo_tap_test_success("unref renderer", igloo_ro_unref(&renderer));
}

int main (void)
{
    igloo_tap_init();
    igloo_tap_exit_on(igloo_TAP_EXIT_ON_FIN, NULL);
    igloo_tap_test_success("igloo_initialize", igloo_initialize(&g_instance));
    if (igloo_ro_is_null(g_instance)) {
        igloo_tap_bail_out("Can not get an instance");
        return EXIT_FAILURE; // return failure as we should never reach this point!
    }

    basic_test();

    igloo_tap_test_success("unref instance", igloo_ro_unref(&g_instance));
    igloo_tap_fin();

    return EXIT_FAILURE; // return failure as we should never reach this point!
}
