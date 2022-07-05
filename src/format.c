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
 * Copyright 2012-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
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

#include <vorbis/codec.h>

#include "connection.h"
#include "refbuf.h"

#include "source.h"
#include "format.h"
#include "global.h"

#include "format_ogg.h"
#include "format_mp3.h"
#include "format_ebml.h"
#include "format_text.h"

#include "logging.h"
#include "stats.h"
#define CATMODULE "format"

static int format_prepare_headers (source_t *source, client_t *client);


format_type_t format_get_type (const char *contenttype)
{
    char buf[32];
    const char *separator = strpbrk(contenttype, "; \t");

    if (separator && (size_t)(separator - contenttype) < sizeof(buf)) {
        memcpy(buf, contenttype, separator - contenttype);
        buf[separator - contenttype] = 0;
        contenttype = buf;
    }

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
    else if(strcmp(contenttype, "text/plain") == 0)
        return FORMAT_TYPE_TEXT;
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
            ret = format_ogg_get_plugin(source);
        break;
        case FORMAT_TYPE_EBML:
            ret = format_ebml_get_plugin(source);
        break;
        case FORMAT_TYPE_TEXT:
            ret = format_text_get_plugin(source);
        break;
        case FORMAT_TYPE_GENERIC:
            ret = format_mp3_get_plugin(source);
        break;
        default:
        break;
    }

    return ret;
}


/* clients need to be start from somewhere in the queue so we will look for
 * a refbuf which has been previously marked as a sync point.
 */
static void find_client_start(source_t *source, client_t *client)
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


static int get_file_data(FILE *intro, client_t *client)
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
int format_check_http_buffer(source_t *source, client_t *client)
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
        stats_event_inc(NULL, "listeners");
        stats_event_inc(NULL, "listener_connections");
        stats_event_inc(source->mount, "listener_connections");
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


int format_generic_write_to_client(client_t *client)
{
    refbuf_t *refbuf = client->refbuf;
    int ret;
    const char *buf = refbuf->data + client->pos;
    unsigned int len = refbuf->len - client->pos;

    ret = client_send_bytes(client, buf, len);

    if (ret > 0)
        client->pos += ret;

    return ret;
}


/* This is the commonly used for source streams, here we just progress to
 * the next buffer in the queue if there is no more left to be written from
 * the existing buffer.
 */
int format_advance_queue(source_t *source, client_t *client)
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


/* Prepare headers
 * If any error occurs in this function, return -1
 * Do not send a error to the client using client_send_error
 * here but instead set client->respcode to 500.
 * Else client_send_error will destroy and free the client and all
 * calling functions will use a already freed client struct and
 * cause a segfault!
 */
static inline ssize_t __print_var(char *str, size_t remaining, const char *format, const char *first, const http_var_t *var)
{
    size_t i;
    ssize_t done = 0;
    int ret;

    for (i = 0; i < var->values; i++) {
        ret = snprintf(str + done, remaining - done, format, first, var->value[i]);
        if (ret <= 0 || (size_t)ret >= (remaining - done))
            return -1;

        done += ret;
    }

    return done;
}

static inline const char *__find_bitrate(const http_var_t *var)
{
    size_t i;
    const char *ret;

    for (i = 0; i < var->values; i++) {
        ret = strstr(var->value[i], "bitrate=");
        if (ret)
            return ret;
    }

    return NULL;
}

static int format_prepare_headers (source_t *source, client_t *client)
{
    size_t remaining;
    char *ptr;
    int bytes;
    int bitrate_filtered = 0;
    avl_node *node;

    remaining = client->refbuf->len;
    ptr = client->refbuf->data;
    client->respcode = 200;

    bytes = util_http_build_header(ptr, remaining, 0, 0, 200, NULL, source->format->contenttype, NULL, NULL, source, client);
    if (bytes <= 0) {
        ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
        client->respcode = 500;
        return -1;
    } else if (((size_t)bytes + (size_t)1024U) >= remaining) { /* we don't know yet how much to follow but want at least 1kB free space */
        void *new_ptr = realloc(ptr, bytes + 1024);
        if (new_ptr) {
            ICECAST_LOG_DEBUG("Client buffer reallocation succeeded.");
            client->refbuf->data = ptr = new_ptr;
            client->refbuf->len = remaining = bytes + 1024;
            bytes = util_http_build_header(ptr, remaining, 0, 0, 200, NULL, source->format->contenttype, NULL, NULL, source, client);
            if (bytes <= 0 || (size_t)bytes >= remaining) {
                ICECAST_LOG_ERROR("Dropping client as we can not build response headers.");
                client->respcode = 500;
                return -1;
            }
        } else {
            ICECAST_LOG_ERROR("Client buffer reallocation failed. Dropping client.");
            client->respcode = 500;
            return -1;
        }
    }

    if (bytes <= 0 || (size_t)bytes >= remaining) {
        ICECAST_LOG_ERROR("Can not allocate headers for client %p", client);
        client->respcode = 500;
        return -1;
    }
    remaining -= bytes;
    ptr += bytes;

    /* iterate through source http headers and send to client */
    avl_tree_rlock(source->parser->vars);
    node = avl_get_first(source->parser->vars);
    while (node)
    {
        int next = 1;
        http_var_t *var = (http_var_t *) node->key;
        bytes = 0;
        if (!strcasecmp(var->name, "ice-audio-info"))
        {
            /* convert ice-audio-info to icy-br */
            const char *brfield = NULL;
            unsigned int bitrate;

            if (bitrate_filtered == 0)
                brfield = __find_bitrate(var);
            if (brfield && sscanf (brfield, "bitrate=%u", &bitrate))
            {
                bytes = snprintf (ptr, remaining, "icy-br:%u\r\n", bitrate);
                next = 0;
                bitrate_filtered = 1;
            }
            else
                /* show ice-audio_info header as well because of relays */
                bytes = __print_var(ptr, remaining, "%s: %s\r\n", var->name, var);
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
                        bytes = __print_var(ptr, remaining, "icy-%s:%s\r\n", "name", var);

                    config_release_config();
                }
                else if (!strncasecmp("ice-", var->name, 4))
                {
                    if (!strcasecmp("ice-public", var->name))
                        bytes = __print_var(ptr, remaining, "icy-%s:%s\r\n", "pub", var);
                    else
                        if (!strcasecmp ("ice-bitrate", var->name))
                            bytes = __print_var(ptr, remaining, "icy-%s:%s\r\n", "br", var);
                        else
                            bytes = __print_var(ptr, remaining, "icy%s:%s\r\n", var->name + 3, var);
                }
                else
                    if (!strncasecmp("icy-", var->name, 4))
                    {
                        bytes = __print_var(ptr, remaining, "icy%s:%s\r\n", var->name + 3, var);
                    }
            }
        }

        if (bytes < 0 || (size_t)bytes >= remaining) {
            avl_tree_unlock(source->parser->vars);
            ICECAST_LOG_ERROR("Can not allocate headers for client %p", client);
            client->respcode = 500;
            return -1;
        }

        remaining -= bytes;
        ptr += bytes;
        if (next)
            node = avl_get_next(node);
    }
    avl_tree_unlock(source->parser->vars);

    bytes = snprintf(ptr, remaining, "\r\n");
    if (bytes <= 0 || (size_t)bytes >= remaining) {
        ICECAST_LOG_ERROR("Can not allocate headers for client %p", client);
        client->respcode = 500;
        return -1;
    }
    remaining -= bytes;
    ptr += bytes;

    client->refbuf->len -= remaining;
    if (source->format->create_client_data)
        if (source->format->create_client_data (source, client) < 0) {
            ICECAST_LOG_ERROR("Client format header generation failed. "
                "(Likely not enough or wrong source data) Dropping client.");
            client->respcode = 500;
            return -1;
        }
    return 0;
}

void format_set_vorbiscomment(format_plugin_t *plugin, const char *tag, const char *value) {
    if (vorbis_comment_query_count(&plugin->vc, tag) != 0) {
        /* delete key */
        /* as libvorbis hides away all the memory functions we need to copy
         * the structure comment by comment. sorry about that...
         */
        vorbis_comment vc;
        int i; /* why does vorbis_comment use int, not size_t? */
        size_t keylen = strlen(tag);

        vorbis_comment_init(&vc);
        /* copy tags */
        for (i = 0; i < plugin->vc.comments; i++) {
            if (strncasecmp(plugin->vc.user_comments[i], tag, keylen) == 0 && plugin->vc.user_comments[i][keylen] == '=')
                continue;
            vorbis_comment_add(&vc, plugin->vc.user_comments[i]);
        }
        /* move vendor */
        vc.vendor = plugin->vc.vendor;
        plugin->vc.vendor = NULL;
        vorbis_comment_clear(&plugin->vc);
        plugin->vc = vc;
    }
    vorbis_comment_add_tag(&plugin->vc, tag, value);
}
