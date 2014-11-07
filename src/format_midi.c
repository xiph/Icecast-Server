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


/* Ogg codec handler for MIDI logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ogg/ogg.h>
#include <string.h>

typedef struct source_tag source_t;

#include "refbuf.h"
#include "format_ogg.h"
#include "client.h"
#include "stats.h"

#define CATMODULE "format-midi"
#include "logging.h"


static void midi_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    ICECAST_LOG_DEBUG("freeing MIDI codec");
    ogg_stream_clear (&codec->os);
    free (codec);
}


/* Here, we just verify the page is ok and then add it to the queue */
static refbuf_t *process_midi_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t * refbuf;

    if (ogg_stream_pagein (&codec->os, page) < 0)
    {
        ogg_info->error = 1;
        return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    return refbuf;
}


/* Check for midi header in logical stream */

ogg_codec_t *initial_midi_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    ogg_stream_packetout (&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for MIDI codec");
    do
    {
        if (packet.bytes < 9)
            break;
        if (memcmp (packet.packet, "OggMIDI\000", 8) != 0)
            break;
        if (packet.bytes != 12)
            break;

        ICECAST_LOG_INFO("seen initial MIDI header");
        codec->process_page = process_midi_page;
        codec->codec_free = midi_codec_free;
        codec->headers = 1;
        codec->name = "MIDI";

        format_ogg_attach_header (ogg_info, page);
        return codec;
    } while (0);

    ogg_stream_clear (&codec->os);
    free (codec);
    return NULL;
}

