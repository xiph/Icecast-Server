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

/*
 * Overview:
 * ---------
 *  * First a renderer is created using json_renderer_create().
 *  * Elements can then be written to the renderer in order they appear in the
 *    final JSON using json_renderer_write_*().
 *  * Objects and arrays can be opened and closed using json_renderer_begin()
 *    and json_renderer_end()
 *  * Objects and arrays are automatically closed on json_renderer_finish()
 *  * json_renderer_finish() is used to fetch the final rendering result
 *    and destroy the renderer object. The renderer object is invalid after this.
 *    The returned value must be freed using free() by the caller.
 */

/* Dummy value used when no flags are passed. */
#define JSON_RENDERER_FLAGS_NONE            0

/* Enum of JSON element types, may not include all element types */
typedef enum {
    /* Objects ('{key:value}') */
    JSON_ELEMENT_TYPE_OBJECT,
    /* Arrays ('[a,b,c]') */
    JSON_ELEMENT_TYPE_ARRAY
} json_element_type_t;

/* Type of the renderer */
typedef struct json_renderer_tag json_renderer_t;

/*
 * json_renderer_create() creates a renderer object using
 * the given flags. See the definition of the flags for details.
 * If no flags are to be passed, JSON_RENDERER_FLAGS_NONE MUST be used.
 */
json_renderer_t * json_renderer_create(unsigned int flags);

/*
 * json_renderer_finish() finishes the rendering, destroys the renderer,
 * and returns the rendered JSON.
 * The renderer is invalid after this and MUST NOT be reused.
 * The returned buffer MUST be freed by the caller using free().
 */
char * json_renderer_finish(json_renderer_t **rendererptr);

/*
 * json_renderer_begin() begins a new sub-element that might be an object or array.
 * json_renderer_begin() MUST be matched with a correspoinding json_renderer_end()
 * unless at end of the document as all elements that are still open are closed
 * automatically.
 */
void json_renderer_begin(json_renderer_t *renderer, json_element_type_t type);
/*
 * json_renderer_end() closes the current sub-element that was opened using json_renderer_begin().
 */
void json_renderer_end(json_renderer_t *renderer);

/*
 * json_renderer_write_key() writes a key for a object key-value pair.
 * Parameters work the same as for json_renderer_write_string().
 */
void json_renderer_write_key(json_renderer_t *renderer, const char *key, unsigned int flags);

/*
 * json_renderer_write_null() writes a null-value.
 */
void json_renderer_write_null(json_renderer_t *renderer);

/*
 * json_renderer_write_boolean() writes a boolean value.
 * The parameter val is interpreted by standard C rules for truth.
 */

void json_renderer_write_boolean(json_renderer_t *renderer, int val);

/*
 * json_renderer_write_string() writes a string value.
 * The parameter string must be in UTF-8 encoding and \0-terminated.
 * The parameter flags can be used to hint the rendering engine.
 * If no flags are given JSON_RENDERER_FLAGS_NONE MUST be given.
 */
void json_renderer_write_string(json_renderer_t *renderer, const char *string, unsigned int flags);

/*
 * json_renderer_write_int() writes am signed integer value.
 * Note: JSON does not define specific types of numbers (signed/unsigned, integer/fixed ponint/floating point)
 *       so this writes the integer as generic number.
 */
void json_renderer_write_int(json_renderer_t *renderer, intmax_t val);

/*
 * json_renderer_write_uint() writes an unsigned integer value.
 * Note: JSON does not define specific types of numbers (signed/unsigned, integer/fixed ponint/floating point)
 *       so this writes the integer as generic number.
 */
void json_renderer_write_uint(json_renderer_t *renderer, uintmax_t val);

#endif
