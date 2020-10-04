/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* This file contains functions for rendering JSON. */

#ifndef __JSON_H__
#define __JSON_H__

#include <stdint.h>

#define JSON_RENDERER_FLAGS_NONE            0

typedef enum {
    JSON_ELEMENT_TYPE_OBJECT,
    JSON_ELEMENT_TYPE_ARRAY
} json_element_type_t;

typedef struct json_renderer_tag json_renderer_t;

json_renderer_t * json_renderer_create(unsigned int flags);

char * json_renderer_finish(json_renderer_t *renderer);

void json_renderer_begin(json_renderer_t *renderer, json_element_type_t type);
void json_renderer_end(json_renderer_t *renderer);

void json_renderer_write_key(json_renderer_t *renderer, const char *key, unsigned int flags);

void json_renderer_write_null(json_renderer_t *renderer);
void json_renderer_write_boolean(json_renderer_t *renderer, int val);
void json_renderer_write_string(json_renderer_t *renderer, const char *string, unsigned int flags);
void json_renderer_write_int(json_renderer_t *renderer, intmax_t val);
void json_renderer_write_uint(json_renderer_t *renderer, uintmax_t val);

#endif
