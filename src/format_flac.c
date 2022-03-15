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
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */


/* Ogg codec handler for FLAC logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <ogg/ogg.h>
#include <string.h>

#include "refbuf.h"
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

static flac_block_type_t flac_blocktype(const ogg_packet * packet)
{
    uint8_t type;

    if (packet->bytes <= 4)
        return FLAC_BLOCK_TYPE__ERROR;

    type = packet->packet[0] & 0x7F;

    if (type <= FLAC_BLOCK_TYPE_PICTURE) {
        return type;
    } else {
        return FLAC_BLOCK_TYPE__ERROR;
    }
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
            if (packet.bytes >= 1) {
                uint8_t type = packet.packet[0];
                flac_block_type_t blocktype;

                if (type == 0xFF) {
                    codec->headers = 0;
                    break;
                }

                blocktype = flac_blocktype(&packet);

                ICECAST_LOG_DEBUG("Found header of type %s%s", flac_block_type_to_name(blocktype), (type & 0x80) ? "|0x80" : "");

                if (type >= 1 && type <= 0x7E)
                    continue;
                if (type >= 0x81 && type <= 0xFE)
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

        format_ogg_attach_header(ogg_info, page);
        return codec;
    } while (0);

    ogg_stream_clear(&codec->os);
    free(codec);
    return NULL;
}

