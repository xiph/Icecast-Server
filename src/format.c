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
#define CATMODULE "format"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define snprintf _snprintf
#endif

format_type_t format_get_type(char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_OGG; /* Backwards compatibility */
    else if(strcmp(contenttype, "application/ogg") == 0)
        return FORMAT_TYPE_OGG; /* Now blessed by IANA */
    else if(strcmp(contenttype, "audio/mpeg") == 0)
        return FORMAT_TYPE_MP3; 
    else if(strcmp(contenttype, "audio/x-mpeg") == 0)
        return FORMAT_TYPE_MP3; 
    else
        return FORMAT_ERROR;
}

char *format_get_mimetype(format_type_t type)
{
    switch(type) {
        case FORMAT_TYPE_OGG:
            return "application/ogg";
            break;
        case FORMAT_TYPE_MP3:
            return "audio/mpeg";
            break;
        default:
            return NULL;
    }
}

int format_get_plugin(format_type_t type, source_t *source)
{
    int ret = -1;

    switch (type)
    {
    case FORMAT_TYPE_OGG:
        ret = format_ogg_get_plugin (source);
        break;
    case FORMAT_TYPE_MP3:
        ret = format_mp3_get_plugin (source);
        break;
    default:
        break;
    }

    return ret;
}


int format_generic_write_buf_to_client(format_plugin_t *format, 
        client_t *client, unsigned char *buf, int len)
{
    int ret;

    ret = client_send_bytes (client, buf, len);
    if (ret < 0 && client->con->error == 0)
        ret = 0;

    return ret;
}


void format_prepare_headers (source_t *source, client_t *client)
{
    unsigned remaining;
    char *ptr;
    int bytes;
    int bitrate_filtered = 0;
    avl_node *node;
    char *agent;

    remaining = client->predata_size;
    ptr = client->predata;
    client->respcode = 200;

    /* ugly hack, but send ICY OK header when client is realplayer */
    agent = httpp_getvar (client->parser, "user-agent");
    if (agent && strstr (agent, "RealMedia") != NULL)
        bytes = snprintf (ptr, remaining, "ICY 200 OK\r\nContent-Type: %s\r\n",
                format_get_mimetype (source->format->type));
    else
        bytes = snprintf (ptr, remaining, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n",
                format_get_mimetype (source->format->type));

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

    bytes = snprintf (ptr, remaining, "Server: %s\r\n", ICECAST_VERSION_STRING);
    remaining -= bytes;
    ptr += bytes;

    bytes = snprintf (ptr, remaining, "\r\n");
    remaining -= bytes;
    ptr += bytes;

    client->predata_len = client->predata_size - remaining;
}


void format_initialise ()
{
    format_ogg_initialise ();
}

