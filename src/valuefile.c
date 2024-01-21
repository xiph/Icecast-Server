/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2024     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <igloo/ro.h>
#include <igloo/error.h>

#include "valuefile.h"
#include "string_renderer.h"
#include "global.h"
#include "errors.h"
#include "logging.h"
#define CATMODULE "valuefile"

// universal:
#define WK_ASI              "ddd60c5c-2934-404f-8f2d-fcb4da88b633"
#define WK_TAGNAME          "bfae7574-3dae-425d-89b1-9c087c140c23"
#define WK_ENGLISH          "c50134ca-0a32-5c5c-833c-2686043c0b3f"
#define WK_GB_TEXT          "0678cfe2-1386-43db-86a9-6c491b2df64c"
#define WK_RELATES_TO       "079ab791-784e-4bb9-ae2d-07e01710a60c"
#define WK_SEE_ALSO         "a75f9010-9db3-4d78-bd78-0dd528d6b55d"
#define WK_DEFAULT_TYPE     "87c4892f-ae39-476e-8ed0-d9ed321dafe9"
#define WK_UNSIGNED_INT     "dea3782c-6bcb-4ce9-8a39-f8dab399d75d"

// Icecast:
#define WK_EQ_HTTP          "06cfbaf9-01fe-4d82-8075-0389231c46b3"
#define WK_STATE_info       "c2bb1328-87d6-49ad-b7df-0b381a2e606d"
#define WK_STATE_error      "e0faf79c-bddf-42d7-a237-975aca861a0b"
#define WK_STATE_warning    "107c0e91-1fd8-4bbc-a54c-899cbb3d9bc6"

static const char *states[] = {WK_STATE_info, WK_STATE_error, WK_STATE_warning};

static const struct {
    const char *uuid;
    const char *symbol;
    const char *tagname;
    const char *text;
    const char *state;
    const unsigned int http_status;
} extra[] = {
    {.uuid = "be7fac90-54fb-4673-9e0d-d15d6a4963a2", .symbol = "AUTH_ALTER_REDIRECT_SEE_OTHER", .http_status = 303},
    {.uuid = "4b08a03a-ecce-4981-badf-26b0bb6c9d9c", .symbol = "AUTH_ALTER_REDIRECT_TEMPORARY", .http_status = 307},
    {.uuid = "36bf6815-95cb-4cc8-a7b0-6b4b0c82ac5d", .symbol = "AUTH_ALTER_REDIRECT_PERMANENT", .http_status = 308},
    {.uuid = WK_EQ_HTTP, .tagname = "equivalent-http-status"},
    {.uuid = WK_STATE_info, .tagname = "info"},
    {.uuid = WK_STATE_error, .tagname = "error"},
    {.uuid = WK_STATE_warning, .tagname = "warning"},
    // grep __reportxml_add_maintenance.\*\" admin.c  | sed 's/^.*__reportxml_add_maintenance([^,]*,[^,]*, *"\([^"]*\)", *"\([^"]*\)", *"\([^"]*\)", NULL);.*$/{.uuid = "\1", .state = WK_STATE_\2, .text = "\3"},/'
    {.uuid = "a93a842a-9664-43a9-b707-7f358066fe2b", .state = WK_STATE_error, .text = "Global client, and source limit is bigger than suitable for current open file limit."},
    {.uuid = "fdbacc56-1150-4c79-b53a-43cc79046f40", .state = WK_STATE_info, .text = "Core files are disabled."},
    {.uuid = "688bdebc-c190-4b5d-8764-3a050c48387a", .state = WK_STATE_info, .text = "Core files are enabled."},
    {.uuid = "c704804e-d3b9-4544-898b-d477078135de", .state = WK_STATE_warning, .text = "Developer logging is active. This mode is not for production."},
    {.uuid = "f90219e1-bd07-4b54-b1ee-0ba6a0289a15", .state = WK_STATE_error, .text = "IPv6 not enabled."},
    {.uuid = "709ab43b-251d-49a5-a4fe-c749eaabf17c", .state = WK_STATE_info, .text = "IPv4-mapped IPv6 is available on this system."},
    {.uuid = "c4f25c51-2720-4b38-a806-19ef024b5289", .state = WK_STATE_warning, .text = "Hostname is not set to anything useful in <hostname>."},
    {.uuid = "8defae31-a52e-4bba-b904-76db5362860f", .state = WK_STATE_warning, .text = "No useful location is given in <location>."},
    {.uuid = "cf86d88e-dc20-4359-b446-110e7065d17a", .state = WK_STATE_warning, .text = "No admin contact given in <admin>. YP directory support is disabled."},
    {.uuid = "e2ba5a8b-4e4f-41ca-b455-68ae5fb6cae0", .state = WK_STATE_error, .text = "No PRNG seed configured. PRNG is insecure."},
    {.uuid = "6620ef7b-46ef-4781-9a5e-8ee7f0f9d44e", .state = WK_STATE_error, .text = "Unknown tags are used in the config file. See the error.log for details."},
    {.uuid = "b6224fc4-53a1-433f-a6cd-d5b85c60f1c9", .state = WK_STATE_error, .text = "Obsolete tags are used in the config file. See the error.log for details and update your configuration accordingly."},
    {.uuid = "0f6f757d-52d8-4b9a-8e57-9bcd528fffba", .state = WK_STATE_error, .text = "Invalid tags are used in the config file. See the error.log for details and update your configuration accordingly."},
    {.uuid = "8fc33086-274d-4ccb-b32f-599b3fa0f41a", .state = WK_STATE_error, .text = "The configuration did not validate. See the error.log for details and update your configuration accordingly."},
    {.uuid = "6830cbf7-cd68-4c0c-ab5a-81499c70fd34", .state = WK_STATE_info, .text = "chroot configured and active."},
    {.uuid = "2d584a76-e67c-4268-b7e8-139b0b9b1131", .state = WK_STATE_error, .text = "chroot configured but failed."},
    {.uuid = "1a3fea5c-3352-4cb5-85cc-51ab9bd6ea83", .state = WK_STATE_error, .text = "chroot configured but not supported by operating system."},
    {.uuid = "bab05e81-fd03-4773-9fc5-c4609883a5e3", .state = WK_STATE_info, .text = "Change of UID/GID configured and active."},
    {.uuid = "4f856dd4-7aac-44b4-95b5-b6798f547603", .state = WK_STATE_error, .text = "Change of UID/GID configured but failed."},
    {.uuid = "afcaa756-b91c-4496-a9e2-44400a18789c", .state = WK_STATE_error, .text = "Change of UID/GID configured but not supported by operating system."},
    {.uuid = "f68dd8a3-22b1-4118-aba6-b039f2c5b51e", .state = WK_STATE_info, .text = "Currently no sources are connected to this server."},
    {.uuid = "a3a51986-3bba-42b9-ad5c-d9ecc9967320", .state = WK_STATE_warning, .text = "Legacy sources are connected. See mount list for details."},
    {.uuid = "08676614-50b4-4ea7-ba99-7c2ffcecf705", .state = WK_STATE_warning, .text = "More than 90% of the server's configured maximum clients are connected"},
    {.uuid = "417ae59c-de19-4ed1-ade1-429c689f1152", .state = WK_STATE_info, .text = "More than 75% of the server's configured maximum clients are connected"},
    {.uuid = "dc91ce96-f473-41d1-bfff-379666306911", .state = WK_STATE_info, .text = "Environment is noisy."},
    {.uuid = "40d134e3-fbbe-46b1-a409-9b2ca8954528", .state = WK_STATE_warning, .text = "No secure password hash support detected."},
    {NULL}
};

static igloo_error_t valuefile_value(string_renderer_t *renderer, const char *value, bool first)
{
    if (!first)
        string_renderer_add_string(renderer, " ");

    if (!value) {
        return string_renderer_add_string(renderer, "!null");
    } else if (!*value) {
        return string_renderer_add_string(renderer, "!empty");
    }

    return string_renderer_add_string_with_options(renderer, value, false, STRING_RENDERER_ENCODING_URI);
}

static igloo_error_t valuefile_line_metadata(string_renderer_t *renderer, const char *tag, const char *relation, const char *context, const char *type, const char *encoding, const char *value)
{
    string_renderer_add_string(renderer, "tag-metadata        ");
    string_renderer_add_string(renderer, tag);
    string_renderer_add_string(renderer, " ");
    string_renderer_add_string(renderer, relation);
    valuefile_value(renderer, context, false);
    valuefile_value(renderer, type, false);
    valuefile_value(renderer, encoding, false);
    valuefile_value(renderer, value, false);
    string_renderer_add_string(renderer, "\n");

    return igloo_ERROR_NONE;
}

static igloo_error_t valuefile_line_relation(string_renderer_t *renderer, const char *tag, const char *relation, const char *related, const char *context, const char *filter)
{
    string_renderer_add_string(renderer, "tag-relation        ");
    string_renderer_add_string(renderer, tag);
    string_renderer_add_string(renderer, " ");
    string_renderer_add_string(renderer, relation);
    string_renderer_add_string(renderer, " ");
    string_renderer_add_string(renderer, related);
    valuefile_value(renderer, context, false);
    valuefile_value(renderer, filter, false);
    string_renderer_add_string(renderer, "\n");

    return igloo_ERROR_NONE;
}

static igloo_error_t valuefile_line_tag_ise(string_renderer_t *renderer, const char *ise, const char *comment)
{
    string_renderer_add_string(renderer, "tag-ise             ");
    string_renderer_add_string(renderer, ise);
    if (comment) {
        string_renderer_add_string(renderer, " # ");
        string_renderer_add_string(renderer, comment);
    }
    string_renderer_add_string(renderer, "\n");

    return igloo_ERROR_NONE;
}

static igloo_error_t valuefile_http_status(string_renderer_t *renderer, const char *tag, unsigned int http_status)
{
    if (http_status < 100 || http_status > 599)
        return igloo_ERROR_NONE;

    string_renderer_add_string(renderer, "tag-metadata        ");
    string_renderer_add_string(renderer, tag);
    string_renderer_add_string(renderer, " ");
    string_renderer_add_string(renderer, WK_EQ_HTTP);
    string_renderer_add_string(renderer, " !null !null !null ");
    string_renderer_add_int(renderer, http_status);
    string_renderer_add_string(renderer, "\n");

    return igloo_ERROR_NONE;
}

string_renderer_t * valuefile_export_database(void)
{
    string_renderer_t *renderer;

    if (igloo_ro_new(&renderer, string_renderer_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    string_renderer_add_string(renderer, "!!ValueFile 54bf8af4-b1d7-44da-af48-5278d11e8f32 e5da6a39-46d5-48a9-b174-5c26008e208e\n");
    string_renderer_add_string(renderer, "!!Feature   f06c2226-b33e-48f2-9085-cd906a3dcee0 # tagpool-source-format-modern-limited\n");
    string_renderer_add_string(renderer, "\n");

    // errors:
    string_renderer_add_string(renderer, "# Errors:\n");
    for (size_t i = 0; ; i++) {
        const icecast_error_t * error = error_get_by_index(i);
        if (!error)
            break;
        valuefile_line_tag_ise(renderer,  error->uuid, error->message);
        valuefile_line_metadata(renderer, error->uuid, WK_ASI, NULL, WK_TAGNAME, NULL, error->symbol);
        valuefile_line_metadata(renderer, error->uuid, WK_GB_TEXT, WK_ENGLISH, NULL, NULL, error->message);
        valuefile_http_status(renderer,   error->uuid, error->http_status);
    }
    string_renderer_add_string(renderer, "\n");

    string_renderer_add_string(renderer, "# Extra:\n");
    for (size_t i = 0; extra[i].uuid; i++) {
        valuefile_line_tag_ise(renderer, extra[i].uuid, extra[i].symbol);
        if (extra[i].symbol)
            valuefile_line_metadata(renderer, extra[i].uuid, WK_ASI, NULL, WK_TAGNAME, NULL, extra[i].symbol);
        if (extra[i].tagname)
            valuefile_line_metadata(renderer, extra[i].uuid, WK_ASI, NULL, WK_TAGNAME, NULL, extra[i].tagname);
        if (extra[i].text)
            valuefile_line_metadata(renderer, extra[i].uuid, WK_GB_TEXT, WK_ENGLISH, NULL, NULL, extra[i].text);
        if (extra[i].state)
            valuefile_line_relation(renderer, extra[i].uuid, WK_RELATES_TO, extra[i].state, NULL, NULL);

        valuefile_http_status(renderer, extra[i].uuid, extra[i].http_status);
    }
    string_renderer_add_string(renderer, "\n");

    string_renderer_add_string(renderer, "# Special:\n");
    valuefile_line_relation(renderer, WK_EQ_HTTP, WK_DEFAULT_TYPE, WK_UNSIGNED_INT, NULL, NULL);
    for (size_t i = 0; i < (sizeof(states)/sizeof(*states)); i++) {
        for (size_t j = 0; j < (sizeof(states)/sizeof(*states)); j++) {
            if (i != j)
                valuefile_line_relation(renderer, states[i], WK_SEE_ALSO, states[j], NULL, NULL);
        }
    }
    string_renderer_add_string(renderer, "\n");

    return renderer;
}
