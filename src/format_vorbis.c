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


/* Ogg codec handler for vorbis streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <memory.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "format_ogg.h"
#include "stats.h"
#include "format.h"

#define CATMODULE "format-vorbis"
#include "logging.h"


typedef struct vorbis_codec_tag
{
    vorbis_info vi;
    vorbis_comment vc;

    int rebuild_comment;
    int stream_notify;

    ogg_stream_state new_os;
    int page_samples_trigger;
    ogg_int64_t prev_granulepos;
    ogg_packet *prev_packet;
    ogg_int64_t granulepos;
    ogg_int64_t samples_in_page;
    int prev_window;
    int initial_audio_packet;

    ogg_packet *header [3];
    ogg_int64_t prev_page_samples;

    int (*process_packet)(ogg_state_t *ogg_info, ogg_codec_t *codec);
    refbuf_t *(*get_buffer_page)(ogg_state_t *ogg_info, ogg_codec_t *codec);

} vorbis_codec_t;

static int process_vorbis_headers (ogg_state_t *ogg_info, ogg_codec_t *codec);
static refbuf_t *process_vorbis_page (ogg_state_t *ogg_info,
                ogg_codec_t *codec, ogg_page *page);
static refbuf_t *process_vorbis (ogg_state_t *ogg_info, ogg_codec_t *codec);
static void vorbis_set_tag (format_plugin_t *plugin, char *tag, char *value);
static refbuf_t *vorbis_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page);


static void free_ogg_packet (ogg_packet *packet)
{
    if (packet)
    {
        free (packet->packet);
        free (packet);
    }
}


static void vorbis_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *vorbis = codec->specific;

    DEBUG0 ("freeing vorbis codec");
    stats_event (ogg_info->mount, "audio-bitrate", NULL);
    stats_event (ogg_info->mount, "audio-channels", NULL);
    stats_event (ogg_info->mount, "audio-samplerate", NULL);
    vorbis_info_clear (&vorbis->vi);
    vorbis_comment_clear (&vorbis->vc);
    ogg_stream_clear (&codec->os);
    ogg_stream_clear (&vorbis->new_os);
    free_ogg_packet (vorbis->header[0]);
    free_ogg_packet (vorbis->header[1]);
    free_ogg_packet (vorbis->header[2]);
    free_ogg_packet (vorbis->prev_packet);
    free (vorbis);
    free (codec);
}


static ogg_packet *copy_ogg_packet (ogg_packet *packet)
{
    ogg_packet *next;
    do
    {
        next = malloc (sizeof (ogg_packet));
        if (next == NULL)
            break;
        memcpy (next, packet, sizeof (ogg_packet));
        next->packet = malloc (next->bytes);
        if (next->packet == NULL)
            break;
        memcpy (next->packet, packet->packet, next->bytes);
        return next;
    } while (0);

    if (next)
        free (next);
    return NULL;
}


static void add_audio_packet (vorbis_codec_t *source_vorbis, ogg_packet *packet)
{
    if (source_vorbis->initial_audio_packet)
    {
        packet->granulepos = 0;
        source_vorbis->initial_audio_packet = 0;
    }
    else
    {
        source_vorbis->samples_in_page +=
            (packet->granulepos - source_vorbis->prev_granulepos);
        source_vorbis->prev_granulepos = packet->granulepos;
        source_vorbis->granulepos += source_vorbis->prev_window;
    }
    ogg_stream_packetin (&source_vorbis->new_os, packet);
}


static refbuf_t *get_buffer_audio (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    refbuf_t *refbuf = NULL;
    ogg_page page;
    vorbis_codec_t *source_vorbis = codec->specific;
    int (*get_ogg_page)(ogg_stream_state*, ogg_page *) = ogg_stream_pageout;

    if (source_vorbis->samples_in_page > source_vorbis->page_samples_trigger)
    {
        get_ogg_page = ogg_stream_flush;
    }
    if (get_ogg_page (&source_vorbis->new_os, &page) > 0)
    {
        /* printf ("got audio page %lld\n", ogg_page_granulepos (&page)); */
        /* squeeze a page copy into a buffer */
        source_vorbis->samples_in_page -= (ogg_page_granulepos (&page) - source_vorbis->prev_page_samples);
        source_vorbis->prev_page_samples = ogg_page_granulepos (&page);

        refbuf = make_refbuf_with_page (&page);
    }
    return refbuf;
}


static refbuf_t *get_buffer_header (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    int headers_flushed = 0;
    ogg_page page;
    vorbis_codec_t *source_vorbis = codec->specific;

    while (ogg_stream_flush (&source_vorbis->new_os, &page) > 0)
    {
        format_ogg_attach_header (ogg_info, &page);
        headers_flushed = 1;
    }
    if (headers_flushed)
    {
        /* printf ("headers have now been handled\n"); */
        source_vorbis->get_buffer_page = get_buffer_audio;
    }
    return NULL;
}


static refbuf_t *get_buffer_finished (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;
    ogg_page page;
    refbuf_t *refbuf;

    if (ogg_stream_flush (&source_vorbis->new_os, &page) > 0)
    {
        /* printf ("EOS stream flush %lld\n", ogg_page_granulepos (&page)); */

        source_vorbis->samples_in_page -= (ogg_page_granulepos (&page) - source_vorbis->prev_page_samples);
        source_vorbis->prev_page_samples = ogg_page_granulepos (&page);

        refbuf = make_refbuf_with_page (&page);
        DEBUG0 ("flushing page");
        return refbuf;
    }
    ogg_stream_clear (&source_vorbis->new_os);
    ogg_stream_init (&source_vorbis->new_os, rand());

    refbuf = ogg_info->header_pages;
    while (refbuf)
    {
        refbuf_t *to_go = refbuf;
        refbuf = refbuf->next;
        refbuf_release (to_go);
    }
    ogg_info->header_pages = NULL;
    ogg_info->header_pages_tail = NULL;
    source_vorbis->get_buffer_page = NULL;
    source_vorbis->process_packet = process_vorbis_headers;
    return NULL;
}


/* push last packet into stream marked with eos */
static void initiate_flush (vorbis_codec_t *source_vorbis)
{
    DEBUG0 ("adding EOS packet");
    if (source_vorbis->prev_packet)
    {
        /* insert prev_packet with eos */
        source_vorbis->prev_packet->e_o_s = 1;
        add_audio_packet (source_vorbis, source_vorbis->prev_packet);
        source_vorbis->prev_packet->e_o_s = 0;
    }
    source_vorbis->get_buffer_page = get_buffer_finished;
    source_vorbis->initial_audio_packet = 1;
}



static int process_vorbis_audio (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;
    int result = 0;

    while (1)
    {
        int window;
        ogg_packet packet;

        /* now, lets extract what packets we can */
        if (ogg_stream_packetout (&codec->os, &packet) <= 0)
            return result;

        /* calculate granulepos for the packet */
        window = vorbis_packet_blocksize (&source_vorbis->vi, &packet) / 4;

        source_vorbis->granulepos += window;
        if (source_vorbis->prev_packet)
        {
            ogg_packet *prev_packet = source_vorbis->prev_packet;
            if (packet.b_o_s)
                prev_packet->e_o_s = 1;
            add_audio_packet (source_vorbis, prev_packet);
            free_ogg_packet (prev_packet);
            packet . granulepos = source_vorbis->granulepos;
        }
        else
        {
            packet . granulepos = 0;
        }
        source_vorbis->prev_window = window;

        /* copy the next packet */
        source_vorbis->prev_packet = copy_ogg_packet (&packet);

        if (source_vorbis->stream_notify)
        {
            initiate_flush (source_vorbis);
            source_vorbis->stream_notify = 0;
        }

        /* allow for pages to be flushed if there's over a certain number of samples */
        if (source_vorbis->samples_in_page > source_vorbis->page_samples_trigger)
            return 1;
    }
}


/* handle the headers we want going to the clients */
static int process_vorbis_headers (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;

    if (source_vorbis->header [0] == NULL)
        return 0;

    DEBUG0 ("Adding the 3 header packets");
    ogg_stream_packetin (&source_vorbis->new_os, source_vorbis->header [0]);
    /* NOTE: we could build a separate comment packet each time */
    if (source_vorbis->rebuild_comment)
    {
        vorbis_comment vc;
        ogg_packet header;

        vorbis_comment_init (&vc);
        if (ogg_info->artist) 
            vorbis_comment_add_tag (&vc, "artist", ogg_info->artist);
        if (ogg_info->title)
            vorbis_comment_add_tag (&vc, "title", ogg_info->title);
        vorbis_comment_add (&vc, "server=" ICECAST_VERSION_STRING);
        vorbis_commentheader_out (&vc, &header);

        ogg_stream_packetin (&source_vorbis->new_os, &header);
        vorbis_comment_clear (&vc);
        ogg_packet_clear (&header);
    }
    else
        ogg_stream_packetin (&source_vorbis->new_os, source_vorbis->header [1]);
    ogg_stream_packetin (&source_vorbis->new_os, source_vorbis->header [2]);
    source_vorbis->rebuild_comment = 0;

    ogg_info->log_metadata = 1;
    source_vorbis->get_buffer_page = get_buffer_header;
    source_vorbis->process_packet = process_vorbis_audio;
    source_vorbis->granulepos = 0;
    source_vorbis->initial_audio_packet = 1;
    return 1;
}


/* this is called with the first page after the initial header */
/* it processes any headers that have come in on the stream */
static int process_vorbis_incoming_hdrs (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    ogg_packet header;
    vorbis_codec_t *source_vorbis = codec->specific;

    DEBUG1 ("processing incoming header packet (%d)", codec->headers);
    while (codec->headers < 3)
    {
        /* now, lets extract the packets */
        int result = ogg_stream_packetout (&codec->os, &header);

        if (result <= 0)
            return -1;   /* need more pages */

        /* change comments here if need be */
        if (vorbis_synthesis_headerin (&source_vorbis->vi, &source_vorbis->vc, &header) < 0)
        {
            ogg_info->error = 1;
            WARN0 ("Problem parsing ogg vorbis header");
            return -1;
        }
        header.granulepos = 0;
        source_vorbis->header [codec->headers] = copy_ogg_packet (&header);
        codec->headers++;
    }
    DEBUG0 ("we have the header packets now");

    /* we have all headers */

    stats_event_args (ogg_info->mount, "audio-samplerate", "%ld", (long)source_vorbis->vi.rate);
    stats_event_args (ogg_info->mount, "audio-channels", "%ld", (long)source_vorbis->vi.channels);
    stats_event_args (ogg_info->mount, "audio-bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal);
    stats_event_args (ogg_info->mount, "ice-bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal/1000);

    /* set queued pages to contain a 1/4 of a second worth of samples */
    source_vorbis->page_samples_trigger = source_vorbis->vi.rate / 4;

    /* printf ("finished with incoming header packets\n"); */
    source_vorbis->process_packet = process_vorbis_headers;

    return 1;
}


ogg_codec_t *initial_vorbis_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    vorbis_codec_t *source_vorbis = calloc (1, sizeof (vorbis_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    vorbis_info_init (&source_vorbis->vi);
    vorbis_comment_init (&source_vorbis->vc);

    ogg_stream_packetout (&codec->os, &packet);

    DEBUG0("checking for vorbis codec");
    if (vorbis_synthesis_headerin (&source_vorbis->vi, &source_vorbis->vc, &packet) < 0)
    {
        ogg_stream_clear (&codec->os);
        vorbis_info_clear (&source_vorbis->vi);
        vorbis_comment_clear (&source_vorbis->vc);
        free (source_vorbis);
        free (codec);
        return NULL;
    }
    INFO0 ("seen initial vorbis header");
    codec->specific = source_vorbis;
    codec->codec_free = vorbis_codec_free;
    codec->headers = 1;

    /* */
    if (ogg_info->rebuild)
    {
        free_ogg_packet (source_vorbis->header[0]);
        free_ogg_packet (source_vorbis->header[1]);
        free_ogg_packet (source_vorbis->header[2]);
        memset (source_vorbis->header, 0, sizeof (source_vorbis->header));
        source_vorbis->header [0] = copy_ogg_packet (&packet);
        ogg_stream_init (&source_vorbis->new_os, rand());

        codec->process_page = process_vorbis_page;
        codec->process = process_vorbis;
        plugin->set_tag = vorbis_set_tag;
        source_vorbis->process_packet = process_vorbis_incoming_hdrs;
    }
    else
    {
        codec->process_page = vorbis_page;
        format_ogg_attach_header (ogg_info, page);
    }
    
    return codec;
}


static void vorbis_set_tag (format_plugin_t *plugin, char *tag, char *value)
{   
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = ogg_info->codecs;
    vorbis_codec_t *source_vorbis;
    int change = 0;

    /* avoid updating if multiple codecs in use */
    if (codec && codec->next == NULL)
        source_vorbis = codec->specific;
    else
        return;

    if (strcmp (tag, "artist") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (ogg_info->artist);
            ogg_info->artist = p;
            change = 1;
        }
    }
    if (strcmp (tag, "title") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (ogg_info->title);
            ogg_info->title = p;
            change = 1;
        }
    }
    if (strcmp (tag, "song") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (ogg_info->artist);
            free (ogg_info->title);
            ogg_info->title = p;
            change = 1;
        }
    }
    if (change)
    {
        source_vorbis->stream_notify = 1;
        source_vorbis->rebuild_comment = 1;
    }
}


static refbuf_t *process_vorbis (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;
    refbuf_t *refbuf;

    while (1)
    {
        if (source_vorbis->get_buffer_page)
        {
            refbuf = source_vorbis->get_buffer_page (ogg_info, codec);
            if (refbuf)
                return refbuf;
        }

        if (source_vorbis->process_packet &&
                source_vorbis->process_packet (ogg_info, codec) > 0)
            continue;
        return NULL;
    }
}


static refbuf_t *process_vorbis_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page)
{
    if (ogg_stream_pagein (&codec->os, page) < 0)
        ogg_info->error = 1;
    return NULL;
}


static refbuf_t *vorbis_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page)
{
    if (codec->headers < 3)
    {
        vorbis_codec_t *vorbis = codec->specific;
        ogg_packet packet;

        ogg_stream_pagein (&codec->os, page);
        while (ogg_stream_packetout (&codec->os, &packet) > 0)
        {
            if (vorbis_synthesis_headerin (&vorbis->vi, &vorbis->vc, &packet) < 0)
            {
                ogg_info->error = 1;
                WARN0 ("error processing vorbis header packet");
                return NULL;
            }
            codec->headers++;
        }
        /* add header page to associated list */
        format_ogg_attach_header (ogg_info, page);
        DEBUG1 ("header page processed, headers at %d", codec->headers);
        if (codec->headers == 3)
        {
            char *comment;
            free (ogg_info->title);
            comment = vorbis_comment_query (&vorbis->vc, "TITLE", 0);
            if (comment)
                ogg_info->title = strdup (comment);
            else
                ogg_info->title = NULL;

            free (ogg_info->artist);
            comment = vorbis_comment_query (&vorbis->vc, "ARTIST", 0);
            if (comment)
                ogg_info->artist = strdup (comment);
            else
                ogg_info->artist = NULL;

            ogg_info->bitrate += vorbis->vi.bitrate_nominal;
            stats_event_args (ogg_info->mount, "audio-samplerate", "%ld", (long)vorbis->vi.rate);
            stats_event_args (ogg_info->mount, "audio-channels", "%ld", (long)vorbis->vi.channels);
            stats_event_args (ogg_info->mount, "audio-bitrate", "%ld", (long)vorbis->vi.bitrate_nominal);
            ogg_info->log_metadata = 1;
        }
        return NULL;
    }
    return make_refbuf_with_page (page);
}



