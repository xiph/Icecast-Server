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

/* format_ogg.c
**
** format plugin for Ogg
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif
#ifdef HAVE_SPEEX
#include <speex_header.h>
#endif

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"

#define CATMODULE "format-ogg"
#include "logging.h"

struct _ogg_state_tag;

/* per codec/logical structure */
typedef struct _ogg_codec_tag
{
    ogg_stream_state os;
    unsigned headers;
    void *specific;
    struct _ogg_state_tag *feed;
    struct _ogg_codec_tag *next;
    refbuf_t *(*process_page)(struct _ogg_codec_tag *codec, ogg_page *page);
    void (*codec_free)(struct _ogg_codec_tag *codec);
    refbuf_t        *possible_start;
} ogg_codec_t;


typedef struct _ogg_state_tag
{
    char *mount;
    ogg_sync_state oy;

    ogg_codec_t *codecs;
    char *artist;
    char *title;
    int send_yp_info;
    refbuf_t *file_headers;
    refbuf_t *header_pages;
    refbuf_t *header_pages_tail;
    int headers_completed;
    long bitrate;
    ogg_codec_t *codec_sync;
} ogg_state_t;


struct client_vorbis
{
    refbuf_t *headers;
    refbuf_t *header_page;
    unsigned pos;
    int headers_sent;
};


void refbuf_page_prerelease (struct source_tag *source, refbuf_t *refbuf)
{
    ogg_state_t *ogg_info = source->format->_state;

    /* only theora will be marking refbufs as sync that are behind in the
     * queue, here we just make sure that it isn't the one we are going to
     * remove. */
    if (ogg_info->codec_sync)
    {
        if (ogg_info->codec_sync->possible_start == refbuf)
            ogg_info->codec_sync->possible_start = NULL;
    }
}


static refbuf_t *make_refbuf_with_page (ogg_page *page)
{
    refbuf_t *refbuf = refbuf_new (page->header_len + page->body_len);

    memcpy (refbuf->data, page->header, page->header_len);
    memcpy (refbuf->data+page->header_len, page->body, page->body_len);
    return refbuf;
}


void format_ogg_attach_header (ogg_state_t *ogg_info, ogg_page *page)
{
    refbuf_t *refbuf = make_refbuf_with_page (page);

    if (ogg_info->header_pages_tail)
        ogg_info->header_pages_tail->next = refbuf;
    ogg_info->header_pages_tail = refbuf;

    if (ogg_info->header_pages == NULL)
        ogg_info->header_pages = refbuf;
}



/**** vorbis ****/
typedef struct _vorbis_codec_tag
{
    vorbis_info vi;
    vorbis_comment vc;
} vorbis_codec_t;


static void vorbis_codec_free (ogg_codec_t *codec)
{
    vorbis_codec_t *vorbis = codec->specific;

    codec->feed->artist = NULL;
    codec->feed->title = NULL;
    vorbis_info_clear (&vorbis->vi);
    vorbis_comment_clear (&vorbis->vc);
    ogg_stream_clear (&codec->os);
    free (vorbis);
    free (codec);
}

static refbuf_t *process_vorbis_page (ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t *refbuf;

    if (ogg_page_granulepos (page) == 0)
    {
        vorbis_codec_t *vorbis = codec->specific;
        ogg_packet packet;

        ogg_stream_pagein (&codec->os, page);
        while (ogg_stream_packetout (&codec->os, &packet) > 0)
        {
           if (vorbis_synthesis_headerin (&vorbis->vi, &vorbis->vc, &packet) < 0)
           {
               /* set some error code */
               return NULL;
           }
           codec->headers++;
        }
        /* add header page to associated list */
        format_ogg_attach_header (codec->feed, page);
        if (codec->headers == 3)
        {
            ogg_state_t *ogg_info = codec->feed;

            ogg_info->send_yp_info = 1;
            ogg_info->title  = vorbis_comment_query (&vorbis->vc, "TITLE", 0);
            ogg_info->artist = vorbis_comment_query (&vorbis->vc, "ARTIST", 0);
            ogg_info->bitrate += vorbis->vi.bitrate_nominal;
            stats_event_args (codec->feed->mount, "audio-samplerate", "%ld", (long)vorbis->vi.rate);
            stats_event_args (codec->feed->mount, "audio-channels", "%ld", (long)vorbis->vi.channels);
            stats_event_args (codec->feed->mount, "audio-bitrate", "%ld", (long)vorbis->vi.bitrate_nominal);
        }
        return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    if (codec->feed->codec_sync == NULL)
        refbuf->sync_point = 1;
    return refbuf;
}


static ogg_codec_t *initial_vorbis_page (ogg_page *page)
{
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    vorbis_codec_t *vorbis_codec = calloc (1, sizeof (vorbis_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    vorbis_info_init (&vorbis_codec->vi);
    vorbis_comment_init (&vorbis_codec->vc);

    ogg_stream_packetout (&codec->os, &packet);

    if (vorbis_synthesis_headerin (&vorbis_codec->vi, &vorbis_codec->vc, &packet) < 0)
    {
        ogg_stream_clear (&codec->os);
        vorbis_info_clear (&vorbis_codec->vi);
        vorbis_comment_clear (&vorbis_codec->vc);
        free (vorbis_codec);
        free (codec);
        return NULL;
    }
    INFO0 ("seen initial vorbis header");
    codec->specific = vorbis_codec;
    codec->process_page = process_vorbis_page;
    codec->codec_free = vorbis_codec_free;
    codec->headers = 1;
    return codec;
}


/* Writ codec handler */
static void writ_codec_free (ogg_codec_t *codec)
{
    ogg_stream_clear (&codec->os);
    free (codec);
}


static refbuf_t *process_writ_page (ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t *refbuf;

    if (codec->headers)
    {
        ogg_packet pkt;

        ogg_stream_pagein (&codec->os, page);
        while (ogg_stream_packetout (&codec->os, &pkt) > 0)
        {
            if (pkt.bytes >= 5 && pkt.packet[0] != (unsigned char)'\xff' &&
                        memcmp (pkt.packet+1, "writ", 4) == 0)
            {
                codec->headers++;
                continue;
            }
            codec->headers = 0;
        }
        if (codec->headers)
            return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    /* allow clients to start here if nothing prevents it */
    if (codec->feed->codec_sync == NULL)
        refbuf->sync_point = 1;
    return refbuf;
}


static ogg_codec_t *initial_writ_page (ogg_page *page)
{
    ogg_packet packet;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    ogg_stream_packetout (&codec->os, &packet);
    if (memcmp (packet.packet, "\x00writ", 5) == 0)
    {

        INFO0 ("seen initial writ header");
        codec->process_page = process_writ_page;
        codec->codec_free = writ_codec_free;
        codec->headers = 1;
        return codec;
    }
    ogg_stream_clear (&codec->os);
    free (codec);

    return NULL;
}


#ifdef HAVE_SPEEX

static void speex_codec_free (ogg_codec_t *codec)
{
    ogg_stream_clear (&codec->os);
    free (codec);
}


static refbuf_t *process_speex_page (ogg_codec_t *codec, ogg_page *page)
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
        format_ogg_attach_header (codec->feed, page);
        return NULL;
    }
    refbuf = make_refbuf_with_page (page);
    if (codec->feed->codec_sync == NULL)
        refbuf->sync_point = 1;
    return refbuf;
}


static ogg_codec_t *initial_speex_page (ogg_page *page)
{
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;
    SpeexHeader *header;

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    ogg_stream_packetout (&codec->os, &packet);

    header = speex_packet_to_header (packet.packet, packet.bytes);
    if (header == NULL)
    {
        ogg_stream_clear (&codec->os);
        free (header);
        free (codec);
        return NULL;
    }
    INFO0 ("seen initial speex header");
    codec->process_page = process_speex_page;
    codec->codec_free = speex_codec_free;
    codec->headers = 1;
    free (header);
    return codec;
}
#endif

#ifdef HAVE_THEORA

typedef struct _theora_codec_tag
{
    theora_info     ti;
    theora_comment  tc;
    int             granule_shift;
    ogg_int64_t     last_iframe;
    ogg_int64_t     prev_granulepos;
} theora_codec_t;


static void theora_codec_free (ogg_codec_t *codec)
{
    theora_codec_t *theora = codec->specific;

    theora_info_clear (&theora->ti);
    theora_comment_clear (&theora->tc);
    ogg_stream_clear (&codec->os);
    free (theora);
    free (codec);
}


static int _ilog (unsigned int v)
{ 
  int ret=0;
  while(v){
    ret++;
    v>>=1;
  }
  return ret;
}


static refbuf_t *process_theora_page (ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t *refbuf;
    theora_codec_t *theora = codec->specific;
    ogg_int64_t granulepos;

    granulepos = ogg_page_granulepos (page);
    // printf ("   granulepos of page %ld is %lld\n", ogg_page_pageno (page), granulepos);
    if (granulepos == 0)
    {
        ogg_packet packet;
        ogg_state_t *ogg_info = codec->feed;

        ogg_stream_pagein (&codec->os, page);
        // printf ("page %ld processing\n", ogg_page_pageno (page));
        while (ogg_stream_packetout (&codec->os, &packet) > 0)
        {
           if (theora_decode_header (&theora->ti, &theora->tc, &packet) < 0)
           {
               /* set some error code */
               WARN0 ("problem with theora header");
               return NULL;
           }
           codec->headers++;
           // printf ("header packets: %d\n", codec->headers);
           if (codec->headers == 3)
           {
               theora->granule_shift = _ilog (theora->ti.keyframe_frequency_force - 1);
               DEBUG1 ("granule shift is %lu", theora->granule_shift);
               theora->last_iframe = (ogg_int64_t)-1;
               codec->possible_start = NULL;
               ogg_info->bitrate += theora->ti.target_bitrate;
               stats_event_args (codec->feed->mount, "video_bitrate",
                       "%ld", (long)theora->ti.target_bitrate);
               stats_event_args (codec->feed->mount, "frame_size",
                       "%ld x %ld", (long)theora->ti.frame_width, (long)theora->ti.frame_height);
               stats_event_args (codec->feed->mount, "framerate",
                       "%.2f", (float)theora->ti.fps_numerator/theora->ti.fps_denominator);
           }
        }
        /* add page to associated list */
        format_ogg_attach_header (ogg_info, page);

        return NULL;
    }
    refbuf = make_refbuf_with_page (page);

    // DEBUG2 ("granulepos is %lld,  %p", granulepos, refbuf);
    if (granulepos == -1 || granulepos == theora->prev_granulepos)
    {
        if (codec->possible_start == NULL)
        {
            // DEBUG1 ("granulepos is unset, possible beginning %p", refbuf);
            codec->possible_start = refbuf;
        }
        // DEBUG1 ("possible start is %p", codec->possible_start);
    }
    else
    {
        if ((granulepos >> theora->granule_shift) != theora->last_iframe)
        {
            theora->last_iframe = (granulepos >> theora->granule_shift);
            // DEBUG2 ("iframe changed to %lld (%p)", theora->last_iframe, refbuf);
            if (codec->possible_start == NULL)
                codec->possible_start = refbuf;
            codec->possible_start->sync_point = 1;
            // DEBUG1 ("possible start is %p", codec->possible_start);
        }
        else
        {
            if (theora->prev_granulepos != -1)
                codec->possible_start = refbuf;
        }
    }
    theora->prev_granulepos = granulepos;

    return refbuf;
}


static ogg_codec_t *initial_theora_page (ogg_page *page)
{
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    theora_codec_t *theora_codec = calloc (1, sizeof (theora_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    theora_info_init (&theora_codec->ti);
    theora_comment_init (&theora_codec->tc);

    ogg_stream_packetout (&codec->os, &packet);

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
    // printf ("initial page %ld processing\n", ogg_page_pageno (page));
    codec->specific = theora_codec;
    codec->process_page = process_theora_page;
    codec->codec_free = theora_codec_free;
    codec->headers = 1;
    return codec;
}
#endif /* THEORA */

static void format_ogg_free_plugin (format_plugin_t *plugin);
static int  create_ogg_client_data(source_t *source, client_t *client);
static void free_ogg_client_data (client_t *client);

static void write_ogg_to_file (struct source_tag *source, refbuf_t *refbuf);
static refbuf_t *ogg_get_buffer (source_t *source);
static int write_buf_to_client (format_plugin_t *self, client_t *client);


static void free_ogg_codecs (ogg_state_t *ogg_info)
{
    ogg_codec_t *codec;
    refbuf_t *header;

    if (ogg_info == NULL)
        return;
    /* release the header pages first */
    header = ogg_info->header_pages;
    while (header)
    {
        refbuf_t *to_release = header;
        header = header->next;
        refbuf_release (to_release);
    }
    ogg_info->header_pages = NULL;
    ogg_info->header_pages_tail = NULL;

    /* now free the codecs */
    codec = ogg_info->codecs;
    while (codec)
    {
        ogg_codec_t *next = codec->next;
        codec->codec_free (codec);
        codec = next;
    }
    ogg_info->codecs = NULL;
    ogg_info->headers_completed = 0;
}


int format_ogg_get_plugin (source_t *source)
{
    format_plugin_t *plugin;
    ogg_state_t *state = calloc (1, sizeof (ogg_state_t));

    plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_OGG;
    plugin->format_description = "Ogg Vorbis";
    plugin->get_buffer = ogg_get_buffer;
    plugin->write_buf_to_client = write_buf_to_client;
    plugin->write_buf_to_file = write_ogg_to_file;
    plugin->create_client_data = create_ogg_client_data;
    plugin->free_plugin = format_ogg_free_plugin;
    plugin->set_tag = NULL;
    plugin->prerelease = refbuf_page_prerelease;

    ogg_sync_init (&state->oy);

    plugin->_state = state;
    source->format = plugin;
    state->mount = source->mount;

    return 0;
}


void format_ogg_free_plugin (format_plugin_t *plugin)
{
    ogg_state_t *state = plugin->_state;

    /* free memory associated with this plugin instance */
    free_ogg_codecs (state);

    /* free state memory */
    ogg_sync_clear (&state->oy);
    free (state);

    /* free the plugin instance */
    free (plugin);
}



static int process_initial_page (ogg_state_t *ogg_info, ogg_page *page)
{
    ogg_codec_t *codec;

    if (ogg_info->headers_completed)
    {
        ogg_info->bitrate = 0;
        ogg_info->codec_sync = NULL;
        /* need to zap old list of codecs when next group of BOS pages appear */
        free_ogg_codecs (ogg_info);
    }
    do
    {
        codec = initial_vorbis_page (page);
        if (codec)
            break;
        codec = initial_writ_page (page);
        if (codec)
            break;
#ifdef HAVE_THEORA
        codec = initial_theora_page (page);
        if (codec)
        {
            ogg_info->codec_sync = codec;
            break;
        }
#endif
#ifdef HAVE_SPEEX
        codec = initial_speex_page (page);
        if (codec)
            break;
#endif

        /* any others */
        INFO0 ("Seen BOS page with unknown type");
        return -1;
    } while (0);

    if (codec)
    {
        /* add codec to list */
        codec->next = ogg_info->codecs;
        ogg_info->codecs = codec;

        codec->feed = ogg_info;
        format_ogg_attach_header (ogg_info, page);
        // printf ("initial header page stored at %p\n", ogg_info->header_pages);
    }

    return 0;
}


static refbuf_t *process_ogg_page (ogg_state_t *ogg_info, ogg_page *page)
{
    ogg_codec_t *codec = ogg_info->codecs;

    while (codec)
    {
        if (ogg_page_serialno (page) == codec->os.serialno)
        {
            refbuf_t *refbuf = codec->process_page (codec, page);
            if (refbuf)
            {
                refbuf_t *header = ogg_info->header_pages;
                while (header)
                {
                    refbuf_addref (header);
                    header = header->next;
                }
                refbuf->associated = ogg_info->header_pages;
            }
            return refbuf;
        }
        codec = codec->next;
    }
    return NULL;
}


static refbuf_t *ogg_get_buffer (source_t *source)
{
    ogg_state_t *ogg_info = source->format->_state;
    char *data = NULL;
    int bytes;
    ogg_page page;
    refbuf_t *refbuf = NULL;

    while (1)
    {
        while (1)
        {
            if (ogg_sync_pageout (&ogg_info->oy, &page) > 0)
            {
                if (ogg_page_bos (&page))
                {
                    process_initial_page (ogg_info, &page);
                    continue;
                }
                // printf ("finished with BOS pages\n");
                ogg_info->headers_completed = 1;
                /* process the extracted page */
                refbuf = process_ogg_page (ogg_info, &page);

                if (ogg_info->send_yp_info)
                {
                    char *tag;
                    tag = ogg_info->title;
                    if (tag == NULL)
                        tag = "unknown";
                    stats_event (source->mount, "title", tag);
                    INFO1("Updating title \"%s\"", tag);

                    tag = ogg_info->artist;
                    if (tag == NULL)
                        tag = "unknown";
                    stats_event (source->mount, "artist", tag);
                    if (ogg_info->bitrate)
                        stats_event_args (source->mount, "ice-bitrate", "%u", ogg_info->bitrate/1000);

                    INFO1("Updating artist \"%s\"", tag);
                    ogg_info->send_yp_info = 0;
                    yp_touch (source->mount);
                }
                if (refbuf)
                    return refbuf;
                continue;
            }
            break;
        }
        /* we need more data to continue getting pages */
        data = ogg_sync_buffer (&ogg_info->oy, 4096);

        bytes = sock_read_bytes (source->con->sock, data, 4096);
        if (bytes < 0)
        {
            if (sock_recoverable (sock_error()))
                return NULL;
            WARN0 ("source connection has died");
            ogg_sync_wrote (&ogg_info->oy, 0);
            source->running = 0;
            return NULL;
        }
        if (bytes == 0)
        {
            INFO1 ("End of Stream %s", source->mount);
            ogg_sync_wrote (&ogg_info->oy, 0);
            source->running = 0;
            return NULL;
        }
        ogg_sync_wrote (&ogg_info->oy, bytes);
    }
}


static int create_ogg_client_data (source_t *source, client_t *client) 
{
    struct client_vorbis *client_data = calloc (1, sizeof (struct client_vorbis));
    int ret = -1;

    if (client_data)
    {
        client_data->headers_sent = 1;
        client->format_data = client_data;
        client->free_client_data = free_ogg_client_data;
        ret = 0;
    }
    return ret;
}


static void free_ogg_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}



static int send_ogg_headers (client_t *client, refbuf_t *headers)
{
    struct client_vorbis *client_data = client->format_data;
    refbuf_t *refbuf;
    int written = 0;

    if (client_data->headers_sent)
    {
        client_data->header_page = headers;
        client_data->pos = 0;
        client_data->headers_sent = 0;
    }
    refbuf = client_data->header_page;
    while (refbuf)
    {
        char *data = refbuf->data + client_data->pos;
        unsigned len = refbuf->len - client_data->pos;
        int ret;

        ret = client_send_bytes (client, data, len);
        if (ret > 0)
           written += ret;
        if (ret < (int)len)
            return written ? written : -1;
        client_data->pos += ret;
        if (client_data->pos == refbuf->len)
        {
            refbuf = refbuf->next;
            client_data->header_page = refbuf;
            client_data->pos = 0;
        }
    }
    client_data->headers_sent = 1;
    client_data->headers = headers;
    return written;
}


static int write_buf_to_client (format_plugin_t *self, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    char *buf;
    unsigned len;
    struct client_vorbis *client_data = client->format_data;
    int ret, written = 0;

    /* rare but the listener could connect before audio is ready */
    if (refbuf == NULL)
        return 0;
    if (refbuf->next == NULL && client->pos == refbuf->len)
        return 0;

    if (refbuf->next && client->pos == refbuf->len)
    {
        client->refbuf = refbuf->next;
        client->pos = 0;
    }
    refbuf = client->refbuf;
    buf = refbuf->data + client->pos;
    len = refbuf->len - client->pos;
    do
    {
        if (client_data->headers != refbuf->associated)
        {
            ret = send_ogg_headers (client, refbuf->associated);
            if (client_data->headers_sent == 0)
                break;
            written += ret;
        }
        ret = client_send_bytes (client, buf, len);

        if (ret > 0)
            client->pos += ret;

        if (ret < (int)len)
            break;
        written += ret;
        /* we have now written the page(s) */
        ret = 0;
    } while (0);

    if (ret > 0)
       written += ret;
    return written;
}


static int write_ogg_data (struct source_tag *source, refbuf_t *refbuf)
{
    int ret = 1;

    if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) != refbuf->len)
    {
        WARN0 ("Write to dump file failed, disabling");
        fclose (source->dumpfile);
        source->dumpfile = NULL;
        ret = 0;
    }
    return ret;
}


static void write_ogg_to_file (struct source_tag *source, refbuf_t *refbuf)
{
    ogg_state_t *ogg_info = source->format->_state;

    if (ogg_info->file_headers != refbuf->associated)
    {
        refbuf_t *header = refbuf->associated;
        while (header)
        {
            if (write_ogg_data (source, header) == 0)
                return;
            header = header->next;
        }
        ogg_info->file_headers = refbuf->associated;
    }
    write_ogg_data (source, refbuf);
}


