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


/* Ogg codec handler for theora logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ogg/ogg.h>
#include <theora/theora.h>

typedef struct source_tag source_t;

#include "refbuf.h"
#include "format_ogg.h"
#include "format_theora.h"
#include "client.h"
#include "stats.h"

#define CATMODULE "format-theora"
#include "logging.h"


typedef struct _theora_codec_tag
{
    theora_info     ti;
    theora_comment  tc;
    int             granule_shift;
    ogg_int64_t     last_iframe;
    ogg_int64_t     prev_granulepos;
} theora_codec_t;


static void theora_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    theora_codec_t *theora = codec->specific;

    DEBUG0 ("freeing theora codec");
    stats_event (ogg_info->mount, "video_bitrate", NULL);
    stats_event (ogg_info->mount, "framerate", NULL);
    stats_event (ogg_info->mount, "frame_size", NULL);
    theora_info_clear (&theora->ti);
    theora_comment_clear (&theora->tc);
    ogg_stream_clear (&codec->os);
    free (theora);
    free (codec);
}


/* theora pages are not rebuilt, so here we just for headers and then
 * pass them straight through to the the queue
 */
static refbuf_t *process_theora_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page)
{
    theora_codec_t *theora = codec->specific;
    ogg_packet packet;
    int header_page = 0;
    int has_keyframe = 0;
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
        if (theora_packet_isheader (&packet))
        {
            if (theora_decode_header (&theora->ti, &theora->tc, &packet) < 0)
            {
                ogg_info->error = 1;
                WARN0 ("problem with theora header");
                return NULL;
            }
            header_page = 1;
            codec->headers++;
            continue;
        }
        if (codec->headers < 3)
        {
            ogg_info->error = 1;
            ERROR0 ("Not enough header packets");
            return NULL;
        }
        if (theora_packet_iskeyframe (&packet))
            has_keyframe = 1;
    }
    if (header_page)
    {
        format_ogg_attach_header (ogg_info, page);
        return NULL;
    }

    refbuf = make_refbuf_with_page (page);
    /* DEBUG3 ("refbuf %p has pageno %ld, %llu", refbuf, ogg_page_pageno (page), (uint64_t)granulepos); */

    if (has_keyframe && codec->possible_start)
    {
        codec->possible_start->sync_point = 1;
        refbuf_release (codec->possible_start);
        codec->possible_start = NULL;
    }
    if (granulepos != theora->prev_granulepos || granulepos == 0)
    {
        if (codec->possible_start)
            refbuf_release (codec->possible_start);
        refbuf_addref (refbuf);
        codec->possible_start = refbuf;
    }
    theora->prev_granulepos = granulepos;

    return refbuf;
}


/* Check if specified BOS page is the start of a theora stream and
 * if so, create a codec structure for handling it
 */
ogg_codec_t *initial_theora_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    theora_codec_t *theora_codec = calloc (1, sizeof (theora_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    theora_info_init (&theora_codec->ti);
    theora_comment_init (&theora_codec->tc);

    ogg_stream_packetout (&codec->os, &packet);

    DEBUG0("checking for theora codec");
    if (theora_decode_header (&theora_codec->ti, &theora_codec->tc, &packet) < 0)
    {
        theora_info_clear (&theora_codec->ti);
        theora_comment_clear (&theora_codec->tc);
        ogg_stream_clear (&codec->os);
        free (theora_codec);
        free (codec);
        return NULL;
    }
    INFO0 ("seen initial theora header");
    codec->specific = theora_codec;
    codec->process_page = process_theora_page;
    codec->codec_free = theora_codec_free;
    codec->headers = 1;
    format_ogg_attach_header (ogg_info, page);
    ogg_info->codec_sync = codec;
    return codec;
}

