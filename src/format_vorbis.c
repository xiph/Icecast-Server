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
#include <string.h>

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
    int initial_audio_page;

    ogg_stream_state new_os;
    int page_samples_trigger;
    ogg_int64_t prev_granulepos;
    ogg_packet *prev_packet;
    ogg_int64_t granulepos;
    ogg_int64_t initial_page_granulepos;
    ogg_int64_t samples_in_page;
    int prev_window;
    int initial_audio_packet;

    ogg_page bos_page;
    ogg_packet *header [3];
    ogg_int64_t prev_page_samples;

    int (*process_packet)(ogg_state_t *ogg_info, ogg_codec_t *codec);
    refbuf_t *(*get_buffer_page)(ogg_state_t *ogg_info, ogg_codec_t *codec);

} vorbis_codec_t;

static int process_vorbis_headers (ogg_state_t *ogg_info, ogg_codec_t *codec);
static refbuf_t *process_vorbis_page (ogg_state_t *ogg_info,
                ogg_codec_t *codec, ogg_page *page);
static refbuf_t *process_vorbis (ogg_state_t *ogg_info, ogg_codec_t *codec);
static void vorbis_set_tag (format_plugin_t *plugin, const char *tag, const char *value, const char *charset);


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

    ICECAST_LOG_DEBUG("freeing vorbis codec");
    stats_event (ogg_info->mount, "audio_bitrate", NULL);
    stats_event (ogg_info->mount, "audio_channels", NULL);
    stats_event (ogg_info->mount, "audio_samplerate", NULL);
    vorbis_info_clear (&vorbis->vi);
    vorbis_comment_clear (&vorbis->vc);
    ogg_stream_clear (&codec->os);
    ogg_stream_clear (&vorbis->new_os);
    free_ogg_packet (vorbis->header[0]);
    free_ogg_packet (vorbis->header[1]);
    free_ogg_packet (vorbis->header[2]);
    free_ogg_packet (vorbis->prev_packet);
    free (vorbis->bos_page.header);
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
        get_ogg_page = ogg_stream_flush;

    if (get_ogg_page (&source_vorbis->new_os, &page) > 0)
    {
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
        source_vorbis->samples_in_page -= (ogg_page_granulepos (&page) - source_vorbis->prev_page_samples);
        source_vorbis->prev_page_samples = ogg_page_granulepos (&page);

        refbuf = make_refbuf_with_page (&page);
        ICECAST_LOG_DEBUG("flushing page");
        return refbuf;
    }
    ogg_stream_clear (&source_vorbis->new_os);
    ogg_stream_init (&source_vorbis->new_os, rand());

    format_ogg_free_headers (ogg_info);
    source_vorbis->get_buffer_page = NULL;
    if (source_vorbis->prev_packet)
        source_vorbis->process_packet = process_vorbis_headers;
    else
        source_vorbis->process_packet = NULL;

    if (source_vorbis->initial_audio_packet == 0)
        source_vorbis->prev_window = 0;

    return NULL;
}


/* push last packet into stream marked with eos */
static void initiate_flush (vorbis_codec_t *source_vorbis)
{
    if (source_vorbis->prev_packet)
    {
        /* insert prev_packet with eos */
        ICECAST_LOG_DEBUG("adding EOS packet");
        source_vorbis->prev_packet->e_o_s = 1;
        add_audio_packet (source_vorbis, source_vorbis->prev_packet);
        source_vorbis->prev_packet->e_o_s = 0;
    }
    source_vorbis->get_buffer_page = get_buffer_finished;
    source_vorbis->initial_audio_packet = 1;
}


/* process the vorbis audio packets. Here we just take each packet out 
 * and add them into the new stream, flushing after so many samples. We
 * also check if an new headers are requested after each processed page
 */
static int process_vorbis_audio (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;

    while (1)
    {
        int window;
        ogg_packet packet;

        /* now, lets extract what packets we can */
        if (ogg_stream_packetout (&codec->os, &packet) <= 0)
            break;

        /* calculate granulepos for the packet */
        window = vorbis_packet_blocksize (&source_vorbis->vi, &packet) / 4;

        source_vorbis->granulepos += window;
        if (source_vorbis->prev_packet)
        {
            ogg_packet *prev_packet = source_vorbis->prev_packet;

            add_audio_packet (source_vorbis, prev_packet);
            free_ogg_packet (prev_packet);

            /* check for short values on first initial page */
            if (packet . packetno == 4)
            {
                if (source_vorbis->initial_page_granulepos < source_vorbis->granulepos)
                {
                    source_vorbis->granulepos -= source_vorbis->initial_page_granulepos;
                    source_vorbis->samples_in_page = source_vorbis->page_samples_trigger;
                }
            }
            /* check for long values on first page */
            if (packet.granulepos == source_vorbis->initial_page_granulepos)
            {
                if (source_vorbis->initial_page_granulepos > source_vorbis->granulepos)
                    source_vorbis->granulepos = source_vorbis->initial_page_granulepos;
            }

            if (packet.e_o_s == 0)
                packet . granulepos = source_vorbis->granulepos;
        }
        else
        {
            packet . granulepos = 0;
        }

        /* store the current packet details */
        source_vorbis->prev_window = window;
        source_vorbis->prev_packet = copy_ogg_packet (&packet);
        if (packet.e_o_s)
        {
            initiate_flush (source_vorbis);
            free_ogg_packet (source_vorbis->prev_packet);
            source_vorbis->prev_packet = NULL;
            return 1;
        }

        /* allow for pages to be flushed if there's over a certain number of samples */
        if (source_vorbis->samples_in_page > source_vorbis->page_samples_trigger)
            return 1;
    }
    if (source_vorbis->stream_notify)
    {
        initiate_flush (source_vorbis);
        source_vorbis->stream_notify = 0;
        return 1;
    }
    return -1;
}


/* This handles the headers at the backend, here we insert the header packets
 * we want for the queue.
 */
static int process_vorbis_headers (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    vorbis_codec_t *source_vorbis = codec->specific;

    if (source_vorbis->header [0] == NULL)
        return 0;

    ICECAST_LOG_DEBUG("Adding the 3 header packets");
    ogg_stream_packetin (&source_vorbis->new_os, source_vorbis->header [0]);
    /* NOTE: we could build a separate comment packet each time */
    if (source_vorbis->rebuild_comment)
    {
        vorbis_comment vc;
        ogg_packet header;
        ice_config_t *config;

        vorbis_comment_init (&vc);
        if (ogg_info->artist) 
            vorbis_comment_add_tag (&vc, "artist", ogg_info->artist);
        if (ogg_info->title)
            vorbis_comment_add_tag (&vc, "title", ogg_info->title);
        config = config_get_config();
        vorbis_comment_add_tag (&vc, "server", config->server_id);
        config_release_config();
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
    source_vorbis->granulepos = source_vorbis->prev_window;
    source_vorbis->initial_audio_packet = 1;
    return 1;
}


/* check if the provided BOS page is the start of a vorbis stream. If so
 * then setup a structure so it can be used
 */
ogg_codec_t *initial_vorbis_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    vorbis_codec_t *vorbis = calloc (1, sizeof (vorbis_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    vorbis_info_init (&vorbis->vi);
    vorbis_comment_init (&vorbis->vc);

    ogg_stream_packetout (&codec->os, &packet);

    ICECAST_LOG_DEBUG("checking for vorbis codec");
    if (vorbis_synthesis_headerin (&vorbis->vi, &vorbis->vc, &packet) < 0)
    {
        ogg_stream_clear (&codec->os);
        vorbis_info_clear (&vorbis->vi);
        vorbis_comment_clear (&vorbis->vc);
        free (vorbis);
        free (codec);
        return NULL;
    }
    ICECAST_LOG_INFO("seen initial vorbis header");
    codec->specific = vorbis;
    codec->codec_free = vorbis_codec_free;
    codec->headers = 1;
    codec->name = "Vorbis";

    free_ogg_packet (vorbis->header[0]);
    free_ogg_packet (vorbis->header[1]);
    free_ogg_packet (vorbis->header[2]);
    memset (vorbis->header, 0, sizeof (vorbis->header));
    vorbis->header [0] = copy_ogg_packet (&packet);
    ogg_stream_init (&vorbis->new_os, rand());

    codec->process_page = process_vorbis_page;
    codec->process = process_vorbis;
    plugin->set_tag = vorbis_set_tag;

    vorbis->bos_page.header = malloc (page->header_len + page->body_len);
    
    memcpy (vorbis->bos_page.header, page->header, page->header_len);
    vorbis->bos_page.header_len = page->header_len;

    vorbis->bos_page.body = vorbis->bos_page.header + page->header_len;
    memcpy (vorbis->bos_page.body, page->body, page->body_len);
    vorbis->bos_page.body_len = page->body_len;

    return codec;
}


/* called from the admin interface, here we update the artist/title info
 * and schedule a new set of header pages
 */
static void vorbis_set_tag (format_plugin_t *plugin, const char *tag, const char *in_value, const char *charset)
{   
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = ogg_info->codecs;
    vorbis_codec_t *source_vorbis;
    char *value;

    /* avoid updating if multiple codecs in use */
    if (codec && codec->next == NULL)
        source_vorbis = codec->specific;
    else
        return;

    if (tag == NULL)
    {
        source_vorbis->stream_notify = 1;
        source_vorbis->rebuild_comment = 1;
        return;
    }

    value = util_conv_string (in_value, charset, "UTF-8");
    if (value == NULL)
        value = strdup (in_value);

    if (strcmp (tag, "artist") == 0)
    {
        free (ogg_info->artist);
        ogg_info->artist = value;
    }
    else if (strcmp (tag, "title") == 0)
    {
        free (ogg_info->title);
        ogg_info->title = value;
    }
    else if (strcmp (tag, "song") == 0)
    {
        free (ogg_info->title);
        ogg_info->title = value;
    }
    else
        free (value);
}


/* main backend routine when rebuilding streams. Here we loop until we either
 * have a refbuf to add onto the queue, or we want more data to process.
 */
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


/* no processing of pages, just wrap them up in a refbuf and pass
 * back for adding to the queue
 */
static refbuf_t *process_vorbis_passthru_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page)
{
    return make_refbuf_with_page (page);
}


/* handle incoming page. as the stream is being rebuilt, we need to
 * add all pages from the stream before processing packets
 */
static refbuf_t *process_vorbis_page (ogg_state_t *ogg_info,
        ogg_codec_t *codec, ogg_page *page)
{
    ogg_packet header;
    vorbis_codec_t *source_vorbis = codec->specific;
    char *comment;

    if (ogg_stream_pagein (&codec->os, page) < 0)
    {
        ogg_info->error = 1;
        return NULL;
    }
    if (codec->headers == 3)
    {
        if (source_vorbis->initial_audio_page)
        {
            source_vorbis->initial_page_granulepos = ogg_page_granulepos (page);
            source_vorbis->initial_audio_page = 0;
        }
        return NULL;
    }

    while (codec->headers < 3)
    {
        /* now, lets extract the packets */
        ICECAST_LOG_DEBUG("processing incoming header packet (%d)", codec->headers);

        if (ogg_stream_packetout (&codec->os, &header) <= 0)
        {
            if (ogg_info->codecs->next)
                format_ogg_attach_header (ogg_info, page);
            return NULL;
        }

        /* change comments here if need be */
        if (vorbis_synthesis_headerin (&source_vorbis->vi, &source_vorbis->vc, &header) < 0)
        {
            ogg_info->error = 1;
            ICECAST_LOG_WARN("Problem parsing ogg vorbis header");
            return NULL;
        }
        header.granulepos = 0;
        source_vorbis->header [codec->headers] = copy_ogg_packet (&header);
        codec->headers++;
    }
    ICECAST_LOG_DEBUG("we have the header packets now");

    /* if vorbis is the only codec then allow rebuilding of the streams */
    if (ogg_info->codecs->next == NULL)
    {
        /* set queued vorbis pages to contain about 1/2 of a second worth of samples */
        source_vorbis->page_samples_trigger = source_vorbis->vi.rate / 2;
        source_vorbis->process_packet = process_vorbis_headers;
        source_vorbis->initial_audio_page = 1;
    }
    else
    {
        format_ogg_attach_header (ogg_info, &source_vorbis->bos_page);
        format_ogg_attach_header (ogg_info, page);
        codec->process_page = process_vorbis_passthru_page;
    }

    free (ogg_info->title);
    comment = vorbis_comment_query (&source_vorbis->vc, "TITLE", 0);
    if (comment)
        ogg_info->title = strdup (comment);
    else
        ogg_info->title = NULL;

    free (ogg_info->artist);
    comment = vorbis_comment_query (&source_vorbis->vc, "ARTIST", 0);
    if (comment)
        ogg_info->artist = strdup (comment);
    else
        ogg_info->artist = NULL;
    ogg_info->log_metadata = 1;

    stats_event_args (ogg_info->mount, "audio_samplerate", "%ld", (long)source_vorbis->vi.rate);
    stats_event_args (ogg_info->mount, "audio_channels", "%ld", (long)source_vorbis->vi.channels);
    stats_event_args (ogg_info->mount, "audio_bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal);
    stats_event_args (ogg_info->mount, "ice-bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal/1000);

    return NULL;
}

