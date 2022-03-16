/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "metadata_xiph.h"

#include "logging.h"
#define CATMODULE "metadata-xiph"

uint32_t metadata_xiph_read_u32le_unaligned(const unsigned char *in)
{
    uint32_t ret = 0;
    ret += in[3];
    ret <<= 8;
    ret += in[2];
    ret <<= 8;
    ret += in[1];
    ret <<= 8;
    ret += in[0];
    return ret;
}

bool     metadata_xiph_read_vorbis_comments(vorbis_comment *vc, const void *buffer, size_t len)
{
    bool ret = true;
    size_t expected_len = 8;
    uint32_t vendor_len;
    uint32_t count;
    uint32_t i;
    char *out_buffer = NULL;
    size_t out_buffer_len = 0;

    if (!vc || !buffer || len < expected_len)
        return false;

    /* reading vendor tag and discarding it */
    vendor_len = metadata_xiph_read_u32le_unaligned(buffer);
    expected_len += vendor_len;

    if (len < expected_len)
        return false;

    buffer += 4 + vendor_len;

    count = metadata_xiph_read_u32le_unaligned(buffer);

    expected_len += count * 4;

    if (len < expected_len)
        return false;

    buffer += 4;

    for (i = 0; i < count; i++) {
        uint32_t comment_len = metadata_xiph_read_u32le_unaligned(buffer);

        buffer += 4;

        expected_len += comment_len;

        if (len < expected_len) {
            ret = false;
            break;
        }

        if (out_buffer_len < comment_len || !out_buffer) {
            char *n_out_buffer = realloc(out_buffer, comment_len + 1);
            if (!n_out_buffer) {
                ret = false;
                break;
            }

            out_buffer = n_out_buffer;
            out_buffer_len = comment_len;
        }

        memcpy(out_buffer, buffer, comment_len);
        out_buffer[comment_len] = 0;
        buffer += comment_len;

        vorbis_comment_add(vc, out_buffer);
    }

    if (!ret) {
        free(out_buffer);
        vorbis_comment_clear(vc);
        vorbis_comment_init(vc);
    }

    return ret;
}
