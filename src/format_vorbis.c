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

/* format_vorbis.c
**
** format plugin for vorbis
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

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"

#define CATMODULE "format-vorbis"
#include "logging.h"

static ogg_int64_t next_rebuild_serialno = 0;
static mutex_t serial_lock;

typedef struct _vstate_tag
{
    ogg_sync_state oy;
    ogg_stream_state os, out_os;
    vorbis_info vi;
    vorbis_comment vc;

    ogg_packet *prev_packet;
    refbuf_t *file_headers;

    int initial_audio_packet;
    int stream_notify;
    int use_url_comment;
    int to_terminate;
    int more_headers;
    int prev_window;
    int page_samples_trigger;
    ogg_int64_t granulepos;
    ogg_int64_t samples_in_page;
    ogg_int64_t prev_samples;
    ogg_int64_t prev_page_samples;

    refbuf_t *headers_head;
    refbuf_t *headers_tail;
    ogg_packet *header [3];
    ogg_packet url_comment;
    char *url_artist;
    char *url_title;

    int (*process_packet)(source_t *);
    refbuf_t *(*get_buffer_page)(struct _vstate_tag *source_vorbis);

} vstate_t;

struct client_vorbis
{
    refbuf_t *headers;
    refbuf_t *header_page;
    unsigned pos;
    int headers_sent;
};


static ogg_int64_t get_next_serialno ()
{
    ogg_int64_t serialno;
    thread_mutex_lock (&serial_lock);
    serialno = next_rebuild_serialno++;
    thread_mutex_unlock (&serial_lock);
    return serialno;
}

static void format_vorbis_free_plugin (format_plugin_t *plugin);
static int  create_vorbis_client_data(source_t *source, client_t *client);
static void free_vorbis_client_data (client_t *client);

static void write_vorbis_to_file (struct source_tag *source, refbuf_t *refbuf);
static refbuf_t *vorbis_get_buffer (source_t *source);
static int vorbis_write_buf_to_client (source_t *source, client_t *client);
static void vorbis_set_tag (format_plugin_t *plugin, char *tag, char *value);


static void free_ogg_packet (ogg_packet *packet)
{
    if (packet)
    {
        free (packet->packet);
        free (packet);
    }
}


int format_ogg_get_plugin (source_t *source)
{
    format_plugin_t *plugin;
    vstate_t *state;
    vorbis_comment vc;

    plugin = (format_plugin_t *)calloc(1, sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_OGG;
    plugin->get_buffer = vorbis_get_buffer;
    plugin->write_buf_to_client = vorbis_write_buf_to_client;
    plugin->write_buf_to_file = write_vorbis_to_file;
    plugin->create_client_data = create_vorbis_client_data;
    plugin->free_plugin = format_vorbis_free_plugin;
    plugin->set_tag = vorbis_set_tag;
    plugin->contenttype = "application/ogg";

    state = (vstate_t *)calloc(1, sizeof(vstate_t));
    ogg_sync_init(&state->oy);
    ogg_stream_init (&state->out_os, get_next_serialno());

    vorbis_comment_init (&vc);
    vorbis_commentheader_out (&vc, &state->url_comment);
    vorbis_comment_clear (&vc);

    plugin->_state = (void *)state;
    source->format = plugin;

    return 0;
}

void format_vorbis_free_plugin (format_plugin_t *plugin)
{
    vstate_t *state = plugin->_state;

    /* free memory associated with this plugin instance */

    /* free state memory */
    ogg_sync_clear (&state->oy);
    ogg_stream_clear (&state->os);
    ogg_stream_clear (&state->out_os);
    vorbis_comment_clear (&state->vc);
    vorbis_info_clear (&state->vi);

    free_ogg_packet (state->header[0]);
    free_ogg_packet (state->header[1]);
    free_ogg_packet (state->header[2]);
    if (state->prev_packet)
        free_ogg_packet (state->prev_packet);

    while (state->headers_head)
    {
        refbuf_t *to_go = state->headers_head;
        state->headers_head = to_go->next;
        /* printf ("releasing vorbis header %p\n", to_go); */
        refbuf_release (to_go);
    }

    free (state->url_artist);
    free (state->url_title);
    ogg_packet_clear (&state->url_comment);

    free (state);

    /* free the plugin instance */
    free (plugin);
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


static void add_audio_packet (vstate_t *source_vorbis, ogg_packet *packet)
{
    if (source_vorbis->initial_audio_packet)
    {
        packet->granulepos = 0;
        source_vorbis->initial_audio_packet = 0;
    }
    else
    {
        source_vorbis->samples_in_page += (packet->granulepos - source_vorbis->prev_samples);
        source_vorbis->prev_samples = packet->granulepos;
        source_vorbis->granulepos += source_vorbis->prev_window;
    }
    /* printf ("Adding packet %lld, granulepos %lld (%ld)\n", packet->packetno,
            packet->granulepos, vorbis_packet_blocksize (&source_vorbis->vi, packet)/4); */
    ogg_stream_packetin (&source_vorbis->out_os, packet);
}


static refbuf_t *get_buffer_audio (vstate_t *source_vorbis)
{
    refbuf_t *refbuf = NULL;
    ogg_page page;
    int (*get_ogg_page)(ogg_stream_state*, ogg_page *) = ogg_stream_pageout;

    /* printf ("current sample count is %lld, %ld\n", source_vorbis->samples_in_page, source_vorbis->vi.rate>>1); */
    if (source_vorbis->samples_in_page > source_vorbis->page_samples_trigger)
    {
        get_ogg_page = ogg_stream_flush;
        /* printf ("forcing flush with %lld samples\n", source_vorbis->samples_in_page); */
    }
    if (get_ogg_page (&source_vorbis->out_os, &page) > 0)
    {
        /* printf ("got audio page %lld\n", ogg_page_granulepos (&page)); */
        /* squeeze a page copy into a buffer */
        source_vorbis->samples_in_page -= (ogg_page_granulepos (&page) - source_vorbis->prev_page_samples);
        source_vorbis->prev_page_samples = ogg_page_granulepos (&page);

        refbuf = refbuf_new (page.header_len + page.body_len);
        memcpy (refbuf->data, page.header, page.header_len);
        memcpy (refbuf->data+page.header_len, page.body, page.body_len);
        /* printf ("setting associated to %p\n", refbuf->associated); */
    }
    return refbuf;
}


static refbuf_t *get_buffer_header (vstate_t *source_vorbis)
{
    int headers_flushed = 0;
    ogg_page page;

    /* printf ("in buffer_header\n"); */
    while (ogg_stream_flush (&source_vorbis->out_os, &page) > 0)
    {
        refbuf_t *refbuf;
        /* squeeze a page copy into a buffer */
        refbuf = refbuf_new (page.header_len + page.body_len);
        memcpy (refbuf->data, page.header, page.header_len);
        memcpy (refbuf->data+page.header_len, page.body, page.body_len);
        refbuf->len = page.header_len + page.body_len;

        /* store header page for associated list */
        if (source_vorbis->headers_tail)
            source_vorbis->headers_tail->next = refbuf;
        if (source_vorbis->headers_head == NULL)
            source_vorbis->headers_head = refbuf;
        source_vorbis->headers_tail = refbuf;
        /* printf ("Stored vorbis header %p\n", refbuf); */
        headers_flushed = 1;
    }
    if (headers_flushed)
    {
        /* printf ("headers have now been handled\n"); */
        source_vorbis->get_buffer_page = get_buffer_audio;
    }
    return NULL;
}


static refbuf_t *get_buffer_finished (vstate_t *source_vorbis)
{
    ogg_page page;
    refbuf_t *refbuf;

    if (ogg_stream_flush (&source_vorbis->out_os, &page) > 0)
    {
        /* printf ("EOS stream flush %lld\n", ogg_page_granulepos (&page)); */

        source_vorbis->samples_in_page -= (ogg_page_granulepos (&page) - source_vorbis->prev_page_samples);
        source_vorbis->prev_page_samples = ogg_page_granulepos (&page);

        refbuf = refbuf_new (page.header_len + page.body_len);
        memcpy (refbuf->data, page.header, page.header_len);
        memcpy (refbuf->data+page.header_len, page.body, page.body_len);
        refbuf->len = page.header_len + page.body_len;
        refbuf->associated = source_vorbis->headers_head;
        return refbuf;
    }
    ogg_stream_clear (&source_vorbis->out_os);
    ogg_stream_init (&source_vorbis->out_os, get_next_serialno());
    refbuf = source_vorbis->headers_head;
    while (refbuf)
    {
        refbuf_t *to_go = refbuf;
        refbuf = refbuf->next;
        /* printf ("releasing vorbis header %p\n", to_go); */
        refbuf_release (to_go);
    }
    source_vorbis->headers_head = NULL;
    source_vorbis->headers_tail = NULL;
    source_vorbis->get_buffer_page = get_buffer_header;
    return NULL;
}


/* pushed last packet into stream marked with eos */
static void initiate_flush (vstate_t *source_vorbis)
{
    if (source_vorbis->prev_packet)
    {
        /* insert prev_packet with eos */
        source_vorbis->prev_packet->e_o_s = 1;
        /* printf ("adding stored packet marked as EOS\n"); */
        add_audio_packet (source_vorbis, source_vorbis->prev_packet);
        source_vorbis->prev_packet->e_o_s = 0;
    }
    source_vorbis->get_buffer_page = get_buffer_finished;
    source_vorbis->initial_audio_packet = 1;
}

/* just deal with ogg vorbis streams at the moment */

static int process_vorbis_audio (source_t *source)
{
    vstate_t *source_vorbis = source->format->_state;
    int result = 0;

    while (1)
    {
        int window;
        ogg_packet packet;

        /* now, lets extract what packets we can */
        if (ogg_stream_packetout (&source_vorbis->os, &packet) <= 0)
            return result;
        
        result = 1;

        /* calculate granulepos for the packet */
        window = vorbis_packet_blocksize (&source_vorbis->vi, &packet) / 4;
        
        source_vorbis->granulepos += window;
        if (source_vorbis->prev_packet)
        {
            ogg_packet *prev_packet = source_vorbis->prev_packet;
            if (packet.b_o_s)
                prev_packet->e_o_s = 1;
            add_audio_packet (source_vorbis, prev_packet);
            /* printf ("Adding prev packet %lld, granulepos %lld (%d) samples %lld\n", prev_packet->packetno,
                    prev_packet->granulepos, source_vorbis->prev_window, source_vorbis->samples_in_page); */
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

        /* allow for pages to be flushed if there's over a certain number of samples */
        if (source_vorbis->samples_in_page > source_vorbis->page_samples_trigger)
            return 1;
    }
}

/* handle the headers we want going to the clients */
static int process_vorbis_headers (source_t *source)
{
    vstate_t *source_vorbis = source->format->_state;

    /* trap for missing initial header, this means we're expecting
       headers coming in, so jump out and try in a short while */
    if (source_vorbis->header [0] == NULL)
        return 0;
    /* printf ("Adding the 3 header packets\n"); */
    ogg_stream_packetin (&source_vorbis->out_os, source_vorbis->header [0]);
    /* NOTE: we could build a separate comment packet each time */
    if (source_vorbis->use_url_comment)
        ogg_stream_packetin (&source_vorbis->out_os, &source_vorbis->url_comment);
    else
        ogg_stream_packetin (&source_vorbis->out_os, source_vorbis->header [1]);
    ogg_stream_packetin (&source_vorbis->out_os, source_vorbis->header [2]);
    source_vorbis->use_url_comment = 0;

    source_vorbis->process_packet = process_vorbis_audio;
    source_vorbis->granulepos = 0;
    source_vorbis->initial_audio_packet = 1;
    return 1;
}

static void update_stats (source_t *source, vorbis_comment *vc)
{
    char *artist;
    char *title;
    char *metadata = NULL;
    unsigned int len = 1;

    /* put known comments in the stats, this could be site specific */
    title = vorbis_comment_query (vc, "TITLE", 0);
    if (title)
    {
        INFO1 ("title set to \"%s\"", title);
        len += strlen (title);
    }
    stats_event (source->mount, "title", title);

    artist = vorbis_comment_query (vc, "ARTIST", 0);
    if (artist)
    {
        INFO1 ("artist set to \"%s\"", artist);
        len += strlen (artist);
    }
    stats_event (source->mount, "artist", artist);
    if (artist)
    {
        if (title)
        {
            len += strlen(artist) + strlen(title) + 3;
            metadata = calloc (1, len);
            snprintf (metadata, len, "%s - %s", artist, title);
        }
        else
        {
            len += strlen(artist);
            metadata = calloc (1, len);
            snprintf (metadata, len, "%s", artist);
        }
    }
    else
    {
        if (title)
        {
            len += strlen (title);
            metadata = calloc (1, len);
            snprintf (metadata, len, "%s", title);
        }
    }
    if (metadata)
    {
        logging_playlist (source->mount, metadata, source->listeners);
        free (metadata);
    }
}


/* this is called with the first page after the initial header */
/* it processes any headers that have come in on the stream */
static int process_vorbis_incoming_hdrs (source_t *source)
{
    ogg_packet header;
    vstate_t *source_vorbis = source->format->_state;

    /* printf ("processing incoming header packet\n"); */
    while (source_vorbis->more_headers)
    {
        /* now, lets extract the packets */
        int result = ogg_stream_packetout (&source_vorbis->os, &header);
        
        if (result <= 0)
            return result;   /* need more pages */

        /* change comments here if need be */
        if (vorbis_synthesis_headerin (&source_vorbis->vi, &source_vorbis->vc, &header) < 0)
        {
            WARN0 ("Problem parsing ogg vorbis header");
            return -1;
        }
        header.granulepos = 0;
        /* printf ("Parsing [%d] vorbis header %lld,  %lld\n", source_vorbis->more_headers, header.packetno, header.granulepos); */
        source_vorbis->header [3-source_vorbis->more_headers] = copy_ogg_packet (&header);
        source_vorbis->more_headers--;
    }

    /* we have all headers */

    update_stats (source, &source_vorbis->vc);

    stats_event_args (source->mount, "audio-samplerate", "%ld", (long)source_vorbis->vi.rate);
    stats_event_args (source->mount, "audio-channels", "%ld", (long)source_vorbis->vi.channels);
    stats_event_args (source->mount, "audio-bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal);
    stats_event_args (source->mount, "ice-bitrate", "%ld", (long)source_vorbis->vi.bitrate_nominal/1000);
    /* set queued pages to contain a 1/4 of a second worth of samples */
    source_vorbis->page_samples_trigger = source_vorbis->vi.rate / 4;

    /* printf ("finished with incoming header packets\n"); */
    source_vorbis->process_packet = process_vorbis_headers;

    return 1;
}



static int initial_vorbis_page (vstate_t *source_vorbis, ogg_packet *packet)
{
    /* init vi and vc */
    vorbis_comment_clear (&source_vorbis->vc);
    vorbis_info_clear (&source_vorbis->vi);

    vorbis_info_init (&source_vorbis->vi);
    vorbis_comment_init (&source_vorbis->vc);

    /* printf ("processing initial page\n"); */
    if (vorbis_synthesis_headerin (&source_vorbis->vi, &source_vorbis->vc, packet) < 0)
    {
        /* printf ("not a vorbis packet\n"); */
        return -1;
    }

    /* printf ("Handling ogg vorbis header\n"); */
    free_ogg_packet (source_vorbis->header[0]);
    free_ogg_packet (source_vorbis->header[1]);
    free_ogg_packet (source_vorbis->header[2]);
    memset (source_vorbis->header, 0, sizeof (source_vorbis->header));
    source_vorbis->header [0] = copy_ogg_packet (packet);
    source_vorbis->more_headers = 2;

    initiate_flush (source_vorbis);
    source_vorbis->process_packet = process_vorbis_incoming_hdrs;
    /* free previous audio packet, it maybe in a different format */
    free_ogg_packet (source_vorbis->prev_packet);
    source_vorbis->prev_packet = NULL;
    source_vorbis->prev_window = 0;

    source_vorbis->initial_audio_packet = 1;

    return 0;
}


static int process_initial_page (source_t *source, ogg_page *page)
{
    vstate_t *source_vorbis = source->format->_state;
    int ret = -1;
    ogg_packet packet;

    ogg_stream_clear (&source_vorbis->os);
    ogg_stream_init (&source_vorbis->os, ogg_page_serialno (page));

    ogg_stream_pagein (&source_vorbis->os, page);
    do
    {
        if (ogg_stream_packetout (&source_vorbis->os, &packet) <= 0)
            break;
        ret = 0;
        if (initial_vorbis_page (source_vorbis, &packet) == 0)
            break;
        /* any others */
        ret = -1;
    } while (0);
    /* printf ("processed initial page\n"); */
    return ret;
}


static void vorbis_set_tag (format_plugin_t *plugin, char *tag, char *value)
{
    vstate_t *source_vorbis = plugin->_state;
    int change = 0;
    if (strcmp (tag, "artist") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (source_vorbis->url_artist);
            source_vorbis->url_artist = p;
            change = 1;
        }
    }
    if (strcmp (tag, "title") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (source_vorbis->url_title);
            source_vorbis->url_title = p;
            change = 1;
        }
    }
    if (strcmp (tag, "song") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (source_vorbis->url_artist);
            free (source_vorbis->url_title);
            source_vorbis->url_title = p;
            change = 1;
        }
    }
    if (change)
        source_vorbis->stream_notify = 1;
}


static void update_comments (source_t *source)
{
    vstate_t *source_vorbis = source->format->_state;
    vorbis_comment vc;
    ogg_packet header;

    initiate_flush (source_vorbis);

    /* printf ("updated comment header\n"); */
    vorbis_comment_init (&vc);
    if (source_vorbis->url_artist)
        vorbis_comment_add_tag (&vc, "artist", source_vorbis->url_artist);
    if (source_vorbis->url_title)
        vorbis_comment_add_tag (&vc, "title", source_vorbis->url_title);
    vorbis_comment_add (&vc, "server=" ICECAST_VERSION_STRING);
    ogg_packet_clear (&source_vorbis->url_comment);
    update_stats (source, &vc);
    vorbis_commentheader_out (&vc, &source_vorbis->url_comment);
    vorbis_comment_clear (&vc);
    header.packetno = 1;
    source_vorbis->use_url_comment = 1;
    source_vorbis->process_packet = process_vorbis_headers;
}

static refbuf_t *vorbis_get_buffer (source_t *source)
{
    vstate_t *source_vorbis = source->format->_state;
    char *data = NULL;
    int bytes = 1;
    ogg_page page;
    refbuf_t *refbuf = NULL;

    while (1)
    {
        while (1)
        {
            if (source_vorbis->get_buffer_page)
                refbuf = source_vorbis->get_buffer_page (source_vorbis);
            if (refbuf)
            {
                refbuf_t *header = source_vorbis->headers_head;
                refbuf->associated = source_vorbis->headers_head;
                while (header)
                {
                    refbuf_addref (header);
                    header = header->next;
                }
                refbuf->sync_point = 1;
                return refbuf;
            }
            
            /* printf ("check for processed packets\n"); */
            if (source_vorbis->process_packet && source_vorbis->process_packet (source) > 0)
                continue;
            /* printf ("Checking for more in-pages\n"); */
            if (ogg_sync_pageout (&source_vorbis->oy, &page) > 0)
            {
                /* lets see what we do with it */
                if (ogg_page_bos (&page))
                {
                    process_initial_page (source, &page);
                    return NULL;
                }
                /* printf ("Adding in page to out_os\n"); */
                ogg_stream_pagein (&source_vorbis->os, &page);
                continue;
            }
            break;
        }
        if (source_vorbis->to_terminate)
        {
            /* normal exit path */
            source->running = 0;
            source_vorbis->to_terminate = 0;
            return NULL;
        }
        /* see if any non-stream updates are requested */
        if (source_vorbis->stream_notify)
        {
            update_comments (source);
            source_vorbis->stream_notify = 0;
            continue;
        }
        if (data == NULL)
            data = ogg_sync_buffer (&source_vorbis->oy, 4096);
        /* printf ("reading data in\n"); */
        bytes = sock_read_bytes (source->con->sock, data, 4096);
        if (bytes < 0)
        {
            if (sock_recoverable (sock_error()))
                return NULL;
            WARN0 ("source connection has died");
            ogg_sync_wrote (&source_vorbis->oy, 0);
            source_vorbis->to_terminate = 1;
            initiate_flush (source_vorbis);
            return NULL;
        }
        if (bytes == 0)
        {
            INFO1 ("End of Stream %s", source->mount);
            ogg_sync_wrote (&source_vorbis->oy, 0);
            source_vorbis->to_terminate = 1;
            initiate_flush (source_vorbis);
            return NULL;
        }
        ogg_sync_wrote (&source_vorbis->oy, bytes);
        data = NULL;
    }
}


static int create_vorbis_client_data (source_t *source, client_t *client) 
{
    struct client_vorbis *client_data = calloc (1, sizeof (struct client_vorbis));
    if (client_data == NULL)
    {
        ERROR0("malloc failed");
        return -1;
    }
    client_data->headers_sent = 1;
    client->format_data = client_data;
    client->free_client_data = free_vorbis_client_data;
    return 0;
}

static void free_vorbis_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}


static int send_vorbis_headers (client_t *client, refbuf_t *headers)
{
    struct client_vorbis *client_data = client->format_data;
    refbuf_t *refbuf;
    int written = 0;

    if (client_data->headers_sent)
    {
        /* printf ("setting client_data header to %p\n", headers); */
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

        /* printf ("....sending header at %p\n", refbuf); */
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


static int vorbis_write_buf_to_client (source_t *source, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    char *buf;
    unsigned len;
    struct client_vorbis *client_data = client->format_data;
    int ret, written = 0;

    /* rare but the listener could connect before audio is ready */
    if (refbuf == NULL)
        return 0;
    /* printf ("client %p (%p) @ %lu\n", refbuf, refbuf->next,  client->pos); */
    if (refbuf->next == NULL && client->pos == refbuf->len)
        return 0;

    if (refbuf->next && client->pos == refbuf->len)
    {
        client_set_queue (client, refbuf->next);
        refbuf = client->refbuf;
    }
    refbuf = client->refbuf;
    buf = refbuf->data + client->pos;
    len = refbuf->len - client->pos;
    do
    {
        if (client_data->headers != refbuf->associated)
        {
            /* printf ("sending header data %p\n", refbuf->associated); */
            ret = send_vorbis_headers (client, refbuf->associated);
            if (client_data->headers_sent == 0)
                break;
            written += ret;
        }
        /* printf ("sending audio data\n"); */
        ret = client_send_bytes (client, buf, len);

        if (ret > 0)
            client->pos += ret;

        if (ret < (int)len)
            break;
        written += ret;
        /* we have now written the header page(s) */
        ret = 0;
    } while (0);

    if (ret > 0)
       written += ret;
    return written ? written : -1;
}


static int write_vorbis_data (struct source_tag *source, refbuf_t *refbuf)
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


static void write_vorbis_to_file (struct source_tag *source, refbuf_t *refbuf)
{
    vstate_t *source_vorbis = source->format->_state;

    if (source_vorbis->file_headers != refbuf->associated)
    {
        refbuf_t *header = refbuf->associated;
        while (header)
        {
            if (write_vorbis_data (source, header) == 0)
                return;
            header = header->next;
        }
        source_vorbis->file_headers = refbuf->associated;
    }
    write_vorbis_data (source, refbuf);
}


void format_ogg_initialise (void)
{
    next_rebuild_serialno = 1;
    thread_mutex_create ("serial", &serial_lock);
}

