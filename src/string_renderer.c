/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include "icecasttypes.h"
#include <igloo/ro.h>
#include <igloo/error.h>

#include "string_renderer.h"
#include "util.h"
#include "logging.h"
#define CATMODULE "string-renderer"

struct string_renderer_tag {
    igloo_ro_full_t __parent;

    /* config */
    const char *record_separator;
    const char *kv_separator;
    bool allow_null_key;
    bool allow_null_value;
    string_renderer_encoding_t encoding;

    /* state */
    bool list_start;
    char *buffer;
    size_t fill;
    size_t len;
};

static void __string_renderer_free(igloo_ro_t self)
{
    string_renderer_t *renderer = igloo_ro_to_type(self, string_renderer_t);

    free(renderer->buffer);
}

static igloo_error_t __string_renderer_new(igloo_ro_t self, const igloo_ro_type_t *type, va_list ap)
{
    string_renderer_t *renderer = igloo_ro_to_type(self, string_renderer_t);

    renderer->list_start        = false;
    renderer->record_separator  = ", ";
    renderer->kv_separator      = "=";
    renderer->allow_null_key    = true;
    renderer->allow_null_value  = true;
    renderer->encoding          = STRING_RENDERER_ENCODING_PLAIN;

    return igloo_ERROR_NONE;
}

igloo_RO_PUBLIC_TYPE(string_renderer_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(__string_renderer_free),
        igloo_RO_TYPEDECL_NEW(__string_renderer_new)
        );

static inline igloo_error_t string_renderer_pre_allocate(string_renderer_t *self, size_t len)
{
    if (self->len < (self->fill + len + 1)) {
        size_t new_len = self->len + len + 1 + 64; /* allocate more than we need to avoid re-allocating every time */
        char *n = realloc(self->buffer, new_len);
        if (!n)
            return igloo_ERROR_NOMEM;
        self->buffer = n;
        self->len = new_len;
    }

    return igloo_ERROR_NONE;
}

static igloo_error_t string_renderer_append_raw(string_renderer_t *self, const char *string, ssize_t len)
{
    igloo_error_t err;

    if (len < 0)
        len = strlen(string);

    err = string_renderer_pre_allocate(self, len);
    if (err != igloo_ERROR_NONE)
        return err;

    memcpy(self->buffer + self->fill, string, len);
    self->fill += len;

    return igloo_ERROR_NONE;
}

static inline igloo_error_t string_renderer_append_char(string_renderer_t *self, const char c)
{
    if (self->len < (self->fill + 1 + 1)) {
        return string_renderer_append_raw(self, &c, 1);
    }

    self->buffer[self->fill++] = c;
    return igloo_ERROR_NONE;
}

igloo_error_t       string_renderer_start_list(string_renderer_t *self, const char *record_separator, const char *kv_separator, bool allow_null_key, bool allow_null_value, string_renderer_encoding_t encoding)
{
    if (!self)
        return igloo_ERROR_FAULT;

    self->list_start        = true;
    self->record_separator  = record_separator;
    self->kv_separator      = kv_separator;
    self->allow_null_key    = allow_null_key;
    self->allow_null_value  = allow_null_value;

    if (encoding != STRING_RENDERER_ENCODING_DEFAULT) {
        self->encoding = encoding;
    }

    return igloo_ERROR_NONE;
}

igloo_error_t       string_renderer_end_list(string_renderer_t *self)
{
    /* no-op */
    return igloo_ERROR_NONE;
}

igloo_error_t       string_renderer_add_string_with_options(string_renderer_t *self, const char *string, bool allow_null, string_renderer_encoding_t encoding)
{
    if (!self)
        return igloo_ERROR_FAULT;

    if (!string && !allow_null) {
        return igloo_ERROR_INVAL;
    }

    if (encoding == STRING_RENDERER_ENCODING_DEFAULT) {
        encoding = self->encoding;
    }

    switch (encoding) {
        case STRING_RENDERER_ENCODING_PLAIN:
            if (!string)
                return igloo_ERROR_NONE;
            return string_renderer_append_raw(self, string, -1);
            break;
        case STRING_RENDERER_ENCODING_URI:
            if (!string) {
                return igloo_ERROR_NONE;
            } else {
                char *x = util_url_escape(string);
                igloo_error_t err;

                if (!x)
                    return igloo_ERROR_NOMEM;

                err = string_renderer_append_raw(self, x, -1);
                free(x);
                return err;
            }
            break;
        case STRING_RENDERER_ENCODING_H:
        case STRING_RENDERER_ENCODING_H_ALT:
        case STRING_RENDERER_ENCODING_H_SPACE:
        case STRING_RENDERER_ENCODING_H_ALT_SPACE:
            if (!string) {
                return string_renderer_append_char(self, '-');
            } else {
                bool alt    = encoding == STRING_RENDERER_ENCODING_H_ALT    || encoding == STRING_RENDERER_ENCODING_H_ALT_SPACE;
                bool space  = encoding == STRING_RENDERER_ENCODING_H_SPACE  || encoding == STRING_RENDERER_ENCODING_H_ALT_SPACE;

                /* ignore errors here as we just try to optimise access */
                string_renderer_pre_allocate(self, strlen(string));

                if (alt) {
                    igloo_error_t err = string_renderer_append_char(self, '"');
                    if (err != igloo_ERROR_NONE)
                        return err;
                }

                for (const char *sp = string; *sp; sp++) {
                    const char c = *sp;

                    /* copied from common/log/log.c __vsnprintf__is_print() */
                    if ((c <= '"' || c == '`' || c == '\\') && !(space && c == ' ')) {
                        static const char hextable[] = "0123456789abcdef";
                        char buf[4] = "\\xXX";
                        buf[2] = hextable[(c >> 4) & 0x0F];
                        buf[3] = hextable[(c >> 0) & 0x0F];
                        igloo_error_t err = string_renderer_append_raw(self, buf, 4);
                        if (err != igloo_ERROR_NONE)
                            return err;
                    } else {
                        igloo_error_t err = string_renderer_append_char(self, c);
                        if (err != igloo_ERROR_NONE)
                            return err;
                    }
                }

                if (alt) {
                    igloo_error_t err = string_renderer_append_char(self, '"');
                    if (err != igloo_ERROR_NONE)
                        return err;
                }
            }
            return igloo_ERROR_NONE;
            break;
        default:
            return igloo_ERROR_INVAL;
    }

    /* this should never be reached */
    return igloo_ERROR_GENERIC;
}

igloo_error_t       string_renderer_add_int_with_options(string_renderer_t *self, long long int val, bool allow_zero, bool allow_negative, string_renderer_encoding_t encoding)
{
    if (!self)
        return igloo_ERROR_FAULT;

    if (val == 0 && !allow_zero) {
        return igloo_ERROR_INVAL;
    }

    if (val < 0 && !allow_negative) {
        return igloo_ERROR_INVAL;
    }

    if (encoding == STRING_RENDERER_ENCODING_DEFAULT) {
        encoding = self->encoding;
    }

    switch (encoding) {
        case STRING_RENDERER_ENCODING_PLAIN:
        case STRING_RENDERER_ENCODING_URI:
        case STRING_RENDERER_ENCODING_H:
        case STRING_RENDERER_ENCODING_H_ALT:
        case STRING_RENDERER_ENCODING_H_SPACE:
        case STRING_RENDERER_ENCODING_H_ALT_SPACE:
            {
                char buffer[64];
                int ret = snprintf(buffer, sizeof(buffer), "%lli", val);
                if (ret < 1 || ret > (int)sizeof(buffer))
                    return igloo_ERROR_GENERIC;
                return string_renderer_append_raw(self, buffer, ret);
            }
            break;
        default:
            return igloo_ERROR_INVAL;
    }

    /* this should never be reached */
    return igloo_ERROR_GENERIC;
}

static igloo_error_t string_renderer_add_kv_key_only(string_renderer_t *self, const char *key, string_renderer_encoding_t encoding)
{
    if (!self)
        return igloo_ERROR_FAULT;

    if (!key && !self->allow_null_key) {
        return igloo_ERROR_INVAL;
    }

    if (encoding == STRING_RENDERER_ENCODING_DEFAULT) {
        encoding = self->encoding;
    }

    if (self->list_start) {
        self->list_start = false;
    } else {
        igloo_error_t err = string_renderer_append_raw(self, self->record_separator, -1);
        if (err != igloo_ERROR_NONE)
            return err;
    }


    return string_renderer_add_string_with_options(self, key, true, encoding);
}

igloo_error_t       string_renderer_add_kv_with_options(string_renderer_t *self, const char *key, const char *value, string_renderer_encoding_t key_encoding, bool allow_null_value, bool allow_empty_value)
{
    igloo_error_t err;

    if (!self)
        return igloo_ERROR_FAULT;

    if (!value && !(allow_null_value && self->allow_null_value)) {
        return igloo_ERROR_INVAL;
    }

    if (value && !*value && !allow_empty_value) {
        return igloo_ERROR_INVAL;
    }

    err = string_renderer_add_kv_key_only(self, key, key_encoding);
    if (err != igloo_ERROR_NONE)
        return err;

    err = string_renderer_append_raw(self, self->kv_separator, -1);
    if (err != igloo_ERROR_NONE)
        return err;

    return string_renderer_add_string_with_options(self, value, allow_null_value, STRING_RENDERER_ENCODING_DEFAULT);
}

igloo_error_t       string_renderer_add_ki_with_options(string_renderer_t *self, const char *key, long long int value, string_renderer_encoding_t key_encoding, bool allow_zero, bool allow_negative)
{
    igloo_error_t err;

    if (!self)
        return igloo_ERROR_FAULT;

    if (value == 0 && !allow_zero) {
        return igloo_ERROR_INVAL;
    }

    if (value < 0 && !allow_negative) {
        return igloo_ERROR_INVAL;
    }

    err = string_renderer_add_kv_key_only(self, key, key_encoding);
    if (err != igloo_ERROR_NONE)
        return err;

    err = string_renderer_append_raw(self, self->kv_separator, -1);
    if (err != igloo_ERROR_NONE)
        return err;

    return string_renderer_add_int_with_options(self, value, allow_zero, allow_negative, STRING_RENDERER_ENCODING_DEFAULT);
}

const char *        string_renderer_to_string_zero_copy(string_renderer_t *self)
{
    if (!self)
        return NULL;

    /* add a \0 to the end */
    if (string_renderer_append_char(self, '\0') != igloo_ERROR_NONE)
        return NULL;

    /* but do not count it as fill */
    self->fill--;

    return self->buffer;
}
