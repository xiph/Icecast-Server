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
/* format.c
**
** format plugin implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <time.h>

#include "connection.h"
#include "refbuf.h"

#include "source.h"
#include "format.h"
#include "global.h"
#include "httpp/httpp.h"

#include "format_ogg.h"
#include "format_mp3.h"

#include "logging.h"
#include "stats.h"
#define CATMODULE "format"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define snprintf _snprintf
#endif

static int format_prepare_headers (source_t *source, client_t *client);


format_type_t format_get_type(char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_OGG; /* Backwards compatibility */
    else if(strcmp(contenttype, "application/ogg") == 0)
        return FORMAT_TYPE_OGG; /* Now blessed by IANA */
    else
        /* We default to the Generic format handler, which
           can handle many more formats than just mp3 */
        return FORMAT_TYPE_GENERIC;
}

void format_free_plugin (format_plugin_t *format)
{
    if (format == NULL)
        return;
    rate_free (format->in_bitrate);
    rate_free (format->out_bitrate);
    if (format->free_plugin)
        format->free_plugin (format);
}


int format_get_plugin (format_type_t type, source_t *source)
{
    int ret = -1;

    switch (type) {
    case FORMAT_TYPE_OGG:
        ret = format_ogg_get_plugin (source);
        break;
    case FORMAT_TYPE_GENERIC:
        ret = format_mp3_get_plugin (source);
        break;
    default:
        break;
    }
    source->format->in_bitrate = rate_setup (30);
    source->format->out_bitrate = rate_setup (30);

    return ret;
}


/* clients need to be start from somewhere in the queue so we will look for
 * a refbuf which has been previously marked as a sync point. 
 */
static void find_client_start (source_t *source, client_t *client)
{
    refbuf_t *refbuf = source->burst_point;

    /* we only want to attempt a burst at connection time, not midstream
     * however streams like theora may not have the most recent page marked as
     * a starting point, so look for one from the burst point */
    if (client->intro_offset == -1 && source->stream_data_tail->sync_point)
        refbuf = source->stream_data_tail;
    else
    {
        long size = 0;
        refbuf = source->burst_point;
        size = client->intro_offset;
        while (size > 0 && refbuf->next)
        {
            size -= refbuf->len;
            refbuf = refbuf->next;
        }
    }

    while (refbuf)
    {
        if (refbuf->sync_point)
        {
            client_set_queue (client, refbuf);
            client->check_buffer = format_advance_queue;
            client->write_to_client = source->format->write_buf_to_client;
            client->intro_offset = -1;
            break;
        }
        refbuf = refbuf->next;
    }
}


static int get_file_data (FILE *intro, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    size_t bytes;

    if (intro == NULL || fseek (intro, client->intro_offset, SEEK_SET) < 0)
        return 0;
    bytes = fread (refbuf->data, 1, 4096, intro);
    if (bytes == 0)
        return 0;

    refbuf->len = bytes;
    return 1;
}


/* call to check the buffer contents for file reading. move the client
 * to right place in the queue at end of file else repeat file if queue
 * is not ready yet.
 */
int format_check_file_buffer (source_t *source, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;

    if (refbuf == NULL)
    {
        /* client refers to no data, must be from a move */
        if (source->client)
        {
            find_client_start (source, client);
            return -1;
        }
        /* source -> file fallback, need a refbuf for data */
        refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
        client->refbuf = refbuf;
        client->pos = refbuf->len;
        client->intro_offset = 0;
    }
    if (client->pos == refbuf->len)
    {
        if (get_file_data (source->intro_file, client))
        {
            client->pos = 0;
            client->intro_offset += refbuf->len;
        }
        else
        {
            if (source->stream_data_tail)
            {
                /* better find the right place in queue for this client */
                client_set_queue (client, NULL);
                find_client_start (source, client);
            }
            else
                client->intro_offset = 0;  /* replay intro file */
            return -1;
        }
    }
    return 0;
}


/* call this to verify that the HTTP data has been sent and if so setup
 * callbacks to the appropriate format functions
 */
int format_check_http_buffer (source_t *source, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;

    if (refbuf == NULL)
        return -1;

    if (client->respcode == 0)
    {
        DEBUG0("processing pending client headers");

        client->respcode = 200;
        if (format_prepare_headers (source, client) < 0)
        {
            ERROR0 ("internal problem, dropping client");
            client->con->error = 1;
            return -1;
        }
    }

    if (client->pos == refbuf->len)
    {
        client->write_to_client = source->format->write_buf_to_client;
        client->check_buffer = format_check_file_buffer;
        client->intro_offset = 0;
        client->pos = refbuf->len = 4096;
        return -1;
    }
    return 0;
}


int format_generic_write_to_client (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int ret;
    const char *buf = refbuf->data + client->pos;
    unsigned int len = refbuf->len - client->pos;

    ret = client_send_bytes (client, buf, len);

    if (ret > 0)
        client->pos += ret;

    return ret;
}


/* This is the commonly used for source streams, here we just progress to
 * the next buffer in the queue if there is no more left to be written from 
 * the existing buffer.
 */
int format_advance_queue (source_t *source, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;

    if (refbuf == NULL)
        return -1;

    if (refbuf->next == NULL && client->pos == refbuf->len)
        return -1;

    /* move to the next buffer if we have finished with the current one */
    if (refbuf->next && client->pos == refbuf->len)
    {
        client_set_queue (client, refbuf->next);
        refbuf = client->refbuf;
    }
    return 0;
}


static int format_prepare_headers (source_t *source, client_t *client)
{
    unsigned remaining;
    char *ptr;
    int bytes;
    int bitrate_filtered = 0;
    avl_node *node;
    ice_config_t *config;

    remaining = client->refbuf->len;
    ptr = client->refbuf->data;
    client->respcode = 200;

    bytes = snprintf (ptr, remaining, "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n", source->format->contenttype);

    remaining -= bytes;
    ptr += bytes;

    /* iterate through source http headers and send to client */
    avl_tree_rlock (source->parser->vars);
    node = avl_get_first (source->parser->vars);
    while (node)
    {
        int next = 1;
        http_var_t *var = (http_var_t *)node->key;
        bytes = 0;
        if (!strcasecmp (var->name, "ice-audio-info"))
        {
            /* convert ice-audio-info to icy-br */
            char *brfield = NULL;
            unsigned int bitrate;

            if (bitrate_filtered == 0)
                brfield = strstr (var->value, "bitrate=");
            if (brfield && sscanf (brfield, "bitrate=%u", &bitrate))
            {
                bytes = snprintf (ptr, remaining, "icy-br:%u\r\n", bitrate);
                next = 0;
                bitrate_filtered = 1;
            }
            else
                /* show ice-audio_info header as well because of relays */
                bytes = snprintf (ptr, remaining, "%s: %s\r\n", var->name, var->value);
        }
        else
        {
            if (strcasecmp (var->name, "ice-password") &&
                    strcasecmp (var->name, "icy-metaint"))
            {
                if (!strncasecmp ("ice-", var->name, 4))
                {
                    if (!strcasecmp ("ice-public", var->name))
                        bytes = snprintf (ptr, remaining, "icy-pub:%s\r\n", var->value);
                    else
                        if (!strcasecmp ("ice-bitrate", var->name))
                            bytes = snprintf (ptr, remaining, "icy-br:%s\r\n", var->value);
                        else
                            bytes = snprintf (ptr, remaining, "icy%s:%s\r\n",
                                    var->name + 3, var->value);
                }
                else 
                    if (!strncasecmp ("icy-", var->name, 4))
                    {
                        bytes = snprintf (ptr, remaining, "icy%s:%s\r\n",
                                var->name + 3, var->value);
                    }
            }
        }

        remaining -= bytes;
        ptr += bytes;
        if (next)
            node = avl_get_next (node);
    }
    avl_tree_unlock (source->parser->vars);

    config = config_get_config();
    bytes = snprintf (ptr, remaining, "Server: %s\r\n", config->server_id);
    config_release_config();
    remaining -= bytes;
    ptr += bytes;

    bytes = snprintf (ptr, remaining, "\r\n");
    remaining -= bytes;
    ptr += bytes;

    client->refbuf->len -= remaining;
    if (source->format->create_client_data)
        if (source->format->create_client_data (source, client) < 0)
            return -1;
    return 0;
}


