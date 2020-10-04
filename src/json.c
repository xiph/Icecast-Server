/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * This file contains functions for rendering JSON.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <inttypes.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

#include "logging.h"
#define CATMODULE "json"

#define MAX_RECURSION   64

struct json_renderer_tag {
    unsigned int flags;

    int valid;

    char *buffer;
    size_t bufferlen;
    size_t bufferfill;

    char levelinfo[MAX_RECURSION];
    size_t level;
};

static int allocate_buffer(json_renderer_t *renderer, size_t needed)
{
    size_t required = needed + renderer->level;
    size_t have = renderer->bufferlen - renderer->bufferfill;

    if (!renderer->valid)
        return 1;

    if (have < required) {
        size_t want;
        char *n;

        if (required < 128)
            required = 128;

        want = renderer->bufferfill + required;
        if (want < 512)
            want = 512;

        n = realloc(renderer->buffer, want);

        if (!n)
            return 1;

        renderer->buffer = n;
    }

    return 0;
}

static void json_renderer_destroy(json_renderer_t *renderer)
{
    if (!renderer)
        return;

    renderer->valid = 0;

    free(renderer->buffer);
    free(renderer);
}

json_renderer_t * json_renderer_create(unsigned int flags)
{
    json_renderer_t *renderer = calloc(1, sizeof(json_renderer_t));

    if (!renderer)
        return NULL;

    renderer->flags = flags;
    renderer->valid = 1;

    renderer->levelinfo[0] = '0';
    renderer->level = 1;

    if (allocate_buffer(renderer, 0) != 0) {
        json_renderer_destroy(renderer);
        return NULL;
    }

    return renderer;
}

char * json_renderer_finish(json_renderer_t *renderer)
{
    char *ret;

    if (!renderer)
        return NULL;

    if (!renderer->valid) {
        json_renderer_destroy(renderer);
        return NULL;
    }

    for (; renderer->level; renderer->level--) {
        switch (renderer->levelinfo[renderer->level-1]) {
            case '0':
                renderer->buffer[renderer->bufferfill++] = 0;
            break;
            case 'o':
            case 'O':
                renderer->buffer[renderer->bufferfill++] = '}';
            break;
            case 'a':
            case 'A':
                renderer->buffer[renderer->bufferfill++] = ']';
            break;
            default:
                json_renderer_destroy(renderer);
                return NULL;
            break;
        }
    }

    ret = renderer->buffer;
    renderer->buffer = NULL;
    json_renderer_destroy(renderer);
    return ret;
}

static int write_raw(json_renderer_t *renderer, const char *raw, int begin)
{
    size_t rawlen = strlen(raw);
    size_t want = rawlen;
    char level;
    char seperator = 0;

    if (!renderer->valid)
        return 1;

    level = renderer->levelinfo[renderer->level-1];

    if (begin) {
        if (level == 'O' || level == 'A') {
            seperator = ',';
        } else if (level == 'o') {
            renderer->levelinfo[renderer->level-1] = 'O';
        } else if (level == 'a') {
            renderer->levelinfo[renderer->level-1] = 'A';
        } else if (level == 'P') {
            seperator = ':';
            renderer->level--;
        }
    }

    if (seperator)
        want++;

    if (allocate_buffer(renderer, want) != 0)
        return 1;

    if (seperator)
        renderer->buffer[renderer->bufferfill++] = seperator;

    memcpy(&(renderer->buffer[renderer->bufferfill]), raw, rawlen);
    renderer->bufferfill += rawlen;

    return 0;
}

static int want_write_value(json_renderer_t *renderer)
{
    char level;


    if (!renderer || !renderer->valid)
        return 1;

    level = renderer->levelinfo[renderer->level-1];
    if (level == 'o' || level == 'O') {
        renderer->valid = 0;
        return 1;
    }

    return 0;
}

void json_renderer_begin(json_renderer_t *renderer, json_element_type_t type)
{
    const char *towrite;
    char next_level;

    if (!renderer || !renderer->valid)
        return;

    if (renderer->level == MAX_RECURSION) {
        renderer->valid = 0;
        return;
    }

    switch (type) {
        case JSON_ELEMENT_TYPE_OBJECT:
            next_level = 'o';
            towrite = "{";
        break;
        case JSON_ELEMENT_TYPE_ARRAY:
            next_level = 'a';
            towrite = "[";
        break;
        default:
            renderer->valid = 0;
        return;
    }

    write_raw(renderer, towrite, 1);
    renderer->levelinfo[renderer->level++] = next_level;
}

void json_renderer_end(json_renderer_t *renderer)
{
    if (!renderer || !renderer->valid)
        return;

    switch (renderer->levelinfo[renderer->level-1]) {
        case 'o':
        case 'O':
            write_raw(renderer, "}", 0);
            renderer->level--;
        break;
        case 'a':
        case 'A':
            write_raw(renderer, "]", 0);
            renderer->level--;
        break;
        default:
            renderer->valid = 0;
        break;
    }
}

void json_renderer_write_null(json_renderer_t *renderer)
{
    if (want_write_value(renderer) != 0)
        return;
    write_raw(renderer, "null", 1);
}

void json_renderer_write_boolean(json_renderer_t *renderer, int val)
{
    if (want_write_value(renderer) != 0)
        return;
    write_raw(renderer, val ? "true" : "false", 1);
}

static void write_string(json_renderer_t *renderer, const char *string, unsigned int flags)
{
    // Allocate for the quotes plus 110% of the string length to account for escapes.
    if (allocate_buffer(renderer, 2 + ((strlen(string) * 110) / 100)) != 0)
        return;

    write_raw(renderer, "\"", 1);

    for (; *string && renderer->valid; string++) {
        if (renderer->bufferfill == renderer->bufferlen) {
            // Reallocate buffer, same rules as above, expect that we only need one additional quote.
            if (allocate_buffer(renderer, 1 + ((strlen(string) * 110) / 100)) != 0)
                return;
        }

        if (*string < 0x20) {
            char buf[7];
            snprintf(buf, sizeof(buf), "\\u%.4x", (unsigned int)*string);
            write_raw(renderer, buf, 0);
        } else if (*string == '\\' || *string == '"') {
            if (allocate_buffer(renderer, 2) != 0)
                return;
            renderer->buffer[renderer->bufferfill++] = '\\';
            renderer->buffer[renderer->bufferfill++] = *string;
        } else {
            renderer->buffer[renderer->bufferfill++] = *string;
        }
    }

    write_raw(renderer, "\"", 0);
}

void json_renderer_write_key(json_renderer_t *renderer, const char *key, unsigned int flags)
{
    char level;

    if (!renderer || !renderer->valid)
        return;

    level = renderer->levelinfo[renderer->level-1];
    if (level != 'o' && level != 'O') {
        renderer->valid = 0;
        return;
    }

    if (renderer->level == MAX_RECURSION) {
        renderer->valid = 0;
        return;
    }

    write_string(renderer, key, flags);

    renderer->levelinfo[renderer->level++] = 'P';
}

void json_renderer_write_string(json_renderer_t *renderer, const char *string, unsigned int flags)
{
    if (want_write_value(renderer) != 0)
        return;
    write_string(renderer, string, flags);
}

void json_renderer_write_int(json_renderer_t *renderer, intmax_t val)
{
    char buf[80];

    if (want_write_value(renderer) != 0)
        return;

    snprintf(buf, sizeof(buf), "%" PRIdMAX, val);

    write_raw(renderer, buf, 1);
}

void json_renderer_write_uint(json_renderer_t *renderer, uintmax_t val)
{
    char buf[80];

    if (want_write_value(renderer) != 0)
        return;

    snprintf(buf, sizeof(buf), "%" PRIuMAX, val);

    write_raw(renderer, buf, 1);
}
