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
 */


/* Ogg codec handler for kate logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>
#ifdef HAVE_KATE
#include <kate/oggkate.h>
#endif

typedef struct source_tag source_t;

#include "refbuf.h"
#include "format_ogg.h"
#include "format_kate.h"
#include "client.h"
#include "stats.h"

#define CATMODULE "format-kate"
#include "logging.h"


typedef struct _kate_codec_tag
{
    int             headers_done;
#ifdef HAVE_KATE
    kate_info       ki;
    kate_comment    kc;
#endif
    int             num_headers;
    int             granule_shift;
    ogg_int64_t     last_iframe;
    ogg_int64_t     prev_granulepos;
} kate_codec_t;


static void kate_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    kate_codec_t *kate = codec->specific;

    ICECAST_LOG_DEBUG("freeing kate codec");
    /* TODO: should i replace with something or just remove
    stats_event (ogg_info->mount, "video_bitrate", NULL);
    stats_event (ogg_info->mount, "video_quality", NULL);
    stats_event (ogg_info->mount, "frame_rate", NULL);
    stats_event (ogg_info->mount, "frame_size", NULL);
    */
#ifdef HAVE_KATE
    kate_info_clear (&kate->ki);
    kate_comment_clear (&kate->kc);
#endif
    ogg_stream_clear (&codec->os);
    free (kate);
    free (codec);
}


/* kate pages are not rebuilt, so here we just for headers and then
 * pass them straight through to the the queue
 */
static refbuf_t *process_kate_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page)
{
    kate_codec_t *kate = codec->specific;
    ogg_packet packet;
    int header_page = 0;
    refbuf_t *refbuf = NULL;
    ogg_int64_t granulepos;

    if (ogg_stream_pagein (&codec->os, page) < 0)
    {
        ogg_info->error = 1;
        return NULL;
    }
    granulepos = ogg_page_granulepos (page);

    while (ogg_stream_packetout (&codec->os, &packet) > 0)
    {
        if (!kate->headers_done)
        {
#ifdef HAVE_KATE
            int ret = kate_ogg_decode_headerin (&kate->ki, &kate->kc, &packet);
            if (ret < 0)
            {
                ogg_info->error = 1;
                ICECAST_LOG_WARN("problem with kate header");
                return NULL;
            }
            header_page = 1;
            kate->num_headers = kate->ki.num_headers;
            codec->headers++;
            if (ret > 0)
            {
                kate->headers_done = 1;
                /* TODO: what to replace this with ?
                ogg_info->bitrate += theora->ti.target_bitrate;
                stats_event_args (ogg_info->mount, "video_bitrate", "%ld",
                        (long)theora->ti.target_bitrate);
                stats_event_args (ogg_info->mount, "video_quality", "%ld",
                        (long)theora->ti.quality);
                stats_event_args (ogg_info->mount, "frame_size", "%ld x %ld",
                        (long)theora->ti.frame_width,
                        (long)theora->ti.frame_height);
                stats_event_args (ogg_info->mount, "frame_rate", "%.2f",
                        (float)theora->ti.fps_numerator/theora->ti.fps_denominator);
                */
            }
            continue;
#else
            header_page = (packet.bytes>0 && (packet.packet[0]&0x80));
            if (!header_page)
                break;
            codec->headers++;
            if (packet.packet[0]==0x80)
            {
                if (packet.bytes<64) return NULL;
                /* we peek for the number of headers to expect */
                kate->num_headers = packet.packet[11];
            }
            continue;
#endif
        }

        if (codec->headers < kate->num_headers)
        {
            ogg_info->error = 1;
            ICECAST_LOG_ERROR("Not enough header packets");
            return NULL;
        }
    }
    if (header_page)
    {
        format_ogg_attach_header (ogg_info, page);
        return NULL;
    }

    refbuf = make_refbuf_with_page (page);
    /* ICECAST_LOG_DEBUG("refbuf %p has pageno %ld, %llu", refbuf, ogg_page_pageno (page), (uint64_t)granulepos); */

    if (codec->possible_start)
    {
        /* we don't bother trying to know where we can start, we'll just
           start whenever we have to, video's more important and in the majority
           of the cases it's ok if we lose an event we're seeking in the middle
           of, as we won't have display artifacts as we'd have with video */
        codec->possible_start->sync_point = 1;
        refbuf_release (codec->possible_start);
        codec->possible_start = NULL;
    }
    if (granulepos != kate->prev_granulepos || granulepos == 0)
    {
        if (codec->possible_start)
            refbuf_release (codec->possible_start);
        refbuf_addref (refbuf);
        codec->possible_start = refbuf;
    }
    kate->prev_granulepos = granulepos;

    return refbuf;
}


/* Check if specified BOS page is the start of a kate stream and
 * if so, create a codec structure for handling it
 */
ogg_codec_t *initial_kate_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    kate_codec_t *kate_codec = calloc (1, sizeof (kate_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

#ifdef HAVE_KATE
    kate_info_init (&kate_codec->ki);
    kate_comment_init (&kate_codec->kc);
#endif

    ogg_stream_packetout (&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for kate codec");
#ifdef HAVE_KATE
    if (kate_ogg_decode_headerin (&kate_codec->ki, &kate_codec->kc, &packet) < 0)
    {
        kate_info_clear (&kate_codec->ki);
        kate_comment_clear (&kate_codec->kc);
        ogg_stream_clear (&codec->os);
        free (kate_codec);
        free (codec);
        return NULL;
    }
#else
    /* we don't have libkate, so we examine the packet magic by hand */
    if ((packet.bytes<9) || memcmp(packet.packet, "\x80kate\0\0\0\0", 9))
    {
        ogg_stream_clear (&codec->os);
        free (kate_codec);
        free (codec);
        return NULL;
    }
#endif

    ICECAST_LOG_INFO("seen initial kate header");
    codec->specific = kate_codec;
    codec->process_page = process_kate_page;
    codec->codec_free = kate_codec_free;
    codec->headers = 1;
    codec->name = "Kate";

    format_ogg_attach_header (ogg_info, page);
    ogg_info->codec_sync = codec;
    return codec;
}

