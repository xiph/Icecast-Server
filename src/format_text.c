/* Icecast
 *
 * This program is distributed under the GNU General Public License,
 * version 2. A copy of this license is included with this source.
 * At your option, this specific source file can also be distributed
 * under the GNU GPL version 3.
 *
 * Copyright 2021,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdbool.h>

#include "source.h"
#include "format.h"
#include "format_text.h"

#define CATMODULE "format-text"

#include "logging.h"

typedef struct {
    bool skipchar;
    char skipchar_char;
} text_state_t;

static void text_free_plugin(format_plugin_t *plugin)
{
    free(plugin->_state);
    free(plugin);
}

static size_t skipchar(char *text, char skip, size_t len)
{
    char *inp = text;
    char *outp = text;
    size_t ret = 0;

    // skip prefix.
    for (; len && *inp != skip; inp++, outp++, len--, ret++);

    for (; len; inp++, len--) {
        if (*inp != skip) {
            *outp = *inp;
            outp++;
            ret++;
        }
    }

    return ret;
}


static refbuf_t *text_get_buffer(source_t *source)
{
    format_plugin_t *format = source->format;
    text_state_t *state = format->_state;
    refbuf_t *refbuf = refbuf_new(1024);
    ssize_t bytes;

    bytes = client_body_read(source->client, refbuf->data, refbuf->len);
    if (bytes < 0) {
        /* Why do we do this here (not source.c)? -- ph3-der-loewe, 2018-04-17 */
        if (client_body_eof(source->client)) {
            refbuf_release (refbuf);
        }
        return NULL;
    }

    if (state->skipchar)
        bytes = skipchar(refbuf->data, state->skipchar_char, bytes);

    refbuf->len = bytes;
    refbuf->sync_point = 1;
    format->read_bytes += bytes;

    ICECAST_LOG_DDEBUG("Got buffer for source %p with %zi bytes", source, bytes);

    return refbuf;
}

int format_text_get_plugin(source_t *source)
{
    format_plugin_t *plugin = calloc(1, sizeof(format_plugin_t));
    text_state_t *state = calloc(1, sizeof(text_state_t));
    const char *skip;

    ICECAST_LOG_DEBUG("Opening text format for source %p", source);

    plugin->get_buffer = text_get_buffer;
    plugin->write_buf_to_client = format_generic_write_to_client;
    plugin->create_client_data = NULL;
    plugin->free_plugin = text_free_plugin;
    plugin->write_buf_to_file = NULL;
    plugin->set_tag = NULL;
    plugin->apply_settings = NULL;

    plugin->contenttype = httpp_getvar(source->parser, "content-type");

    skip = httpp_getvar(source->parser, "x-icecast-text-skip-char");
    if (skip) {
        ICECAST_LOG_WARN("Source %p on mount %#H uses experimental X-Icecast-Text-Skip-Char:-header", source, source->mount);
    }
    if (skip && strlen(skip) == 3 && skip[0] == '%') {
        bool valid = true;
        size_t i;

        for (i = 1; i < 3; i++) {
            const char c = skip[i];

            state->skipchar_char <<= 4;
            if (c >= '0' && c <= '9') {
                state->skipchar_char += c - '0';
            } else if (c >= 'a' && c <= 'f') {
                state->skipchar_char += c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                state->skipchar_char += c - 'A' + 10;
            } else {
                valid = false;
            }
        }

        state->skipchar = valid;
    }

    plugin->_state = state;
    vorbis_comment_init(&plugin->vc);
    source->format = plugin;

    return 0;
}
