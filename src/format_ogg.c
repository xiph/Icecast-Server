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
 *
 * format plugin for Ogg
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "format_ogg.h"
#include "format_vorbis.h"
#ifdef HAVE_THEORA
#include "format_theora.h"
#endif
#ifdef HAVE_SPEEX
#include "format_speex.h"
#endif
#include "format_midi.h"
#include "format_flac.h"
#include "format_kate.h"
#include "format_skeleton.h"

#define CATMODULE "format-ogg"
#include "logging.h"

struct _ogg_state_tag;

static void format_ogg_free_plugin (format_plugin_t *plugin, client_t *client);
static int  create_ogg_client_data(format_plugin_t *plugin, client_t *client);
static void free_ogg_client_data (client_t *client);

static int get_image (client_t *client, struct _format_plugin_tag *format);
static void write_ogg_to_file (struct source_tag *source, refbuf_t *refbuf);
static refbuf_t *ogg_get_buffer (source_t *source);
static int write_buf_to_client (client_t *client);
static void apply_ogg_settings (format_plugin_t *format, mount_proxy *mount);


struct ogg_client
{
    refbuf_t *headers;
    refbuf_t *header_page;
    unsigned pos;
    int headers_sent;
};


refbuf_t *make_refbuf_with_page (ogg_codec_t *codec, ogg_page *page)
{
    refbuf_t *refbuf;

    if (codec && codec->filtered)
        return NULL;
    refbuf = refbuf_new (page->header_len + page->body_len);

    memcpy (refbuf->data, page->header, page->header_len);
    memcpy (refbuf->data+page->header_len, page->body, page->body_len);
    return refbuf;
}


/* routine for taking the provided page (should be a header page) and
 * placing it on the collection of header pages
 */
void format_ogg_attach_header (ogg_codec_t *codec, ogg_page *page)
{
    ogg_state_t *ogg_info = codec->parent;
    refbuf_t *refbuf;
    
    if (codec->filtered)
        return;

    refbuf = make_refbuf_with_page (codec, page);

    if (ogg_page_bos (page))
    {
        DEBUG0 ("attaching BOS page");
        if (*ogg_info->bos_end == NULL)
            ogg_info->header_pages_tail = refbuf;
        refbuf->associated = *ogg_info->bos_end;
        *ogg_info->bos_end = refbuf;
        ogg_info->bos_end = &refbuf->associated;
        return;
    }
    DEBUG0 ("attaching header page");
    if (ogg_info->header_pages_tail)
        ogg_info->header_pages_tail->associated = refbuf;
    ogg_info->header_pages_tail = refbuf;

    if (ogg_info->header_pages == NULL)
        ogg_info->header_pages = refbuf;
}


void format_ogg_free_headers (ogg_state_t *ogg_info)
{
    /* release the header pages first */
    DEBUG0 ("releasing header pages");
    refbuf_release (ogg_info->header_pages);
    ogg_info->header_pages = NULL;
    ogg_info->header_pages_tail = NULL;
    ogg_info->bos_end = &ogg_info->header_pages;
}


/* release the memory used for the codec and header pages from the module */
static void free_ogg_codecs (ogg_state_t *ogg_info)
{
    ogg_codec_t *codec;

    if (ogg_info == NULL)
        return;

    format_ogg_free_headers (ogg_info);

    /* now free the codecs */
    codec = ogg_info->codecs;
    DEBUG0 ("freeing codecs");
    while (codec)
    {
        ogg_codec_t *next = codec->next;
        refbuf_release (codec->possible_start);
        codec->codec_free (ogg_info, codec);
        codec = next;
    }
    ogg_info->codecs = NULL;
    ogg_info->current = NULL;
    ogg_info->bos_completed = 0;
    ogg_info->codec_count = 0;
}


int format_ogg_get_plugin (format_plugin_t *plugin, client_t *client)
{
    ogg_state_t *state = calloc (1, sizeof (ogg_state_t));

    plugin->get_buffer = ogg_get_buffer;
    plugin->write_buf_to_client = write_buf_to_client;
    plugin->write_buf_to_file = write_ogg_to_file;
    plugin->create_client_data = create_ogg_client_data;
    plugin->free_plugin = format_ogg_free_plugin;
    plugin->get_image = get_image;
    plugin->set_tag = NULL;
    plugin->apply_settings = apply_ogg_settings;
    if (plugin->parser)
    {
        const char *s = httpp_getvar (plugin->parser, "content-type");;
        if (s==NULL || strcmp (s, "application/x-ogg") == 0)
            httpp_setvar (plugin->parser, "content-type", "application/ogg");
        plugin->contenttype = strdup (httpp_getvar (plugin->parser, "content-type"));
    }
    else
        plugin->contenttype = strdup ("application/ogg");

    ogg_sync_init (&state->oy);

    plugin->_state = state;
    state->mount = plugin->mount;
    state->bos_end = &state->header_pages;

    return 0;
}


static void format_ogg_free_plugin (format_plugin_t *plugin, client_t *client)
{
    ogg_state_t *state = plugin->_state;

    /* free memory associated with this plugin instance */
    free_ogg_codecs (state);
    free (state->artist);
    free (state->title);

    ogg_sync_clear (&state->oy);
    free (state);
    free (plugin->contenttype);
}


static void apply_ogg_settings (format_plugin_t *format, mount_proxy *mount)
{
    ogg_state_t *ogg_info = format->_state;

    if (mount == NULL || format == NULL || ogg_info == NULL)
        return;
    if (mount->filter_theora)
        ogg_info->filter_theora = 1;
    DEBUG1 ("filter for theora is %d", ogg_info->filter_theora);

    ogg_info->use_url_metadata = mount->url_ogg_meta;
    DEBUG1 ("metadata via url is %d", ogg_info->use_url_metadata);

    ogg_info->passthrough = mount->ogg_passthrough;
    DEBUG1 ("oggpassthrough is %d", ogg_info->passthrough);

    ogg_info->admin_comments_only = mount->admin_comments_only;
    DEBUG1 ("admin_comments_only is %d", ogg_info->admin_comments_only);
}


/* a new BOS page has been seen so check which codec it is */
static int process_initial_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec;

    if (ogg_info->bos_completed)
    {
        ogg_info->bitrate = 0;
        ogg_info->codec_sync = NULL;
        /* need to zap old list of codecs when next group of BOS pages appear */
        free_ogg_codecs (ogg_info);
    }
    do
    {
        if (ogg_info->codec_count > 10)
        {
            ERROR0 ("many codecs in stream, playing safe, dropping source");
            ogg_info->error = 1;
            return -1;
        }
        codec = initial_vorbis_page (plugin, page);
        if (codec)
            break;
#ifdef HAVE_THEORA
        codec = initial_theora_page (plugin, page);
        if (codec)
            break;
#endif
        codec = initial_midi_page (plugin, page);
        if (codec)
            break;
        codec = initial_flac_page (plugin, page);
        if (codec)
            break;
#ifdef HAVE_SPEEX
        codec = initial_speex_page (plugin, page);
        if (codec)
            break;
#endif
        codec = initial_kate_page (plugin, page);
        if (codec)
            break;
        codec = initial_skeleton_page (plugin, page);
        if (codec)
            break;

        /* any others */
        ERROR0 ("Seen BOS page with unknown type");
        ogg_info->error = 1;
        return -1;
    } while (0);

    if (codec)
    {
        /* add codec to list */
        codec->next = ogg_info->codecs;
        ogg_info->codecs = codec;
        ogg_info->codec_count++;
        DEBUG2 ("%s codec has filter mark %d", codec->name, codec->filtered);
    }

    return 0;
}


/* This is called when there has been a change in the metadata. Usually
 * artist and title are provided separately so here we update the stats
 * and write log entry if required.
 */
static void update_comments (source_t *source)
{
    ogg_state_t *ogg_info = source->format->_state;
    char *title = ogg_info->title;
    char *artist = ogg_info->artist;
    char *metadata = NULL;
    unsigned int len = 1; /* space for the nul byte at least */
    ogg_codec_t *codec;
    char codec_names [100] = "";

    if (ogg_info->artist)
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
    stats_event (source->mount, "artist", artist);
    stats_event (source->mount, "title", title);
    stats_event_time (source->mount, "metadata_updated", STATS_GENERAL);

    codec = ogg_info->codecs;
    while (codec)
    {
        if (codec->name)
        {
            size_t len = strlen (codec_names);
            size_t remaining = sizeof (codec_names) - len;
            char *where = codec_names + len;
            char *separator = "/";
            if (len == 0)
                separator = "";
            snprintf (where, remaining, "%s%s", separator, codec->name);
        }
        codec = codec->next;
    }
    stats_event (source->mount, "subtype", codec_names);
    yp_touch (source->mount);
}


/* called when preparing a refbuf with audio data to be passed
 * back for queueing
 */
static refbuf_t *complete_buffer (source_t *source, refbuf_t *refbuf)
{
    ogg_state_t *ogg_info = source->format->_state;

    refbuf->associated = ogg_info->header_pages;
    refbuf_addref (refbuf->associated);

    if (ogg_info->log_metadata)
    {
        update_comments (source);
        ogg_info->log_metadata = 0;
    }
    /* listeners can start anywhere unless the codecs themselves are
     * marking starting points */
    if (ogg_info->codec_sync == NULL)
        refbuf->flags |= SOURCE_BLOCK_SYNC;
    source->client->queue_pos += refbuf->len;
    return refbuf;
}


/* process the incoming page. this requires searching through the
 * currently known codecs that have been seen in the stream
 */
static refbuf_t *process_ogg_page (ogg_state_t *ogg_info, ogg_page *page)
{
    ogg_codec_t *codec = ogg_info->codecs;
    refbuf_t *refbuf = NULL;

    while (codec)
    {
        if (ogg_page_serialno (page) == codec->os.serialno)
        {
            if (codec->process_page)
                refbuf = codec->process_page (ogg_info, codec, page);
            break;
        }

        codec = codec->next;
    }
    ogg_info->current = codec;
    return refbuf;
}


/* main plugin handler for getting a buffer for the queue. In here we
 * just add an incoming page to the codecs and process it until either
 * more data is needed or we prodice a buffer for the queue.
 */
static refbuf_t *ogg_get_buffer (source_t *source)
{
    ogg_state_t *ogg_info = source->format->_state;
    format_plugin_t *format = source->format;
    char *data = NULL;
    int bytes = 0;

    while (1)
    {
        while (1)
        {
            ogg_page page;
            refbuf_t *refbuf = NULL;
            ogg_codec_t *codec = ogg_info->current;

            /* if a codec has just been given a page then process it */
            if (codec && codec->process)
            {
                refbuf = codec->process (ogg_info, codec);
                if (refbuf)
                    return complete_buffer (source, refbuf);

                ogg_info->current = NULL;
            }

            if (ogg_sync_pageout (&ogg_info->oy, &page) > 0)
            {
                if (ogg_page_bos (&page))
                {
                    process_initial_page (source->format, &page);
                }
                else
                {
                    ogg_info->bos_completed = 1;
                    refbuf = process_ogg_page (ogg_info, &page);
                }
                if (ogg_info->error)
                {
                    ERROR0 ("Problem processing stream");
                    source->flags &= ~SOURCE_RUNNING;
                    return NULL;
                }
                if (refbuf)
                    return complete_buffer (source, refbuf);
                continue;
            }
            /* need more stream data */
            break;
        }
        /* we need more data to continue getting pages */
        data = ogg_sync_buffer (&ogg_info->oy, 4096);

        bytes = client_read_bytes (source->client, data, 4096);
        if (bytes <= 0)
        {
            ogg_sync_wrote (&ogg_info->oy, 0);
            return NULL;
        }
        format->read_bytes += bytes;
        rate_add (format->in_bitrate, bytes, source->client->worker->current_time.tv_sec);
        ogg_sync_wrote (&ogg_info->oy, bytes);
    }
}


static int create_ogg_client_data (format_plugin_t *plugin, client_t *client) 
{
    struct ogg_client *client_data = calloc (1, sizeof (struct ogg_client));
    int ret = -1;

    if (client_data)
    {
        client_data->headers_sent = 1;
        client->format_data = client_data;
        client->free_client_data = free_ogg_client_data;
        ret = format_general_headers (plugin, client);
    }
    return ret;
}


static void free_ogg_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}


/* send out the header pages. These are for all codecs but are
 * in the order for the stream, ie BOS pages first
 */
static int send_ogg_headers (client_t *client, refbuf_t *headers)
{
    struct ogg_client *client_data = client->format_data;
    refbuf_t *refbuf;
    int written = 0, loop = 10;

    if (client->flags & CLIENT_HAS_MOVED)
    {
        /* if client has been moved then we need to send all header pages */
        client_data->headers_sent = 1;
        client->flags &= ~CLIENT_HAS_MOVED;
    }
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
        int ret = -1;

        if (len < 8000 && client->connection.error == 0)
            ret = client_send_bytes (client, data, len);
        if (ret > 0)
        {
           written += ret;
           client_data->pos += ret;
        }
        if (ret < (int)len)
        {
            client->schedule_ms += 50;
            return written ? written : -1;
        }
        if (client_data->pos == refbuf->len)
        {
            refbuf = refbuf->associated;
            client_data->header_page = refbuf;
            client_data->pos = 0;
        }
        if (written > 10000 || loop == 0)
            return written;
        loop--;
    }
    client_data->headers_sent = 1;
    client_data->headers = headers;
    client_data->header_page = NULL;
    return written;
}


/* main client write routine for sending ogg data. Each refbuf has a
 * single page so we only need to determine if there are new headers
 */
static int write_buf_to_client (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    char *buf = refbuf->data + client->pos;
    unsigned len = refbuf->len - client->pos;
    struct ogg_client *client_data = client->format_data;
    int ret, written = 0;

    do
    {
        if (client_data->headers != refbuf->associated)
        {
            ret = send_ogg_headers (client, refbuf->associated);
            if (ret > 0)
                written += ret;
            if (client_data->headers_sent == 0)
                break;
        }
        ret = client_send_bytes (client, buf, len);

        if (ret > 0)
        {
            client->pos += ret;
            client->queue_pos += ret;
            written += ret;
            client->counter += ret;
        }

        if (ret < (int)len)
            client->schedule_ms += 50;
    } while (0);

    return written ? written : -1;
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
            header = header->associated;
        }
        ogg_info->file_headers = refbuf->associated;
    }
    write_ogg_data (source, refbuf);
}

static int get_image (client_t *client, struct _format_plugin_tag *format)
{
    const char *serialp = httpp_get_query_param (client->parser, "serial");
    ogg_state_t *ogg_info = format->_state;
    ogg_codec_t *codec = ogg_info->codecs;
    long serial;

    if (serialp)
        serial = atoll (serialp);
    while (codec)
    {
        if (serialp == NULL || serial == codec->os.serialno)
        {
            int ret = 0;
            if (codec->get_image)
                ret = codec->get_image (client, codec);
            return ret;
        }
        codec = codec->next;
    }
    return 0;
}
