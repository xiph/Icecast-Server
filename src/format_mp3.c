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
#define ICY_METADATA_INTERVAL 16000

static void format_mp3_free_plugin(format_plugin_t *self);
static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
        unsigned long len, refbuf_t **buffer);
static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self);
static void *format_mp3_create_client_data(format_plugin_t *self,
        source_t *source, client_t *client);
static void free_mp3_client_data (client_t *client);
static int format_mp3_write_buf_to_client(format_plugin_t *self,
        client_t *client, unsigned char *buf, int len);
static void format_mp3_send_headers(format_plugin_t *self, 
        source_t *source, client_t *client);

typedef struct {
   int use_metadata;
   int interval;
   int offset;
   int metadata_age;
   int metadata_offset;
} mp3_client_data;

format_plugin_t *format_mp3_get_plugin(http_parser_t *parser)
{
    char *metadata;
    format_plugin_t *plugin;
    mp3_state *state = calloc(1, sizeof(mp3_state));

    plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_MP3;
    plugin->has_predata = 0;
    plugin->get_buffer = format_mp3_get_buffer;
    plugin->get_predata = format_mp3_get_predata;
    plugin->write_buf_to_client = format_mp3_write_buf_to_client;
    plugin->create_client_data = format_mp3_create_client_data;
    plugin->client_send_headers = format_mp3_send_headers;
    plugin->free_plugin = format_mp3_free_plugin;
    plugin->format_description = "MP3 audio";

    plugin->_state = state;

    state->metadata_age = 0;
    state->metadata = strdup("");
    thread_mutex_create(&(state->lock));

    metadata = httpp_getvar(parser, "icy-metaint");
    if(metadata)
        state->inline_metadata_interval = atoi(metadata);

    return plugin;
}


static int send_metadata(client_t *client, mp3_client_data *client_state,
        mp3_state *source_state)
{
    int len_byte;
    int len;
    int ret = -1;
    unsigned char *buf;
    int source_age;
    char *fullmetadata = NULL;
    int  fullmetadata_size = 0;
    const char meta_fmt[] = "StreamTitle='';"; 

    do 
    {
        thread_mutex_lock (&(source_state->lock));
        if (source_state->metadata == NULL)
            break; /* Shouldn't be possible */

        fullmetadata_size = strlen (source_state->metadata) + sizeof (meta_fmt);

        if (fullmetadata_size > 4080)
        {
            fullmetadata_size = 4080;
        }
        fullmetadata = malloc (fullmetadata_size);
        if (fullmetadata == NULL)
            break;

        fullmetadata_size = snprintf (fullmetadata, fullmetadata_size,
                "StreamTitle='%.*s';", fullmetadata_size-(sizeof (meta_fmt)-1), source_state->metadata); 

        source_age = source_state->metadata_age;

        if (fullmetadata_size > 0 && source_age != client_state->metadata_age)
        {
            len_byte = (fullmetadata_size-1)/16 + 1; /* to give 1-255 */
            client_state->metadata_offset = 0;
        }
        else
            len_byte = 0;
        len = 1 + len_byte*16;
        buf = malloc (len);
        if (buf == NULL)
            break;

        buf[0] = len_byte;

        if (len > 1) {
            strncpy (buf+1, fullmetadata, len-1);
            buf[len-1] = '\0';
        }

        thread_mutex_unlock (&(source_state->lock));

        /* only write what hasn't been written already */
        ret = sock_write_bytes (client->con->sock, buf+client_state->metadata_offset, len-client_state->metadata_offset);

        if (ret > 0 && ret < len) {
            client_state->metadata_offset += ret;
        }
        else if (ret == len) {
            client_state->metadata_age = source_age;
            client_state->offset = 0;
            client_state->metadata_offset = 0;
        }
        free (buf);
        free (fullmetadata);
        return ret;

    } while (0);

    thread_mutex_unlock(&(source_state->lock));
    free (fullmetadata);
    return -1;
}

static int format_mp3_write_buf_to_client(format_plugin_t *self, 
    client_t *client, unsigned char *buf, int len) 
{
    int ret;
    mp3_client_data *mp3data = client->format_data;
    
    if(((mp3_state *)self->_state)->metadata && mp3data->use_metadata)
    {
        mp3_client_data *state = client->format_data;
        int max = state->interval - state->offset;

        if(len == 0) /* Shouldn't happen */
            return 0;

        if(max > len)
            max = len;

        if(max > 0) {
            ret = sock_write_bytes(client->con->sock, buf, max);
            if(ret > 0)
                state->offset += ret;
        }
        else {
            ret = send_metadata(client, state, self->_state);
            if(ret > 0)
                client->con->sent_bytes += ret;
            ret = 0;
        }

    }
    else {
        ret = sock_write_bytes(client->con->sock, buf, len);
    }

    if(ret < 0) {
        if(sock_recoverable(sock_error())) {
            DEBUG1("Client had recoverable error %ld", ret);
            ret = 0;
        }
    }
    else
        client->con->sent_bytes += ret;

    return ret;
}

static void format_mp3_free_plugin(format_plugin_t *self)
{
    /* free the plugin instance */
    mp3_state *state = self->_state;
    thread_mutex_destroy(&(state->lock));

    free(state->metadata);
    free(state);
    free(self);
}

static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
    unsigned long len, refbuf_t **buffer)
{
    refbuf_t *refbuf;
    mp3_state *state = self->_state;

    /* Set this to NULL in case it doesn't get set to a valid buffer later */
    *buffer = NULL;

    if(!data)
        return 0;

    if(state->inline_metadata_interval) {
        /* Source is sending metadata, handle it... */

        while(len > 0) {
            int to_read = state->inline_metadata_interval - state->offset;
            if(to_read > 0) {
                refbuf_t *old_refbuf = *buffer;

                if(to_read > len)
                    to_read = len;

                if(old_refbuf) {
                    refbuf = refbuf_new(to_read + old_refbuf->len);
                    memcpy(refbuf->data, old_refbuf->data, old_refbuf->len);
                    memcpy(refbuf->data+old_refbuf->len, data, to_read);

                    refbuf_release(old_refbuf);
                }
                else {
                    refbuf = refbuf_new(to_read);
                    memcpy(refbuf->data, data, to_read);
                }

                *buffer = refbuf;

                state->offset += to_read;
                data += to_read;
                len -= to_read;
            }
            else if(!state->metadata_length) {
                /* Next up is the metadata byte... */
                unsigned char byte = data[0];
                data++;
                len--;

                /* According to the "spec"... this byte * 16 */
                state->metadata_length = byte * 16;

                if(state->metadata_length) {
                    state->metadata_buffer = 
                        calloc(state->metadata_length + 1, 1);

                    /* Ensure we have a null-terminator even if the source
                     * stream is invalid.
                     */
                    state->metadata_buffer[state->metadata_length] = 0;
                }
                else {
                    state->offset = 0;
                }

                state->metadata_offset = 0;
            }
            else {
                /* Metadata to read! */
                int readable = state->metadata_length - state->metadata_offset;

                if(readable > len)
                    readable = len;

                memcpy(state->metadata_buffer + state->metadata_offset, 
                        data, readable);

                state->metadata_offset += readable;

                data += readable;
                len -= readable;

                if(state->metadata_offset == state->metadata_length)
                {
                    if(state->metadata_length)
                    {
                        thread_mutex_lock(&(state->lock));
                        free(state->metadata);
                        /* Now, reformat state->metadata_buffer to strip off
                           StreamTitle=' and the closing '; (but only if there's
                           enough data for it to be correctly formatted) */
                        if(state->metadata_length >= 15) {
                            /* This is overly complex because the 
                               metadata_length is the length of the actual raw
                               data, but the (null-terminated) string is going
                               to be shorter than this, and we can't trust that
                               the raw data doesn't have other embedded-nulls */
                            int stringlength;
                            
                            state->metadata = malloc(state->metadata_length -
                                    12);
                            memcpy(state->metadata, 
                                    state->metadata_buffer + 13, 
                                    state->metadata_length - 13);
                            /* Make sure we've got a null-terminator of some
                               sort */
                            state->metadata[state->metadata_length - 13] = 0;

                            /* Now figure out the _right_ one */
                            stringlength = strlen(state->metadata);
                            if(stringlength > 2)
                                state->metadata[stringlength - 2] = 0;
                            free(state->metadata_buffer);
                        }
                        else
                            state->metadata = state->metadata_buffer;

                        stats_event(self->mount, "title", state->metadata);
                        state->metadata_buffer = NULL;
                        state->metadata_age++;
                        thread_mutex_unlock(&(state->lock));
                        yp_touch (self->mount);
                    }

                    state->offset = 0;
                    state->metadata_length = 0;
                }
            }
        }

        /* Either we got a buffer above (in which case it can be used), or
         * we set *buffer to NULL in the prologue, so the return value is
         * correct anyway...
         */
        return 0;
    }
    else {
        /* Simple case - no metadata, just dump data directly to a buffer */
        refbuf = refbuf_new(len);

        memcpy(refbuf->data, data, len);

        *buffer = refbuf;
        return 0;
    }
}

static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self)
{
    return NULL;
}

static void *format_mp3_create_client_data(format_plugin_t *self, 
        source_t *source, client_t *client) 
{
    mp3_client_data *data = calloc(1,sizeof(mp3_client_data));
    char *metadata;

    data->interval = ICY_METADATA_INTERVAL;
    data->offset = 0;
    client->free_client_data = free_mp3_client_data;

    metadata = httpp_getvar(client->parser, "icy-metadata");
    if(metadata)
        data->use_metadata = atoi(metadata)>0?1:0;

    return data;
}


static void free_mp3_client_data (client_t *client)
{
    free (client->format_data);
    client->format_data = NULL;
}


static void format_mp3_send_headers(format_plugin_t *self,
        source_t *source, client_t *client)
{
    int bytes;
    mp3_client_data *mp3data = client->format_data;
    
    client->respcode = 200;
    /* TODO: This may need to be ICY/1.0 for shoutcast-compatibility? */
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\n" 
            "Content-Type: %s\r\n", 
            format_get_mimetype(source->format->type));

    if (bytes > 0)
        client->con->sent_bytes += bytes;

    if (mp3data->use_metadata)
    {
        int bytes = sock_write(client->con->sock, "icy-metaint:%d\r\n", 
                ICY_METADATA_INTERVAL);
        if(bytes > 0)
            client->con->sent_bytes += bytes;
    }
    format_send_general_headers(self, source, client);
}

