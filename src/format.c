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
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#include "connection.h"
#include "refbuf.h"

#include "source.h"
#include "format.h"
#include "global.h"
#include "httpp/httpp.h"

#include "format_ogg.h"
#include "format_mp3.h"
#include "format_ebml.h"

#include "logging.h"
#include "stats.h"
#define CATMODULE "format"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define snprintf _snprintf
#endif

static int format_prepare_headers (source_t *source, client_t *client);


format_type_t format_get_type (const char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_OGG; /* Backwards compatibility */
    else if(strcmp(contenttype, "application/ogg") == 0)
        return FORMAT_TYPE_OGG; /* Now blessed by IANA */
    else if(strcmp(contenttype, "audio/ogg") == 0)
        return FORMAT_TYPE_OGG;
    else if(strcmp(contenttype, "video/ogg") == 0)
        return FORMAT_TYPE_OGG;
    else if(strcmp(contenttype, "audio/webm") == 0)
        return FORMAT_TYPE_EBML;
    else if(strcmp(contenttype, "video/webm") == 0)
        return FORMAT_TYPE_EBML;
    else if(strcmp(contenttype, "audio/x-matroska") == 0)
        return FORMAT_TYPE_EBML;
    else if(strcmp(contenttype, "video/x-matroska") == 0)
        return FORMAT_TYPE_EBML;
    else if(strcmp(contenttype, "video/x-matroska-3d") == 0)
        return FORMAT_TYPE_EBML;
    else
        /* We default to the Generic format handler, which
           can handle many more formats than just mp3.
	   Let's warn that this is not well supported */
	ICECAST_LOG_WARN("Unsupported or legacy stream type: \"%s\". Falling back to generic minimal handler for best effort.", contenttype);
        return FORMAT_TYPE_GENERIC;
}

int format_get_plugin(format_type_t type, source_t *source)
{
    int ret = -1;

    switch (type) {
    case FORMAT_TYPE_OGG:
        ret = format_ogg_get_plugin (source);
        break;
    case FORMAT_TYPE_EBML:
        ret = format_ebml_get_plugin (source);
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

    /* we only want to attempt a burst at connection time, not midstream
     * however streams like theora may not have the most recent page marked as
     * a starting point, so look for one from the burst point */
    if (client->intro_offset == -1 && source->stream_data_tail
            && source->stream_data_tail->sync_point)
        refbuf = source->stream_data_tail;
    else
    {
        size_t size = client->intro_offset;
        refbuf = source->burst_point;
        while (size > 0 && refbuf && refbuf->next)
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

    refbuf->len = (unsigned int)bytes;
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
        ICECAST_LOG_DEBUG("processing pending client headers");

        if (format_prepare_headers (source, client) < 0)
        {
            ICECAST_LOG_ERROR("internal problem, dropping client");
            client->con->error = 1;
            return -1;
        }
        client->respcode = 200;
        stats_event_inc (NULL, "listeners");
        stats_event_inc (NULL, "listener_connections");
        stats_event_inc (source->mount, "listener_connections");
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

    remaining = client->refbuf->len;
    ptr = client->refbuf->data;
    client->respcode = 200;

    bytes = util_http_build_header(ptr, remaining, 0, 0, 200, NULL, source->format->contenttype, NULL, NULL, source);
    if (bytes == -1) {
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client_send_500(client, "Header generation failed.");
        return -1;
    } else if ((bytes + 1024) >= remaining) { /* we don't know yet how much to follow but want at least 1kB free space */
        void *new_ptr = realloc(ptr, bytes + 1024);
        if (new_ptr) {
            ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
            client->refbuf->data = ptr = new_ptr;
            client->refbuf->len = remaining = bytes + 1024;
            bytes = util_http_build_header(ptr, remaining, 0, 0, 200, NULL, source->format->contenttype, NULL, NULL, source);
            if (bytes == -1 ) {
                ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                client_send_500(client, "Header generation failed.");
                return -1;
            }
        } else {
            ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
            client_send_500(client, "Buffer reallocation failed.");
            return -1;
        }
    }

    remaining -= bytes;
    ptr += bytes;

    /* iterate through source http headers and send to client */
    avl_tree_rlock(source->parser->vars);
    node = avl_get_first(source->parser->vars);
    while (node)
    {
        int next = 1;
        http_var_t *var = (http_var_t *)node->key;
        bytes = 0;
        if (!strcasecmp(var->name, "ice-audio-info"))
        {
            /* convert ice-audio-info to icy-br */
            char *brfield = NULL;
            unsigned int bitrate;

            if (bitrate_filtered == 0)
                brfield = strstr(var->value, "bitrate=");
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
            if (strcasecmp(var->name, "ice-password") &&
                strcasecmp(var->name, "icy-metaint"))
            {
		if (!strcasecmp(var->name, "ice-name"))
		{
		    ice_config_t *config;
		    mount_proxy *mountinfo;

		    config = config_get_config();
		    mountinfo = config_find_mount (config, source->mount, MOUNT_TYPE_NORMAL);

		    if (mountinfo && mountinfo->stream_name)
		        bytes = snprintf (ptr, remaining, "icy-name:%s\r\n", mountinfo->stream_name);
                    else
		        bytes = snprintf (ptr, remaining, "icy-name:%s\r\n", var->value);

                    config_release_config();
		}
                else if (!strncasecmp("ice-", var->name, 4))
                {
                    if (!strcasecmp("ice-public", var->name))
                        bytes = snprintf (ptr, remaining, "icy-pub:%s\r\n", var->value);
                    else
                        if (!strcasecmp ("ice-bitrate", var->name))
                            bytes = snprintf (ptr, remaining, "icy-br:%s\r\n", var->value);
                        else
                            bytes = snprintf (ptr, remaining, "icy%s:%s\r\n",
                                    var->name + 3, var->value);
                }
                else
                    if (!strncasecmp("icy-", var->name, 4))
                    {
                        bytes = snprintf (ptr, remaining, "icy%s:%s\r\n",
                                var->name + 3, var->value);
                    }
            }
        }

        remaining -= bytes;
        ptr += bytes;
        if (next)
            node = avl_get_next(node);
    }
    avl_tree_unlock(source->parser->vars);

    bytes = snprintf (ptr, remaining, "\r\n");
    remaining -= bytes;
    ptr += bytes;

    client->refbuf->len -= remaining;
    if (source->format->create_client_data)
        if (source->format->create_client_data (source, client) < 0)
            return -1;
    return 0;
}


