/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */


/* Ogg codec handler for FLAC logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ogg/ogg.h>
#include <string.h>

#include "refbuf.h"
#include "metadata_xiph.h"
#include "format_ogg.h"
#include "client.h"
#include "stats.h"

#define CATMODULE "format-flac"
#include "logging.h"

typedef enum {
    FLAC_BLOCK_TYPE__ERROR = -1,
    FLAC_BLOCK_TYPE_STREAMINFO = 0,
    FLAC_BLOCK_TYPE_PADDING = 1,
    FLAC_BLOCK_TYPE_APPLICATION = 2,
    FLAC_BLOCK_TYPE_SEEKTABLE = 3,
    FLAC_BLOCK_TYPE_VORBIS_COMMENT = 4,
    FLAC_BLOCK_TYPE_CUESHEET = 5,
    FLAC_BLOCK_TYPE_PICTURE = 6
} flac_block_type_t;

typedef struct {
    flac_block_type_t type;
    bool last;
    size_t len;
    const void *data;
} flac_block_t;

/* returns true if parse was ok, false on error */
static bool flac_parse_block(flac_block_t *block, const ogg_packet * packet, size_t offset)
{
    uint8_t type;
    uint32_t len;

    /* check header length */
    if ((size_t)packet->bytes <= (4 + offset))
        return false;

    type = packet->packet[offset];

    /* 0xFF is the sync code for a FRAME not a block */
    if (type == 0xFF)
        return false;

    memset(block, 0, sizeof(*block));

    if (type & 0x80) {
        block->last = true;
        type &= 0x7F;
    }

    if (type > FLAC_BLOCK_TYPE_PICTURE)
        return false;

    block->type = type;

    len  = (unsigned char)packet->packet[1 + offset];
    len <<= 8;
    len |= (unsigned char)packet->packet[2 + offset];
    len <<= 8;
    len |= (unsigned char)packet->packet[3 + offset];

    /* check Ogg packet size vs. self-sync size */
    if ((size_t)packet->bytes != (len + 4 + offset))
        return false;

    block->len = len;
    block->data = packet->packet + 4 + offset;

    return true;
}

static const char * flac_block_type_to_name(flac_block_type_t type)
{
    switch (type) {
        case FLAC_BLOCK_TYPE__ERROR:
            return "<error>";
            break;
        case FLAC_BLOCK_TYPE_STREAMINFO:
            return "STREAMINFO";
            break;
        case FLAC_BLOCK_TYPE_PADDING:
            return "PADDING";
            break;
        case FLAC_BLOCK_TYPE_APPLICATION:
            return "APPLICATION";
            break;
        case FLAC_BLOCK_TYPE_SEEKTABLE:
            return "SEEKTABLE";
            break;
        case FLAC_BLOCK_TYPE_VORBIS_COMMENT:
            return "VORBIS_COMMENT";
            break;
        case FLAC_BLOCK_TYPE_CUESHEET:
            return "CUESHEET";
            break;
        case FLAC_BLOCK_TYPE_PICTURE:
            return "PICTURE";
            break;
    }

    return "<unknown>";
}

static void flac_handle_block_streaminfo(format_plugin_t *plugin, ogg_state_t *ogg_info, ogg_codec_t *codec, const flac_block_t *block)
{
    uint32_t raw;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits;

    if (block->len != 34) {
        ICECAST_LOG_ERROR("Can not parse FLAC header block STREAMINFO");
        return;
    }

    raw = metadata_xiph_read_u32be_unaligned(block->data + 10);
    sample_rate = ((raw >> 12) & 0xfffff) + 0;
    channels    = ((raw >>  9) & 0x7    ) + 1;
    bits        = ((raw >>  4) & 0x1F   ) + 1;

    stats_event_args(ogg_info->mount, "audio_samplerate", "%ld", (long int)sample_rate);
    stats_event_args(ogg_info->mount, "audio_channels", "%ld", (long int)channels);
    stats_event_args(ogg_info->mount, "audio_bits", "%ld", (long int)bits);
}

static void flac_handle_block(format_plugin_t *plugin, ogg_state_t *ogg_info, ogg_codec_t *codec, const flac_block_t *block)
{
    ICECAST_LOG_DEBUG("Found header of type %s%s with %zu bytes of data", flac_block_type_to_name(block->type), block->last ? "(last)" : "", block->len);

    switch (block->type) {
        case FLAC_BLOCK_TYPE_STREAMINFO:
            flac_handle_block_streaminfo(plugin, ogg_info, codec, block);
            break;
        case FLAC_BLOCK_TYPE_VORBIS_COMMENT:
            vorbis_comment_clear(&plugin->vc);
            vorbis_comment_init(&plugin->vc);
            if (!metadata_xiph_read_vorbis_comments(&plugin->vc, block->data, block->len)) {
                ICECAST_LOG_ERROR("Can not parse FLAC header block VORBIS_COMMENT");
            }
            ogg_info->log_metadata = 1;
            break;
        default:
            /* no-op */
            break;
    }
}

static void flac_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    ICECAST_LOG_DEBUG("freeing FLAC codec");
    stats_event(ogg_info->mount, "FLAC_version", NULL);
    ogg_stream_clear(&codec->os);
    free(codec);
}

/* Here, we just verify the page is ok and then add it to the queue */
static refbuf_t *process_flac_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page, format_plugin_t *plugin)
{
    refbuf_t * refbuf;

    if (codec->headers) {
        ogg_packet packet;

        if (ogg_stream_pagein(&codec->os, page) < 0) {
            ogg_info->error = 1;
            return NULL;
        }

        while (ogg_stream_packetout(&codec->os, &packet)) {
            flac_block_t block;

            if (flac_parse_block(&block, &packet, 0)) {
                flac_handle_block(plugin, ogg_info, codec, &block);

                if (block.last) {
                    codec->headers = 0;
                    break;
                }

                continue;
            }

            ogg_info->error = 1;

            return NULL;
        }

        if (codec->headers) {
            format_ogg_attach_header(ogg_info, page);
            return NULL;
        }
    }

    refbuf = make_refbuf_with_page(page);

    return refbuf;
}


/* Check for flac header in logical stream */

ogg_codec_t *initial_flac_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc(1, sizeof(ogg_codec_t));
    ogg_packet packet;

    ogg_stream_init(&codec->os, ogg_page_serialno(page));
    ogg_stream_pagein(&codec->os, page);

    ogg_stream_packetout(&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for FLAC codec");
    do
    {
        unsigned char *parse = packet.packet;
        flac_block_t block;

        if (page->header_len + page->body_len != 79)
            break;
        if (*parse != 0x7F)
            break;
        parse++;
        if (memcmp(parse, "FLAC", 4) != 0)
            break;

        ICECAST_LOG_INFO("seen initial FLAC header");

        parse += 4;
        stats_event_args(ogg_info->mount, "FLAC_version", "%d.%d",  parse[0], parse[1]);
        codec->process_page = process_flac_page;
        codec->codec_free = flac_codec_free;
        codec->headers = 1;
        codec->name = "FLAC";

        if (flac_parse_block(&block, &packet, 13))
            flac_handle_block(plugin, ogg_info, codec, &block);

        format_ogg_attach_header(ogg_info, page);
        return codec;
    } while (0);

    ogg_stream_clear(&codec->os);
    free(codec);
    return NULL;
}

