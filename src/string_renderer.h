/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022     , Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __STRING_RENDERER_H__
#define __STRING_RENDERER_H__

#include <stdbool.h>

#include <igloo/typedef.h>
#include <igloo/error.h>

#include "icecasttypes.h"

igloo_RO_FORWARD_TYPE(string_renderer_t);

typedef enum {
    STRING_RENDERER_ENCODING_DEFAULT,
    STRING_RENDERER_ENCODING_PLAIN,
    STRING_RENDERER_ENCODING_URI,
    STRING_RENDERER_ENCODING_H,             /* same as "%H"   */
    STRING_RENDERER_ENCODING_H_ALT,         /* same as "%#H"  */
    STRING_RENDERER_ENCODING_H_SPACE,       /* same as "% H"  */
    STRING_RENDERER_ENCODING_H_ALT_SPACE    /* same as "%# H" */
} string_renderer_encoding_t;

/* all functions are NOT thread safe */

igloo_error_t       string_renderer_start_list(string_renderer_t *self, const char *record_separator, const char *kv_separator, bool allow_null_key, bool allow_null_value, string_renderer_encoding_t encoding);
#define             string_renderer_start_list_formdata(self)   string_renderer_start_list((self), "&", "=", false, true, STRING_RENDERER_ENCODING_URI)
igloo_error_t       string_renderer_end_list(string_renderer_t *self);

igloo_error_t       string_renderer_add_string_with_options(string_renderer_t *self, const char *string, bool allow_null, string_renderer_encoding_t encoding);
#define             string_renderer_add_string(self,string)     string_renderer_add_string_with_options((self), (string), false, STRING_RENDERER_ENCODING_PLAIN)
igloo_error_t       string_renderer_add_int_with_options(string_renderer_t *self, long long int val, bool allow_zero, bool allow_negative, string_renderer_encoding_t encoding);
#define             string_renderer_add_int(self,val)           string_renderer_add_int_with_options((self), (val), true, true, STRING_RENDERER_ENCODING_PLAIN)

igloo_error_t       string_renderer_add_kv_with_options(string_renderer_t *self, const char *key, const char *value, string_renderer_encoding_t key_encoding, bool allow_null_value, bool allow_empty_value);
#define             string_renderer_add_kv(self,key,value)      string_renderer_add_kv_with_options((self), (key), (value), STRING_RENDERER_ENCODING_DEFAULT, true, true)
igloo_error_t       string_renderer_add_ki_with_options(string_renderer_t *self, const char *key, long long int value, string_renderer_encoding_t key_encoding, bool allow_zero, bool allow_negative);
#define             string_renderer_add_ki(self,key,value)      string_renderer_add_ki_with_options((self), (key), (value), STRING_RENDERER_ENCODING_DEFAULT, true, true)

/* the pointer becomes invalid by calling ANY of the other functions defined here */
const char *        string_renderer_to_string_zero_copy(string_renderer_t *self);

#endif
