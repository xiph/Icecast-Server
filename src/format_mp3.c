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

/* -*- c-basic-offset: 4; -*- */
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
#define ICY_METADATA_INTERVAL 8192

static void format_mp3_free_plugin(format_plugin_t *plugin);
static refbuf_t *mp3_get_filter_meta (source_t *source);
static refbuf_t *mp3_get_no_meta (source_t *source);

static int  format_mp3_create_client_data (source_t *source, client_t *client);
static void free_mp3_client_data (client_t *client);
static int format_mp3_write_buf_to_client(format_plugin_t *self, client_t *client);
static void write_mp3_to_file (struct source_tag *source, refbuf_t *refbuf);
static void mp3_set_tag (format_plugin_t *plugin, char *tag, char *value);


typedef struct {
   int use_metadata;
   int metadata_offset;
   unsigned since_meta_block;
   int in_metadata;
   refbuf_t *associated;
} mp3_client_data;

int format_mp3_get_plugin (source_t *source)
{
    char *metadata;
    format_plugin_t *plugin;
    mp3_state *state = calloc(1, sizeof(mp3_state));
    refbuf_t *meta;

    plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_MP3;
    plugin->get_buffer = mp3_get_no_meta;
    plugin->write_buf_to_client = format_mp3_write_buf_to_client;
    plugin->write_buf_to_file = write_mp3_to_file;
    plugin->create_client_data = format_mp3_create_client_data;
    plugin->free_plugin = format_mp3_free_plugin;
    plugin->set_tag = mp3_set_tag;
    plugin->prerelease = NULL;
    plugin->format_description = "MP3 audio";

    plugin->_state = state;

    meta = refbuf_new (1);
    memcpy (meta->data, "", 1);
    meta->len = 1;
    state->metadata = meta;
    state->interval = ICY_METADATA_INTERVAL;

    metadata = httpp_getvar (source->parser, "icy-metaint");
    if (metadata)
    {
        state->inline_metadata_interval = atoi (metadata);
        state->offset = 0;
        plugin->get_buffer = mp3_get_filter_meta;
    }
    source->format = plugin;

    return 0;
}


static void mp3_set_tag (format_plugin_t *plugin, char *tag, char *value)
{
    mp3_state *source_mp3 = plugin->_state;
    unsigned len;
    const char meta[] = "StreamTitle='";
    int size = sizeof (meta) + 1;

    if (tag==NULL || value == NULL)
        return;

    len = strlen (value)+1;
    size += len;
    if (strcmp (tag, "title") == 0 || strcmp (tag, "song") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (source_mp3->url_title);
            free (source_mp3->url_artist);
            source_mp3->url_artist = NULL;
            source_mp3->url_title = p;
            source_mp3->update_metadata = 1;
        }
        return;
    }
    if (strcmp (tag, "artist") == 0)
    {
        char *p = strdup (value);
        if (p)
        {
            free (source_mp3->url_artist);
            source_mp3->url_artist = p;
        }
    }
    source_mp3->update_metadata = 1;
}


static void filter_shoutcast_metadata (source_t *source, char *metadata, unsigned meta_len)
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
            if ((end = strstr (metadata, "\';")) == NULL)
                break;
            len = (end - metadata) - 13;
            p = calloc (1, len+1);
            if (p)
            {
                memcpy (p, metadata+13, len);
                stats_event (source->mount, "title", p);
                yp_touch (source->mount);
                free (p);
            }
        } while (0);
    }
}


void mp3_set_title (source_t *source)
{
    const char meta[] = "StreamTitle='";
    int size;
    unsigned char len_byte;
    refbuf_t *p;
    unsigned len = sizeof(meta) + 6;
    mp3_state *source_mp3 = source->format->_state;

    if (source_mp3->url_artist)
        len += strlen (source_mp3->url_artist);
    if (source_mp3->url_title)
        len += strlen (source_mp3->url_title);
    if (source_mp3->url_artist && source_mp3->url_title)
        len += 3;
#define MAX_META_LEN 255*16
    size  = sizeof (meta) + len + 2;
    if (len > MAX_META_LEN-(sizeof(meta)+3))
    {
        WARN1 ("Metadata too long at %d chars", len);
        return;
    }
    len_byte = size / 16 + 1;
    size = len_byte * 16 + 1;
    p = refbuf_new (size);
    p->len = size;
    if (p)
    {
        mp3_state *source_mp3 = source->format->_state;

        memset (p->data, '\0', size);
        if (source_mp3->url_artist && source_mp3->url_title)
            snprintf (p->data, size, "%c%s%s - %s';", len_byte, meta,
                    source_mp3->url_artist, source_mp3->url_title);
        else
            snprintf (p->data, size, "%c%s%.*s';", len_byte, meta, len, source_mp3->url_title);
        filter_shoutcast_metadata (source, p->data, size);
        source_mp3->metadata = p;
    }
}


static int send_mp3_metadata (client_t *client, refbuf_t *associated)
{
    int ret = 0;
    unsigned char *metadata;
    int meta_len;
    mp3_client_data *client_mp3 = client->format_data;

    if (associated == client_mp3->associated)
    {
        metadata = "\0";
        meta_len = 1;
    }
    else
    {
        metadata = associated->data + client_mp3->metadata_offset;
        meta_len = associated->len - client_mp3->metadata_offset;
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
    client_mp3->in_metadata = 1;

    return ret;
}


/* return bytes actually written, -1 for error or 0 for no more data to write */

static int format_mp3_write_buf_to_client (format_plugin_t *self, client_t *client) 
{
    int ret, written = 0;
    mp3_client_data *client_mp3 = client->format_data;
    mp3_state *source_mp3 = self->_state;
    refbuf_t *refbuf = client->refbuf;
    char *buf;
    unsigned len;

    if (refbuf == NULL)
        return 0;  /* no data yet */
    if (refbuf->next == NULL && client->pos == refbuf->len)
        return 0;
    buf = refbuf->data + client->pos;
    len = refbuf->len - client->pos;

    do
    {
        /* send any unwritten metadata to the client */
        if (client_mp3->in_metadata)
        {
            refbuf_t *associated = refbuf->associated;
            ret = send_mp3_metadata (client, associated);

            if (ret < (int)associated->len)
                break;
            written += ret;
        }
        /* see if we need to send the current metadata to the client */
        if (client_mp3->use_metadata)
        {
            unsigned remaining = source_mp3->interval - client_mp3->since_meta_block;

            /* sending the metadata block */
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
                ret = send_mp3_metadata (client, refbuf->associated);
                if (client_mp3->in_metadata)
                    break;
                written += ret;
                /* change buf and len */
                buf += remaining;
                len -= remaining;
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
        /* we have now written what we need to in here */
        if (refbuf->next)
        {
            client->refbuf = refbuf->next;
            client->pos = 0;
        }
    } while (0);

    if (ret > 0)
        written += ret;
    return written ? written : -1;
}

static void format_mp3_free_plugin (format_plugin_t *plugin)
{
    /* free the plugin instance */
    mp3_state *state = plugin->_state;

    free(state);
    free(plugin);
}


static refbuf_t *mp3_get_no_meta (source_t *source)
{
    int bytes;
    refbuf_t *refbuf;
    mp3_state *source_mp3 = source->format->_state;

    if ((refbuf = refbuf_new (4096)) == NULL)
        return NULL;
    bytes = sock_read_bytes (source->con->sock, refbuf->data, 4096);

    if (bytes == 0)
    {
        INFO1 ("End of stream %s", source->mount);
        source->running = 0;
        refbuf_release (refbuf);
        return NULL;
    }
    if (source_mp3->update_metadata)
    {
        mp3_set_title (source);
        source_mp3->update_metadata = 0;
    }
    if (bytes > 0)
    {
        refbuf->len  = bytes;
        refbuf->associated = source_mp3->metadata;
        refbuf->sync_point = 1;
        return refbuf;
    }
    refbuf_release (refbuf);

    if (!sock_recoverable (sock_error()))
        source->running = 0;

    return NULL;
}


static refbuf_t *mp3_get_filter_meta (source_t *source)
{
    refbuf_t *refbuf;
    format_plugin_t *plugin = source->format;
    mp3_state *source_mp3 = plugin->_state;
    unsigned char *src;
    unsigned bytes, mp3_block;
    int ret;

    refbuf = refbuf_new (2048);
    src = refbuf->data;

    ret = sock_read_bytes (source->con->sock, refbuf->data, 2048);

    if (ret == 0)
    {
        INFO1 ("End of stream %s", source->mount);
        source->running = 0;
        refbuf_release (refbuf);
        return NULL;
    }
    if (source_mp3->update_metadata)
    {
        mp3_set_title (source);
        source_mp3->update_metadata = 0;
    }
    if (ret < 0)
    {
        refbuf_release (refbuf);
        if (sock_recoverable (sock_error()))
            return NULL; /* go back to waiting */
        INFO0 ("Error on connection from source");
        source->running = 0;
        return NULL;
    }
    /* fill the buffer with the read data */
    bytes = (unsigned)ret;
    while (bytes > 0)
    {
        unsigned metadata_remaining;

        mp3_block = source_mp3->inline_metadata_interval - source_mp3->offset;

        /* is there only enough to account for mp3 data */
        if (bytes <= mp3_block)
        {
            refbuf->len += bytes;
            source_mp3->offset += bytes;
            break;
        }
        /* we have enough data to get to the metadata block, but only transfer upto it */
        if (mp3_block)
        {
            src += mp3_block;
            bytes -= mp3_block;
            refbuf->len += mp3_block;
            source_mp3->offset += mp3_block;
            continue;
        }

        /* are we processing the inline metadata, len == 0 indicates not seen any */
        if (source_mp3->build_metadata_len == 0)
        {
            memset (source_mp3->build_metadata, 0, sizeof (source_mp3->build_metadata));
            source_mp3->build_metadata_offset = 0;
            source_mp3->build_metadata_len = 1 + (*src * 16);
        }

        /* do we have all of the metatdata block */
        metadata_remaining = source_mp3->build_metadata_len - source_mp3->build_metadata_offset;
        if (bytes < metadata_remaining)
        {
            memcpy (source_mp3->build_metadata + source_mp3->build_metadata_offset,
                    src, bytes);
            source_mp3->build_metadata_offset += bytes;
            break;
        }
        memcpy (source_mp3->build_metadata + source_mp3->build_metadata_offset,
                src, metadata_remaining);

        /* overwrite metadata in the buffer */
        bytes -= metadata_remaining;
        memmove (src, src+metadata_remaining, bytes);

        /* assign metadata if it's not 1 byte, as that indicates a change */
        if (source_mp3->build_metadata_len > 1)
        {
            refbuf_t *meta = refbuf_new (source_mp3->build_metadata_len);
            memcpy (meta->data, source_mp3->build_metadata, source_mp3->build_metadata_len);
            meta->len = source_mp3->build_metadata_len;

            DEBUG1("shoutcast metadata %.4080s", meta->data+1);
            if (strncmp (meta->data+1, "StreamTitle=", 12) == 0)
            {
                filter_shoutcast_metadata (source, source_mp3->build_metadata, source_mp3->build_metadata_len);
                source_mp3->metadata = meta;
            }
            else
            {
                ERROR0 ("Incorrect metadata format, ending stream");
                source->running = 0;
                refbuf_release (refbuf);
                return NULL;
            }
        }
        source_mp3->offset = 0;
        source_mp3->build_metadata_len = 0;
    }
    refbuf->associated = source_mp3->metadata;
    refbuf->sync_point = 1;

    return refbuf;
}


static void mp3_set_predata (source_t *source, client_t *client)
{
    mp3_client_data *mp3data = client->format_data;

    if (mp3data->use_metadata)
    {
        unsigned remaining = client->predata_size - client->predata_len + 2;
        char *ptr = client->predata + client->predata_len - 2;
        int bytes;

        bytes = snprintf (ptr, remaining, "icy-metaint:%u\r\n\r\n",
                ICY_METADATA_INTERVAL);
        if (bytes > 0)
            client->predata_len += bytes - 2;
    }
}


static int format_mp3_create_client_data(source_t *source, client_t *client) 
{
    mp3_client_data *data = calloc(1,sizeof(mp3_client_data));
    char *metadata;

    if (data == NULL)
    {
        ERROR0 ("malloc failed");
        return -1;
    }

    client->format_data = data;
    client->free_client_data = free_mp3_client_data;
    metadata = httpp_getvar(client->parser, "icy-metadata");
    
    if(metadata)
    {
        data->use_metadata = atoi(metadata)>0?1:0;

        mp3_set_predata (source, client);
    }

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
        WARN0 ("Write to dump file failed, disabling");
        fclose (source->dumpfile);
        source->dumpfile = NULL;
    }
}

