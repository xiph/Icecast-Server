/* -*- c-basic-offset: 4; -*- */
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
 * Copyright 2011-2012, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* format_mp3.c
**
** format plugin for mp3
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "httpp/httpp.h"

#include "logging.h"

#include "format_mp3.h"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define snprintf _snprintf
#endif

#define CATMODULE "format-mp3"

/* Note that this seems to be 8192 in shoutcast - perhaps we want to be the
 * same for compability with crappy clients?
 */
#define ICY_METADATA_INTERVAL 16000

static void format_mp3_free_plugin(format_plugin_t *self);
static refbuf_t *mp3_get_filter_meta (source_t *source);
static refbuf_t *mp3_get_no_meta (source_t *source);

static int  format_mp3_create_client_data (source_t *source, client_t *client);
static void free_mp3_client_data (client_t *client);
static int format_mp3_write_buf_to_client(client_t *client);
static void write_mp3_to_file (struct source_tag *source, refbuf_t *refbuf);
static void mp3_set_tag (format_plugin_t *plugin, const char *tag, const char *in_value, const char *charset);
static void format_mp3_apply_settings(client_t *client, format_plugin_t *format, mount_proxy *mount);


typedef struct {
   unsigned int interval;
   int metadata_offset;
   unsigned int since_meta_block;
   int in_metadata;
   refbuf_t *associated;
} mp3_client_data;

int format_mp3_get_plugin (source_t *source)
{
    const char *metadata;
    format_plugin_t *plugin;
    mp3_state *state = calloc(1, sizeof(mp3_state));
    refbuf_t *meta;

    plugin = (format_plugin_t *)calloc(1, sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_GENERIC;
    plugin->get_buffer = mp3_get_no_meta;
    plugin->write_buf_to_client = format_mp3_write_buf_to_client;
    plugin->write_buf_to_file = write_mp3_to_file;
    plugin->create_client_data = format_mp3_create_client_data;
    plugin->free_plugin = format_mp3_free_plugin;
    plugin->set_tag = mp3_set_tag;
    plugin->apply_settings = format_mp3_apply_settings;

    plugin->contenttype = httpp_getvar (source->parser, "content-type");
    if (plugin->contenttype == NULL) {
        /* We default to MP3 audio for old clients without content types */
        plugin->contenttype = "audio/mpeg";
    }

    plugin->_state = state;

    /* initial metadata needs to be blank for sending to clients and for
       comparing with new metadata */
    meta = refbuf_new (17);
    memcpy (meta->data, "\001StreamTitle='';", 17);
    state->metadata = meta;
    state->interval = -1;

    metadata = httpp_getvar (source->parser, "icy-metaint");
    if (metadata)
    {
        state->inline_metadata_interval = atoi (metadata);
        if (state->inline_metadata_interval > 0)
        {
            state->offset = 0;
            plugin->get_buffer = mp3_get_filter_meta;
            state->interval = state->inline_metadata_interval;
        }
    }
    source->format = plugin;
    thread_mutex_create (&state->url_lock);

    return 0;
}


static void mp3_set_tag (format_plugin_t *plugin, const char *tag, const char *in_value, const char *charset)
{
    mp3_state *source_mp3 = plugin->_state;
    char *value = NULL;

    /* protect against multiple updaters */
    thread_mutex_lock (&source_mp3->url_lock);

    if (tag==NULL)
    {
        source_mp3->update_metadata = 1;
        thread_mutex_unlock (&source_mp3->url_lock);
        return;
    }

    if (in_value)
    {
        value = util_conv_string (in_value, charset, plugin->charset);
        if (value == NULL)
            value = strdup (in_value);
    }

    if (strcmp (tag, "title") == 0 || strcmp (tag, "song") == 0)
    {
        free (source_mp3->url_title);
        source_mp3->url_title = value;
    }
    else if (strcmp (tag, "artist") == 0)
    {
        free (source_mp3->url_artist);
        source_mp3->url_artist = value;
    }
    else if (strcmp (tag, "url") == 0)
    {
        free (source_mp3->url);
        source_mp3->url = value;
    }
    else
        free (value);
    thread_mutex_unlock (&source_mp3->url_lock);
}


static void filter_shoutcast_metadata (source_t *source, char *metadata, unsigned int meta_len)
{
    if (metadata)
    {
        char *end, *p;
        int len;

        do
        {
            metadata++;
            if (strncmp (metadata, "StreamTitle='", 13))
                break;
            if ((end = strstr (metadata+13, "\';")) == NULL)
                break;
            len = (end - metadata) - 13;
            p = calloc (1, len+1);
            if (p)
            {
                memcpy (p, metadata+13, len);
                logging_playlist (source->mount, p, source->listeners);
                stats_event_conv (source->mount, "title", p, source->format->charset);
                yp_touch (source->mount);
                free (p);
            }
        } while (0);
    }
}


static void format_mp3_apply_settings (client_t *client, format_plugin_t *format, mount_proxy *mount)
{
    mp3_state *source_mp3 = format->_state;

    source_mp3->interval = -1;
    free (format->charset);
    format->charset = NULL;

    if (mount)
    {
        if (mount->mp3_meta_interval >= 0)
            source_mp3->interval = mount->mp3_meta_interval;
        if (mount->charset)
            format->charset = strdup (mount->charset);
    }
    if (source_mp3->interval < 0)
    {
        const char *metadata = httpp_getvar (client->parser, "icy-metaint");
        source_mp3->interval = ICY_METADATA_INTERVAL;
        if (metadata)
        {
            int interval = atoi (metadata);
            if (interval > 0)
                source_mp3->interval = interval;
        }
    }

    if (format->charset == NULL)
        format->charset = strdup ("ISO8859-1");

    ICECAST_LOG_DEBUG("sending metadata interval %d", source_mp3->interval);
    ICECAST_LOG_DEBUG("charset %s", format->charset);
}


/* called from the source thread when the metadata has been updated.
 * The artist title are checked and made ready for clients to send
 */
static void mp3_set_title (source_t *source)
{
    const char streamtitle[] = "StreamTitle='";
    const char streamurl[] = "StreamUrl='";
    size_t size;
    unsigned char len_byte;
    refbuf_t *p;
    unsigned int len = sizeof(streamtitle) + 2; /* the StreamTitle, quotes, ; and null */
    mp3_state *source_mp3 = source->format->_state;

    /* make sure the url data does not disappear from under us */
    thread_mutex_lock (&source_mp3->url_lock);

    /* work out message length */
    if (source_mp3->url_artist)
        len += strlen (source_mp3->url_artist);
    if (source_mp3->url_title)
        len += strlen (source_mp3->url_title);
    if (source_mp3->url_artist && source_mp3->url_title)
        len += 3;
    if (source_mp3->inline_url)
    {
        char *end = strstr (source_mp3->inline_url, "';");
        if (end)
            len += end - source_mp3->inline_url+2;
    }
    else if (source_mp3->url)
        len += strlen (source_mp3->url) + strlen (streamurl) + 2;
#define MAX_META_LEN 255*16
    if (len > MAX_META_LEN)
    {
        thread_mutex_unlock (&source_mp3->url_lock);
        ICECAST_LOG_WARN("Metadata too long at %d chars", len);
        return;
    }
    /* work out the metadata len byte */
    len_byte = (len-1) / 16 + 1;

    /* now we know how much space to allocate, +1 for the len byte */
    size = len_byte * 16 + 1;

    p = refbuf_new (size);
    if (p)
    {
        mp3_state *source_mp3 = source->format->_state;
        int r;

        memset (p->data, '\0', size);
        if (source_mp3->url_artist && source_mp3->url_title)
            r = snprintf (p->data, size, "%c%s%s - %s';", len_byte, streamtitle,
                    source_mp3->url_artist, source_mp3->url_title);
        else
            r = snprintf (p->data, size, "%c%s%s';", len_byte, streamtitle,
                    source_mp3->url_title);
        if (r > 0)
        {
            if (source_mp3->inline_url)
            {
                char *end = strstr (source_mp3->inline_url, "';");
                ssize_t urllen = size;
                if (end) urllen = end - source_mp3->inline_url + 2;
                if ((ssize_t)(size-r) > urllen)
                    snprintf (p->data+r, size-r, "StreamUrl='%s';", source_mp3->inline_url+11);
            }
            else if (source_mp3->url)
                snprintf (p->data+r, size-r, "StreamUrl='%s';", source_mp3->url);
        }
        ICECAST_LOG_DEBUG("shoutcast metadata block setup with %s", p->data+1);
        filter_shoutcast_metadata (source, p->data, size);

        refbuf_release (source_mp3->metadata);
        source_mp3->metadata = p;
    }
    thread_mutex_unlock (&source_mp3->url_lock);
}


/* send the appropriate metadata, and return the number of bytes written
 * which is 0 or greater.  Check the client in_metadata value afterwards
 * to see if all metadata has been sent
 */
static int send_stream_metadata (client_t *client, refbuf_t *associated)
{
    int ret = 0;
    char *metadata;
    int meta_len;
    mp3_client_data *client_mp3 = client->format_data;

    /* If there is a change in metadata then send it else
     * send a single zero value byte in its place
     */
    if (associated && associated != client_mp3->associated)
    {
        metadata = associated->data + client_mp3->metadata_offset;
        meta_len = associated->len - client_mp3->metadata_offset;
    }
    else
    {
        if (associated)
        {
            metadata = "\0";
            meta_len = 1;
        }
        else
        {
            char *meta = "\001StreamTitle='';";
            metadata = meta + client_mp3->metadata_offset;
            meta_len = 17 - client_mp3->metadata_offset;
        }
    }
    ret = client_send_bytes (client, metadata, meta_len);

    if (ret == meta_len)
    {
        client_mp3->associated = associated;
        client_mp3->metadata_offset = 0;
        client_mp3->in_metadata = 0;
        client_mp3->since_meta_block = 0;
        return ret;
    }
    if (ret > 0)
        client_mp3->metadata_offset += ret;
    else
        ret = 0;
    client_mp3->in_metadata = 1;

    return ret;
}


/* Handler for writing mp3 data to a client, taking into account whether
 * client has requested shoutcast style metadata updates
 */
static int format_mp3_write_buf_to_client(client_t *client)
{
    int ret, written = 0;
    mp3_client_data *client_mp3 = client->format_data;
    refbuf_t *refbuf = client->refbuf;
    char *buf = refbuf->data + client->pos;
    unsigned int len = refbuf->len - client->pos;

    do
    {
        /* send any unwritten metadata to the client */
        if (client_mp3->in_metadata)
        {
            refbuf_t *associated = refbuf->associated;
            ret = send_stream_metadata (client, associated);

            if (client_mp3->in_metadata)
                break;
            written += ret;
        }
        /* see if we need to send the current metadata to the client */
        if (client_mp3->interval)
        {
            unsigned int remaining = client_mp3->interval -
                client_mp3->since_meta_block;

            /* leading up to sending the metadata block */
            if (remaining <= len)
            {
                /* send any mp3 before the metadata block */
                if (remaining)
                {
                    ret = client_send_bytes (client, buf, remaining);

                    if (ret > 0)
                    {
                        client_mp3->since_meta_block += ret;
                        client->pos += ret;
                    }
                    if (ret < (int)remaining)
                        break;
                    written += ret;
                }
                ret = send_stream_metadata (client, refbuf->associated);
                if (client_mp3->in_metadata)
                    break;
                written += ret;
                buf += remaining;
                len -= remaining;
                /* limit how much mp3 we send if using small intervals */
                if (len > client_mp3->interval)
                    len = client_mp3->interval;
            }
        }
        /* write any mp3, maybe after the metadata block */
        if (len)
        {
            ret = client_send_bytes (client, buf, len);

            if (ret > 0)
            {
                client_mp3->since_meta_block += ret;
                client->pos += ret;
            }
            if (ret < (int)len)
                break;
            written += ret;
        }
        ret = 0;
    } while (0);

    if (ret > 0)
        written += ret;
    return written;
}

static void format_mp3_free_plugin(format_plugin_t *self)
{
    /* free the plugin instance */
    mp3_state *state = self->_state;

    thread_mutex_destroy (&state->url_lock);
    free (state->url_artist);
    free (state->url_title);
    free (self->charset);
    refbuf_release (state->metadata);
    refbuf_release (state->read_data);
    free(state);
    free(self);
}


/* This does the actual reading, making sure the read data is packaged in
 * blocks of 1400 bytes (near the common MTU size). This is because many
 * incoming streams come in small packets which could waste a lot of 
 * bandwidth with many listeners due to headers and such like.
 */
static int complete_read (source_t *source)
{
    int bytes;
    format_plugin_t *format = source->format;
    mp3_state *source_mp3 = format->_state;
    char *buf;
    refbuf_t *refbuf;

#define REFBUF_SIZE 1400

    if (source_mp3->read_data == NULL)
    {
        source_mp3->read_data = refbuf_new (REFBUF_SIZE); 
        source_mp3->read_count = 0;
    }
    buf = source_mp3->read_data->data + source_mp3->read_count;

    bytes = client_read_bytes (source->client, buf, REFBUF_SIZE-source_mp3->read_count);
    if (bytes < 0)
    {
        if (source->client->con->error)
        {
            refbuf_release (source_mp3->read_data);
            source_mp3->read_data = NULL;
        }
        return 0;
    }
    source_mp3->read_count += bytes;
    refbuf = source_mp3->read_data;
    refbuf->len = source_mp3->read_count;
    format->read_bytes += bytes;

    if (source_mp3->read_count < REFBUF_SIZE)
    {
        if (source_mp3->read_count == 0)
        {
            refbuf_release (source_mp3->read_data);
            source_mp3->read_data = NULL;
        }
        return 0;
    }
    return 1;
}


/* read an mp3 stream which does not have shoutcast style metadata */
static refbuf_t *mp3_get_no_meta (source_t *source)
{
    refbuf_t *refbuf;
    mp3_state *source_mp3 = source->format->_state;

    if (complete_read (source) == 0)
        return NULL;

    refbuf = source_mp3->read_data;
    source_mp3->read_data = NULL;

    if (source_mp3->update_metadata)
    {
        mp3_set_title (source);
        source_mp3->update_metadata = 0;
    }
    refbuf->associated = source_mp3->metadata;
    refbuf_addref (source_mp3->metadata);
    refbuf->sync_point = 1;
    return refbuf;
}


/* read mp3 data with inlined metadata from the source. Filter out the
 * metadata so that the mp3 data itself is store on the queue and the
 * metadata is is associated with it
 */
static refbuf_t *mp3_get_filter_meta (source_t *source)
{
    refbuf_t *refbuf;
    format_plugin_t *plugin = source->format;
    mp3_state *source_mp3 = plugin->_state;
    unsigned char *src;
    unsigned int bytes, mp3_block;

    if (complete_read (source) == 0)
        return NULL;

    refbuf = source_mp3->read_data;
    source_mp3->read_data = NULL;
    src = (unsigned char *)refbuf->data;

    if (source_mp3->update_metadata)
    {
        mp3_set_title (source);
        source_mp3->update_metadata = 0;
    }
    /* fill the buffer with the read data */
    bytes = source_mp3->read_count;
    refbuf->len = 0;
    while (bytes > 0)
    {
        unsigned int metadata_remaining;

        mp3_block = source_mp3->inline_metadata_interval - source_mp3->offset;

        /* is there only enough to account for mp3 data */
        if (bytes <= mp3_block)
        {
            refbuf->len += bytes;
            source_mp3->offset += bytes;
            break;
        }
        /* we have enough data to get to the metadata
         * block, but only transfer upto it */
        if (mp3_block)
        {
            src += mp3_block;
            bytes -= mp3_block;
            refbuf->len += mp3_block;
            source_mp3->offset += mp3_block;
            continue;
        }

        /* process the inline metadata, len == 0 indicates not seen any yet */
        if (source_mp3->build_metadata_len == 0)
        {
            memset (source_mp3->build_metadata, 0,
                    sizeof (source_mp3->build_metadata));
            source_mp3->build_metadata_offset = 0;
            source_mp3->build_metadata_len = 1 + (*src * 16);
        }

        /* do we have all of the metatdata block */
        metadata_remaining = source_mp3->build_metadata_len -
            source_mp3->build_metadata_offset;
        if (bytes < metadata_remaining)
        {
            memcpy (source_mp3->build_metadata +
                    source_mp3->build_metadata_offset, src, bytes);
            source_mp3->build_metadata_offset += bytes;
            break;
        }
        /* copy all bytes except the last one, that way we 
         * know a null byte terminates the message */
        memcpy (source_mp3->build_metadata + source_mp3->build_metadata_offset,
                src, metadata_remaining-1);

        /* overwrite metadata in the buffer */
        bytes -= metadata_remaining;
        memmove (src, src+metadata_remaining, bytes);

        /* assign metadata if it's greater than 1 byte, and the text has changed */
        if (source_mp3->build_metadata_len > 1 &&
                strcmp (source_mp3->build_metadata+1, source_mp3->metadata->data+1) != 0)
        {
            refbuf_t *meta = refbuf_new (source_mp3->build_metadata_len);
            memcpy (meta->data, source_mp3->build_metadata,
                    source_mp3->build_metadata_len);

	    ICECAST_LOG_DEBUG("shoutcast metadata %.*s", 4080, meta->data+1);
            if (strncmp (meta->data+1, "StreamTitle=", 12) == 0)
            {
                filter_shoutcast_metadata (source, source_mp3->build_metadata,
                        source_mp3->build_metadata_len);
                refbuf_release (source_mp3->metadata);
                source_mp3->metadata = meta;
                source_mp3->inline_url = strstr (meta->data+1, "StreamUrl='");
            }
            else
            {
                ICECAST_LOG_ERROR("Incorrect metadata format, ending stream");
                source->running = 0;
                refbuf_release (refbuf);
                refbuf_release (meta);
                return NULL;
            }
        }
        source_mp3->offset = 0;
        source_mp3->build_metadata_len = 0;
    }
    /* the data we have just read may of just been metadata */
    if (refbuf->len == 0)
    {
        refbuf_release (refbuf);
        return NULL;
    }
    refbuf->associated = source_mp3->metadata;
    refbuf_addref (source_mp3->metadata);
    refbuf->sync_point = 1;

    return refbuf;
}


static int format_mp3_create_client_data(source_t *source, client_t *client)
{
    mp3_client_data *client_mp3 = calloc(1,sizeof(mp3_client_data));
    mp3_state *source_mp3 = source->format->_state;
    const char *metadata;
    /* the +-2 is for overwriting the last set of \r\n */
    unsigned remaining = 4096 - client->refbuf->len + 2;
    char *ptr = client->refbuf->data + client->refbuf->len - 2;
    int bytes;
    const char *useragent;

    if (client_mp3 == NULL)
        return -1;

    /* hack for flash player, it wants a length.  It has also been reported that the useragent
     * appears as MSIE if run in internet explorer */
    useragent = httpp_getvar (client->parser, "user-agent");
    if (httpp_getvar(client->parser, "x-flash-version") ||
            (useragent && strstr(useragent, "MSIE")))
    {
        bytes = snprintf (ptr, remaining, "Content-Length: 221183499\r\n");
        remaining -= bytes;
        ptr += bytes;
    }

    client->format_data = client_mp3;
    client->free_client_data = free_mp3_client_data;
    metadata = httpp_getvar(client->parser, "icy-metadata");
    if (metadata && atoi(metadata))
    {
        if (source_mp3->interval >= 0)
            client_mp3->interval = source_mp3->interval;
        else
            client_mp3->interval = ICY_METADATA_INTERVAL;
        if (client_mp3->interval)
        {
            bytes = snprintf (ptr, remaining, "icy-metaint:%u\r\n",
                    client_mp3->interval);
            if (bytes > 0)
            {
                remaining -= bytes;
                ptr += bytes;
            }
        }
    }
    bytes = snprintf (ptr, remaining, "\r\n");
    remaining -= bytes;
    ptr += bytes;

    client->refbuf->len = 4096 - remaining;

    return 0;
}


static void free_mp3_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}


static void write_mp3_to_file (struct source_tag *source, refbuf_t *refbuf)
{
    if (refbuf->len == 0)
        return;
    if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) < (size_t)refbuf->len)
    {
        ICECAST_LOG_WARN("Write to dump file failed, disabling");
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }
}

