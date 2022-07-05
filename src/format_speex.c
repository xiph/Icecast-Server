/* Icecast
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org>,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */


/* Ogg codec handler for speex streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ogg/ogg.h>
#include <speex/speex_header.h>

#include "format_speex.h"
#include "refbuf.h"
#include "client.h"

#define CATMODULE "format-speex"
#include "logging.h"

static void speex_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    ogg_stream_clear (&codec->os);
    free (codec);
}


static refbuf_t *process_speex_page (ogg_state_t *ogg_info,
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
        }
        /* add header page to associated list */
        format_ogg_attach_header (ogg_info, page);
        return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    return refbuf;
}


ogg_codec_t *initial_speex_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;
    SpeexHeader *header;

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    ogg_stream_packetout (&codec->os, &packet);

    /* Check for te first packet to be at least of the minimal size for a Speex header.
     * The header size is 80 bytes as per specs. You can find the specs here:
     * https://speex.org/docs/manual/speex-manual/node8.html#SECTION00830000000000000000
     *
     * speex_packet_to_header() will also check the header size for us. However
     * that function generates noise on stderr in case the header is too short.
     * This is dangerous as we may have closed stderr already and the handle may be use
     * again for something else.
     */
    if (packet.bytes < 80) {
        ICECAST_LOG_DDEBUG("Header too small for Speex, so skipping Speex test.");
        ogg_stream_clear (&codec->os);
        free (codec);
        return NULL;
    }

    ICECAST_LOG_DEBUG("checking for speex codec");
    header = speex_packet_to_header ((char*)packet.packet, packet.bytes);
    if (header == NULL)
    {
        ogg_stream_clear (&codec->os);
        free (header);
        free (codec);
        return NULL;
    }
    ICECAST_LOG_INFO("seen initial speex header");
    codec->process_page = process_speex_page;
    codec->codec_free = speex_codec_free;
    codec->headers = 1;
    format_ogg_attach_header (ogg_info, page);
    free (header);
    return codec;
}
