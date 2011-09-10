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
    else if(strcmp(contenttype, "audio/aac") == 0)
        return FORMAT_TYPE_AAC;
    else if(strcmp(contenttype, "audio/aacp") == 0)
        return FORMAT_TYPE_AAC;
    else if(strcmp(contenttype, "audio/mpeg") == 0)
        return FORMAT_TYPE_MPEG;
    else
        /* We default to the Generic format handler, which
           can handle many more formats than just mp3 */
        return FORMAT_TYPE_GENERIC;
}


void format_plugin_clear (format_plugin_t *format, client_t *client)
{
    if (format == NULL)
        return;
    if (format->free_plugin)
        format->free_plugin (format, client);
    rate_free (format->in_bitrate);
    rate_free (format->out_bitrate);
    free (format->charset);
    if (format->parser && format->parser != client->parser) // a relay client may have a new parser
        httpp_destroy (format->parser);
    memset (format, 0, sizeof (format_plugin_t));
}


int format_get_plugin (format_plugin_t *plugin, client_t *client)
{
    int ret = -1;

    if (client)
        plugin->parser = client->parser;
    switch (plugin->type)
    {
        case FORMAT_TYPE_OGG:
            ret = format_ogg_get_plugin (plugin, client);
            break;
        case FORMAT_TYPE_AAC:
        case FORMAT_TYPE_MPEG:
        case FORMAT_TYPE_GENERIC:
            ret = format_mp3_get_plugin (plugin, client);
            break;
        default:
            break;
    }

    return ret;
}


int format_file_read (client_t *client, format_plugin_t *plugin, FILE *fp)
{
    refbuf_t *refbuf = client->refbuf;
    size_t bytes = -1;
    int unprocessed = 0;

    do
    {
        if (refbuf == NULL)
        {
            if (fp == NULL)
                return -2;
            refbuf = client->refbuf = refbuf_new (4096);
            client->pos = refbuf->len;
            client->intro_offset = 0;
            client->queue_pos = 0;
        }
        if (client->pos < refbuf->len)
            break;
        if (fp == NULL || client->flags & CLIENT_HAS_INTRO_CONTENT)
        {
            if (refbuf->next)
            {
                //DEBUG1 ("next intro block is %d", refbuf->next->len);
                client->refbuf = refbuf->next;
                refbuf->next = NULL;
                refbuf_release (refbuf);
                client->pos = 0;
                return 0;
            }
            //DEBUG0 ("No more intro data ");
            client_set_queue (client, NULL);
            client->flags &= ~CLIENT_HAS_INTRO_CONTENT;
            client->intro_offset = client->connection.sent_bytes;
            refbuf = NULL;
            continue;
        }

        if (fseek (fp, client->intro_offset, SEEK_SET) < 0 ||
                (bytes = fread (refbuf->data, 1, 4096, fp)) <= 0)
        {
            return bytes < 0 ? -2 : -1;
        }
        refbuf->len = bytes;
        client->pos = 0;
        if (plugin->align_buffer)
        {
            /* here the buffer may require truncating to keep the buffers aligned on
             * certain boundaries */
            unprocessed = plugin->align_buffer (client, plugin);
            if (unprocessed < 0 || unprocessed >= bytes)
                unprocessed = 0;
        }
        client->intro_offset += (bytes - unprocessed);
    } while (1);
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
        const char *useragent = httpp_getvar (client->parser, "user-agent");
        const char *protocol = "HTTP/1.0";
        const char *contenttypehdr = "Content-Type";
        const char *contenttype = plugin->contenttype;

        if (useragent)
        {
            if (strstr (useragent, "shoutcastsource")) /* hack for mpc */
                protocol = "ICY";
            if (strstr (useragent, "Shoutcast Server")) /* hack for sc_serv */
                contenttypehdr = "content-type";
            // if (strstr (useragent, "Sonos"))
                // contenttypehdr = "content-type";
            if (plugin->type == FORMAT_TYPE_AAC)
            {
                if (strstr (useragent, "BlackBerry"))
                    contenttype="audio/aac";
            }
        }
        bytes = snprintf (ptr, remaining, "%s 200 OK\r\n"
                "%s: %s\r\n", protocol, contenttypehdr, contenttype);
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

