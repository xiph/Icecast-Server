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

#include "compat.h"
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


format_type_t format_get_type(const char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_OGG; /* Backwards compatibility */
    else if(strcmp(contenttype, "application/ogg") == 0)
        return FORMAT_TYPE_OGG; /* Now blessed by IANA */
    else if(strcmp(contenttype, "audio/ogg") == 0)
        return FORMAT_TYPE_OGG;
    else if(strcmp(contenttype, "video/ogg") == 0)
        return FORMAT_TYPE_OGG;
    else
        /* We default to the Generic format handler, which
           can handle many more formats than just mp3 */
        return FORMAT_TYPE_GENERIC;
}

void format_plugin_clear (format_plugin_t *format)
{
    if (format == NULL)
        return;
    rate_free (format->in_bitrate);
    format->in_bitrate = NULL;
    rate_free (format->out_bitrate);
    format->out_bitrate = NULL;
    free (format->charset);
    format->charset = NULL;
    if (format->free_plugin)
        format->free_plugin (format);
    format->get_buffer = NULL;
    format->write_buf_to_client = NULL;
    format->write_buf_to_file = NULL;
    format->create_client_data = NULL;
    format->free_plugin = NULL;
    format->set_tag = NULL;
    format->apply_settings = NULL;
}


int format_get_plugin (format_plugin_t *plugin)
{
    int ret = -1;

    switch (plugin->type) {
    case FORMAT_TYPE_OGG:
        ret = format_ogg_get_plugin (plugin);
        break;
    case FORMAT_TYPE_GENERIC:
        ret = format_mp3_get_plugin (plugin);
        break;
    default:
        break;
    }

    return ret;
}


int format_file_read (client_t *client, FILE *intro)
{
    refbuf_t *refbuf = client->refbuf;

    if (refbuf == NULL)
        return -1;
    if (client->pos == refbuf->len)
    {
        size_t bytes;

        if (client->flags & CLIENT_HAS_INTRO_CONTENT)
        {
            if (refbuf->next)
            {
                client->refbuf = refbuf->next;
                refbuf->next = NULL;
                refbuf_release (refbuf);
                client->pos = 0;
                return 0;
            }
            client_set_queue (client, NULL);
            client->flags &= ~CLIENT_HAS_INTRO_CONTENT;
            client->intro_offset = client->connection.sent_bytes;
            return -1;
        }
        if (intro == NULL)
            return -1;
        if (fseek (intro, client->intro_offset, SEEK_SET) < 0)
            return -1;
        bytes = fread (refbuf->data, 1, PER_CLIENT_REFBUF_SIZE, intro);
        if (bytes == 0)
            return -1;

        client->intro_offset += bytes;
        refbuf->len = bytes;
        client->pos = 0;
    }
    return 0;
}


int format_generic_write_to_client (client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int ret;
    const char *buf = refbuf->data + client->pos;
    unsigned int len = refbuf->len - client->pos;

    if (len > 5000) /* make sure we don't send huge amounts in one go */
        len = 4096;
    ret = client_send_bytes (client, buf, len);

    if (ret > 0)
        client->pos += ret;

    return ret;
}


int format_general_headers (format_plugin_t *plugin, client_t *client)
{
    unsigned remaining = 4096 - client->refbuf->len;
    char *ptr = client->refbuf->data + client->refbuf->len;
    int bytes;
    int bitrate_filtered = 0;
    avl_node *node;
    ice_config_t *config;

    if (client->respcode == 0)
    {
        bytes = snprintf (ptr, remaining, "HTTP/1.0 200 OK\r\n"
                "Content-Type: %s\r\n", plugin->contenttype);
        remaining -= bytes;
        ptr += bytes;
        client->respcode = 200;
    }

    if (plugin->parser)
    {
        /* iterate through source http headers and send to client */
        avl_tree_rlock (plugin->parser->vars);
        node = avl_get_first (plugin->parser->vars);
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
        avl_tree_unlock (plugin->parser->vars);
    }

    config = config_get_config();
    bytes = snprintf (ptr, remaining, "Server: %s\r\n", config->server_id);
    config_release_config();
    remaining -= bytes;
    ptr += bytes;

    /* prevent proxy servers from caching */
    bytes = snprintf (ptr, remaining, "Cache-Control: no-cache\r\n");
    remaining -= bytes;
    ptr += bytes;

    bytes = snprintf (ptr, remaining, "\r\n");
    remaining -= bytes;
    ptr += bytes;

    client->refbuf->len = 4096 - remaining;
    client->refbuf->flags |= WRITE_BLOCK_GENERIC;
    return 0;
}

