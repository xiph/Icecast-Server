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

#define MAX_HEADER_PAGES 10

typedef struct _vstate_tag
{
    ogg_sync_state oy;
    ogg_stream_state os;
    vorbis_info vi;
    vorbis_comment vc;

    ogg_page og;
    unsigned long serialno;
    int header;
    refbuf_t *file_headers;
    refbuf_t *header_pages;
    refbuf_t *header_pages_tail;
    int packets;
} vstate_t;

struct client_vorbis
{
    refbuf_t *headers;
    refbuf_t *header_page;
    unsigned int pos;
    int processing_headers;
};


static void format_vorbis_free_plugin(format_plugin_t *self);
static refbuf_t *format_vorbis_get_buffer (source_t *source);
static int format_vorbis_create_client_data (source_t *source, client_t *client);
static void format_vorbis_send_headers(format_plugin_t *self,
        source_t *source, client_t *client);
static int write_buf_to_client (format_plugin_t *self, client_t *client);
static void write_ogg_to_file (struct source_tag *source, refbuf_t *refbuf);


int format_vorbis_get_plugin(source_t *source)
{
    format_plugin_t *plugin;
    vstate_t *state;

    plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_VORBIS;
    plugin->write_buf_to_file = write_ogg_to_file;
    plugin->get_buffer = format_vorbis_get_buffer;
    plugin->write_buf_to_client = write_buf_to_client;
    plugin->create_client_data = format_vorbis_create_client_data;
    plugin->client_send_headers = format_vorbis_send_headers;
    plugin->free_plugin = format_vorbis_free_plugin;
    plugin->format_description = "Ogg Vorbis";

    state = (vstate_t *)calloc(1, sizeof(vstate_t));
    ogg_sync_init(&state->oy);

    plugin->_state = (void *)state;
    source->format = plugin;

    return 0;
}

void format_vorbis_free_plugin(format_plugin_t *self)
{
    vstate_t *state = (vstate_t *)self->_state;
    refbuf_t *header = state->header_pages;

    /* free memory associated with this plugin instance */

    /* free state memory */
    while (header)
    {
        refbuf_t *to_release = header;
        header = header->next;
        refbuf_release (to_release);
    }
    ogg_sync_clear(&state->oy);
    ogg_stream_clear(&state->os);
    vorbis_comment_clear(&state->vc);
    vorbis_info_clear(&state->vi);
    
    free(state);

    /* free the plugin instance */
    free(self);
}

static refbuf_t *format_vorbis_get_buffer (source_t *source)
{
    int result;
    ogg_packet op;
    char *title_tag;
    char *artist_tag;
    char *metadata = NULL;
    int   metadata_len = 0;
    refbuf_t *refbuf, *header;
    char *data;
    format_plugin_t *self = source->format;
    int bytes;
    vstate_t *state = (vstate_t *)self->_state;

    data = ogg_sync_buffer (&state->oy, 4096);

    bytes = sock_read_bytes (source->con->sock, data, 4096);
    if (bytes < 0)
    {
        if (sock_recoverable (sock_error()))
            return NULL;
        WARN0 ("source connection has died");
        ogg_sync_wrote (&state->oy, 0);
        source->running = 0;
        return NULL;
    }
    if (bytes == 0)
    {
        INFO1 ("End of Stream %s", source->mount);
        ogg_sync_wrote (&state->oy, 0);
        source->running = 0;
        return NULL;
    }
    ogg_sync_wrote (&state->oy, bytes);

    refbuf = NULL;
    if (ogg_sync_pageout(&state->oy, &state->og) == 1) {
        refbuf = refbuf_new(state->og.header_len + state->og.body_len);
        memcpy(refbuf->data, state->og.header, state->og.header_len);
        memcpy(&refbuf->data[state->og.header_len], state->og.body, state->og.body_len);

        if (state->serialno != ogg_page_serialno(&state->og)) {
            DEBUG0("new stream");
            /* this is a new logical bitstream */
            state->header = 0;
            state->packets = 0;

            /* Clear old stuff. Rarely but occasionally needed. */
            header = state->header_pages;
            while (header)
            {
                refbuf_t *to_release = header;
                DEBUG0 ("clearing out header page");
                header = header->next;
                refbuf_release (to_release);
            }
            ogg_stream_clear(&state->os);
            vorbis_comment_clear(&state->vc);
            vorbis_info_clear(&state->vi);
            state->header_pages = NULL;
            state->header_pages_tail = NULL;

            state->serialno = ogg_page_serialno(&state->og);
            ogg_stream_init(&state->os, state->serialno);
            vorbis_info_init(&state->vi);
            vorbis_comment_init(&state->vc);
        }

        if (state->header >= 0) {
            /* FIXME: In some streams (non-vorbis ogg streams), this could get
             * extras pages beyond the header. We need to collect the pages
             * here anyway, but they may have to be discarded later.
             */
            DEBUG1 ("header %d", state->header);
            if (ogg_page_granulepos(&state->og) <= 0) {
                state->header++;
            } else {
                /* we're done caching headers */
                state->header = -1;

                DEBUG0 ("doing stats");
                /* put known comments in the stats */
                title_tag = vorbis_comment_query(&state->vc, "TITLE", 0);
                if (title_tag) stats_event(source->mount, "title", title_tag);
                else stats_event(source->mount, "title", "unknown");
                artist_tag = vorbis_comment_query(&state->vc, "ARTIST", 0);
                if (artist_tag) stats_event(source->mount, "artist", artist_tag);
                else stats_event(source->mount, "artist", "unknown");

                metadata = NULL;
                if (artist_tag) {
                    if (title_tag) {
                        metadata_len = strlen(artist_tag) + strlen(title_tag) +
                                       strlen(" - ") + 1;
                        metadata = (char *)calloc(1, metadata_len);
                        sprintf(metadata, "%s - %s", artist_tag, title_tag);
                    }
                    else {
                        metadata_len = strlen(artist_tag) + 1;
                        metadata = (char *)calloc(1, metadata_len);
                        sprintf(metadata, "%s", artist_tag);
                    }
                }
                else {
                    if (title_tag) {
                        metadata_len = strlen(title_tag) + 1;
                        metadata = (char *)calloc(1, metadata_len);
                        sprintf(metadata, "%s", title_tag);
                    }
                }
                if (metadata) {
                    logging_playlist(source->mount, metadata, source->listeners);
                    free(metadata);
                    metadata = NULL;
                }
                /* don't need these now */
                ogg_stream_clear(&state->os);
                vorbis_comment_clear(&state->vc);
                vorbis_info_clear(&state->vi);

                yp_touch (source->mount);
            }
        }

        /* cache header pages */
        if (state->header > 0 && state->packets < 3) {
            /* build a list of headers pages for attaching */
            if (state->header_pages_tail)
                state->header_pages_tail->next = refbuf;
            state->header_pages_tail = refbuf;

            if (state->header_pages == NULL)
                state->header_pages = refbuf;

            if (state->packets >= 0 && state->packets < 3) {
                ogg_stream_pagein(&state->os, &state->og);
                while (state->packets < 3) {
                    result = ogg_stream_packetout(&state->os, &op);
                    if (result == 0) break; /* need more data */
                    if (result < 0) {
                        state->packets = -1;
                        break;
                    }

                    state->packets++;

                    if (vorbis_synthesis_headerin(&state->vi, &state->vc, &op) < 0) {
                        state->packets = -1;
                        break;
                    }
                }
            }
            /* we do not place ogg headers on the main queue */
            return NULL;
        }
        /* increase ref counts on each header page going out */
        header = state->header_pages;
        while (header)
        {
            refbuf_addref (header);
            header = header->next;
        }
        refbuf->associated = state->header_pages;
    }

    return refbuf;
}

static void free_ogg_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}

static int format_vorbis_create_client_data (source_t *source, client_t *client)
{
    struct client_vorbis *client_data = calloc (1, sizeof (struct client_vorbis));
    int ret = -1;

    if (client_data)
    {
        client->format_data = client_data;
        client->free_client_data = free_ogg_client_data;
        ret = 0;
    }
    return ret;
}

static void format_vorbis_send_headers(format_plugin_t *self,
        source_t *source, client_t *client)
{
    int bytes;
    
    client->respcode = 200;
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\n" 
            "Content-Type: %s\r\n", 
            format_get_mimetype(source->format->type));

    if(bytes > 0) client->con->sent_bytes += bytes;

    format_send_general_headers(self, source, client);
}

static int send_ogg_headers (client_t *client, refbuf_t *headers)
{
    struct client_vorbis *client_data = client->format_data;
    refbuf_t *refbuf;
    int written = 0;

    if (client_data->processing_headers == 0)
    {
        client_data->header_page = headers;
        client_data->pos = 0;
        client_data->processing_headers = 1;
    }
    refbuf = client_data->header_page;
    while (refbuf)
    {
        char *data = refbuf->data + client_data->pos;
        unsigned int len = refbuf->len - client_data->pos;
        int ret;

        ret = client_send_bytes (client, data, len);
        if (ret > 0)
        {
           written += ret;
           client_data->pos += ret;
        }
        if (ret < (int)len)
            return written;
        if (client_data->pos == refbuf->len)
        {
            refbuf = refbuf->next;
            client_data->header_page = refbuf;
            client_data->pos = 0;
        }
    }
    /* update client info on headers sent */
    client_data->processing_headers = 0;
    client_data->headers = headers;
    return written;
}

static int write_buf_to_client (format_plugin_t *self, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    char *buf;
    unsigned int len;
    struct client_vorbis *client_data = client->format_data;
    int ret, written = 0;

    if (refbuf->next == NULL && client->pos == refbuf->len)
        return 0;

    if (refbuf->next && client->pos == refbuf->len)
    {
        client_set_queue (client, refbuf->next);
        refbuf = client->refbuf;
    }
    do
    {
        if (client_data->headers != refbuf->associated)
        {
            /* different headers seen so send the new ones */
            ret = send_ogg_headers (client, refbuf->associated);
            if (client_data->processing_headers)
                break;
            written += ret;
        }
        buf = refbuf->data + client->pos;
        len = refbuf->len - client->pos;
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
    vstate_t *state = (vstate_t *)source->format->_state;


    if (state->file_headers != refbuf->associated)
    {
        refbuf_t *header = refbuf->associated;
        while (header) 
        {
            if (write_ogg_data (source, header) == 0)
                return;
            header = header->next;
        }
        state->file_headers = refbuf->associated;
    }
    write_ogg_data (source, refbuf);
}

