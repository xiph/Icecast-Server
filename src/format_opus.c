/* Icecast
 *
 *  This program is distributed under the GNU General Public License,
 *  version 2. A copy of this license is included with this source.
 *  At your option, this specific source file can also be distributed
 *  under the GNU GPL version 3.
 *
 * Copyright 2012,      David Richards, Mozilla Foundation,
 *                      and others (see AUTHORS for details).
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */


/* Ogg codec handler for opus streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>

#include "metadata_xiph.h"
#include "format_opus.h"
#include "stats.h"
#include "refbuf.h"
#include "client.h"

#define CATMODULE "format-opus"
#include "logging.h"

static void opus_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    stats_event(ogg_info->mount, "audio_channels", NULL);
    stats_event(ogg_info->mount, "audio_samplerate", NULL);
    ogg_stream_clear(&codec->os);
    free(codec);
}

static void __handle_header_opushead(ogg_state_t *ogg_info, ogg_packet *packet)
{
    if (packet->bytes < 19) {
        ICECAST_LOG_WARN("Bad Opus header: header too small, expected at least 19 byte, got %li", (long int)packet->bytes);
        return; /* Invalid OpusHead */
    }

    if ((packet->packet[8] & 0xF0) != 0) {
        ICECAST_LOG_WARN("Bad Opus header: bad header version, expected major 0, got %i", (int)packet->packet[8]);
        return; /* Invalid OpusHead Version */
    }

    stats_event_args(ogg_info->mount, "audio_channels", "%ld", (long int)packet->packet[9]);
    stats_event_args(ogg_info->mount, "audio_samplerate", "%ld", (long int)metadata_xiph_read_u32le_unaligned(packet->packet+12));
}

static void __handle_header_opustags(ogg_state_t *ogg_info, ogg_packet *packet, format_plugin_t *plugin) 
{
    size_t left = packet->bytes;
    const void *p = packet->packet;

    if (packet->bytes < 16) {
        ICECAST_LOG_WARN("Bad Opus header: header too small, expected at least 16 byte, got %li", (long int)packet->bytes);
        return; /* Invalid OpusHead */
    }

    /* Skip header "OpusTags" */
    p += 8;
    left -= 8;

    vorbis_comment_clear(&plugin->vc);
    vorbis_comment_init(&plugin->vc);
    if (!metadata_xiph_read_vorbis_comments(&plugin->vc, p, left)) {
        ICECAST_LOG_WARN("Bad Opus header: corrupted OpusTags header.");
    }
    ogg_info->log_metadata = 1;
}

static void __handle_header(ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_packet *packet, format_plugin_t *plugin)
{
    ICECAST_LOG_DEBUG("Got Opus header");
    if (packet->bytes < 8) {
        ICECAST_LOG_DEBUG("Not a real header, less than 8 bytes in size.");
        return; /* packet is not a header */
    }

    if (strncmp((const char*)packet->packet, "OpusHead", 8) == 0) {
        ICECAST_LOG_DEBUG("Got Opus header: OpusHead");
        __handle_header_opushead(ogg_info, packet);
    } else if (strncmp((const char*)packet->packet, "OpusTags", 8) == 0) {
        ICECAST_LOG_DEBUG("Got Opus header: OpusTags");
        __handle_header_opustags(ogg_info, packet, plugin);
    } else {
        ICECAST_LOG_DEBUG("Unknown header or data.");
        return; /* Unknown header or data */
    }
}

static refbuf_t *process_opus_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page, format_plugin_t *plugin)
{
    refbuf_t *refbuf;

    if (codec->headers < 2)
    {
        ogg_packet packet;

        ogg_stream_pagein (&codec->os, page);
        while (ogg_stream_packetout (&codec->os, &packet) > 0)
        {
           /* first time around (normal case) yields comments */
           codec->headers++;
            __handle_header(ogg_info, codec, &packet, plugin);
        }
        /* add header page to associated list */
        format_ogg_attach_header (ogg_info, page);
        return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    return refbuf;
}


ogg_codec_t *initial_opus_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc(1, sizeof (ogg_codec_t));
    ogg_packet packet;

    ogg_stream_init(&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein(&codec->os, page);

    ogg_stream_packetout(&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for opus codec");
    if (packet.bytes < 8 || strncmp((char *)packet.packet, "OpusHead", 8) != 0)
    {
        ogg_stream_clear(&codec->os);
        free(codec);
        return NULL;
    }
    __handle_header(ogg_info, codec, &packet, plugin);
    ICECAST_LOG_INFO("seen initial opus header");
    codec->process_page = process_opus_page;
    codec->codec_free = opus_codec_free;
    codec->name = "Opus";
    codec->headers = 1;
    format_ogg_attach_header (ogg_info, page);
    return codec;
}

