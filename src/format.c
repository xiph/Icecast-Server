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
#include "stats.h"
#define CATMODULE "format"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

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

int format_get_plugin(format_type_t type, source_t *source)
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
    if (ret < 0)
        stats_event (source->mount, "content-type", 
                source->format->contenttype);

    return ret;
}


/* clients need to be start from somewhere in the queue so we will look for
 * a refbuf which has been previously marked as a sync point. 
 */
static void find_client_start (source_t *source, client_t *client)
{
    refbuf_t *refbuf = source->burst_point;

    /* we only want to attempt a burst at connection time, not midstream */
    if (client->intro_offset == -1)
        refbuf = source->stream_data_tail;
    else
    {
        long size = 0;
        refbuf = source->burst_point;
        size = source->burst_size - client->intro_offset;
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
            client->intro_offset = -1;
            break;
        }
        refbuf = refbuf->next;
    }
}


static int get_file_data (FILE *intro, client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int bytes;

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
        if (source->intro_file && client->intro_offset == 0)
        {
            refbuf = refbuf_new (4096);
            client->refbuf = refbuf;
            client->pos = refbuf->len;
        }
        else
        {
            find_client_start (source, client);
            return -1;
        }
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
                client->intro_offset = -1;
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


void format_send_general_headers(format_plugin_t *format,
        source_t *source, client_t *client)
{
    http_var_t *var;
    avl_node *node;
    int bytes;

    /* iterate through source http headers and send to client */
    avl_tree_rlock(source->parser->vars);
    node = avl_get_first(source->parser->vars);
    while (node)
    {
        var = (http_var_t *)node->key;
        if (!strcasecmp(var->name, "ice-audio-info")) {
            /* convert ice-audio-info to icy-br */
            char *brfield;
            unsigned int bitrate;

            brfield = strstr(var->value, "bitrate=");
            if (brfield && sscanf(var->value, "bitrate=%u", &bitrate)) {
                bytes = sock_write(client->con->sock, "icy-br:%u\r\n", bitrate);
                if (bytes > 0)
                    client->con->sent_bytes += bytes;
            }
        }
        else
        {
            if (strcasecmp(var->name, "ice-password") &&
                strcasecmp(var->name, "icy-metaint"))
            {
                bytes = 0;
                if (!strncasecmp("ice-", var->name, 4))
                {
                    if (!strcasecmp("ice-bitrate", var->name))
                        bytes += sock_write(client->con->sock, "icy-br:%s\r\n", var->value);
                    else
                        if (!strcasecmp("ice-public", var->name))
                            bytes += sock_write(client->con->sock, 
                                "icy-pub:%s\r\n", var->value);
                        else
                            bytes = sock_write(client->con->sock, "icy%s:%s\r\n",
                                var->name + 3, var->value);
                            
                }
                if (!strncasecmp("icy-", var->name, 4))
                {
                    bytes = sock_write(client->con->sock, "icy%s:%s\r\n",
                            var->name + 3, var->value);
                }
                if (bytes > 0)
                    client->con->sent_bytes += bytes;
            }
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->parser->vars);
    bytes = sock_write(client->con->sock,
            "Server: %s\r\n", ICECAST_VERSION_STRING);
    if(bytes > 0) client->con->sent_bytes += bytes;
}

