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


/* Ogg codec handler for FLAC logical streams */

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

#define CATMODULE "format-flac"
#include "logging.h"


static void flac_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    ICECAST_LOG_DEBUG("freeing FLAC codec");
    stats_event (ogg_info->mount, "FLAC_version", NULL);
    ogg_stream_clear (&codec->os);
    free (codec);
}


/* Here, we just verify the page is ok and then add it to the queue */
static refbuf_t *process_flac_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t * refbuf;

    if (codec->headers)
    {
        ogg_packet packet;
        if (ogg_stream_pagein (&codec->os, page) < 0)
        {
            ogg_info->error = 1;
            return NULL;
        }
        while (ogg_stream_packetout (&codec->os, &packet))
        {
            int type = packet.packet[0];
            if (type == 0xFF)
            {
                codec->headers = 0;
                break;
            }
            if (type >= 1 && type <= 0x7E)
                continue;
            if (type >= 0x81 && type <= 0xFE)
                continue;
            ogg_info->error = 1;
            return NULL;
        }
        if (codec->headers)
        {
            format_ogg_attach_header (ogg_info, page);
            return NULL;
        }
    }
    refbuf = make_refbuf_with_page (page);
    return refbuf;
}


/* Check for flac header in logical stream */

ogg_codec_t *initial_flac_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    ogg_stream_packetout (&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for FLAC codec");
    do
    {
        unsigned char *parse = packet.packet;

        if (page->header_len + page->body_len != 79)
            break;
        if (*parse != 0x7F)
            break;
        parse++;
        if (memcmp (parse, "FLAC", 4) != 0)
            break;

        ICECAST_LOG_INFO("seen initial FLAC header");

        parse += 4;
        stats_event_args (ogg_info->mount, "FLAC_version", "%d.%d",  parse[0], parse[1]);
        codec->process_page = process_flac_page;
        codec->codec_free = flac_codec_free;
        codec->headers = 1;
        codec->name = "FLAC";

        format_ogg_attach_header (ogg_info, page);
        return codec;
    } while (0);

    ogg_stream_clear (&codec->os);
    free (codec);
    return NULL;
}

