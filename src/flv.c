/* -*- c-basic-offset: 4; -*- */
/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2009-2010,     Karl Heyes <karl@xiph.org>
 */

/* flv.c
 *
 * routines for processing an flv container
 *
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
#include "client.h"
#include "stats.h"

#include "flv.h"
#include "logging.h"
#include "mpeg.h"
#include "format_mp3.h"
#include "global.h"

#define CATMODULE "flv"


struct flvmeta
{
    int meta_pos;
    int arraylen;
};

#define FLVHEADER       11


static void flv_hdr (struct flv *flv, unsigned int len)
{
    long  v = flv->prev_tagsize;

    v = flv->prev_tagsize;
    flv->tag [3] = v & 0xFF;
    v >>= 8;
    flv->tag [2] = v & 0xFF;
    v >>= 8;
    flv->tag [1] = v & 0xFF; // assume less than 2^24 

    v = (long)len;
    flv->tag [7] = (unsigned char)(v & 0xFF);
    v >>= 8;
    flv->tag [6] = (unsigned char)(v & 0xFF);
    v >>= 8;
    flv->tag [5] = (unsigned char)(v & 0xFF);

    v = (long)flv->prev_ms;
    flv->tag [10] = (unsigned char)(v & 0xFF);
    v >>= 8;
    flv->tag [9] = (unsigned char)(v & 0xFF);
    v >>= 8;
    flv->tag [8] = (unsigned char)(v & 0xFF);
    v >>= 8;
    flv->tag [11] = (unsigned char)(v & 0xFF);

}

/* Here we append to the scratch buffer each mp3 type frame. This frame includes the
 * header so with the flv header as well it becomes fairly wasteful but that is what
 * works.
 */
static int flv_mpX_hdr (struct mpeg_sync *mp, unsigned char *frame, unsigned int len)
{
    struct flv *flv = mp->callback_key;

    if (mp->raw_offset + len + 16 > mp->raw->len)
    {
        int newlen = mp->raw->len + 4096;
        void *p = realloc (mp->raw->data, newlen);
        if (p == NULL)
            return -1;
        mp->raw->data = p;
        mp->raw->len = newlen;
    }
    flv_hdr (flv, len + 1);
    if (flv->tag[15] == 0x22)
    {
        /* it is unclear what the flv flags are for other samplerates */
        switch (mp->samplerate)
        {
            case 11025: flv->tag[15] |= (1<<2); break;
            case 22050: flv->tag[15] |= (2<<2); break;
            default:    flv->tag[15] |= (3<<2); break;
        }
        if (mp->channels == 2)
            flv->tag[15] |= 0x1;
    }
    memcpy (mp->raw->data + mp->raw_offset, &flv->tag[0], 16);
    connection_bufs_append (&flv->bufs, mp->raw->data + mp->raw_offset, 16);
    flv->samples += mp->sample_count;
    flv->prev_ms = (int64_t)(flv->samples / (mp->samplerate/1000.0));
    // The extra byte is for the flv audio id, usually 0x2F 
    flv->prev_tagsize = (len + FLVHEADER + 1);
    mp->raw_offset += 16;
    connection_bufs_append (&flv->bufs, frame, len);
    return 0;
}

/* Here we append to the scratch buffer each aac headerless frame. The flv tag data size
 * will be 2 bytes more than the frame for codes 0xAF and 0x1
 */
static int flv_aac_hdr (struct mpeg_sync *mp, unsigned char *frame, unsigned int len)
{
    struct flv *flv = mp->callback_key;

    if (mp->raw_offset + len + 17 > mp->raw->len)
    {
        int newlen = mp->raw->len + 4096;
        void *p = realloc (mp->raw->data, newlen);
        if (p == NULL)
            return -1;
        mp->raw->data = p;
        mp->raw->len = newlen;
    }
    flv_hdr (flv, len + 2);
    // a single frame (headerless) follows this
    memcpy (mp->raw->data + mp->raw_offset, &flv->tag[0], 17);
    connection_bufs_append (&flv->bufs, mp->raw->data + mp->raw_offset, 17);
    flv->samples += mp->sample_count;
    flv->prev_ms = (int64_t)(flv->samples / (mp->samplerate/1000.0));
    // frame length + FLVHEADER + AVHEADER
    flv->prev_tagsize = (len + 11 + 2);
    mp->raw_offset += 17;
    connection_bufs_append (&flv->bufs, frame, len);
    return 0;
}


/* Simple function for generating codes for commonly used streams
 * players require this before they start playback. This is typically 2 bytes but can be more
 * 5 bits - codec details, we assume AAC LC here
 * 4 bits - samplerate table index.
 * 4 bits - channels table index.
 *
 */
static int  audio_specific_config (mpeg_sync *mp, unsigned char *p)
{
    int rates [] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0 };
    unsigned char count = 0;
    for (count = 0; rates[count] != mp->samplerate; count++)
    {
        if (rates [count] == 0)
            return 0;
    }
    p[0] = ((2 << 3) | (count >> 1));
    p[1] = ((unsigned char)(count << 7) | (mp->channels << 3));
    return 2;
}

static int flv_aac_firsthdr (struct mpeg_sync *mp, unsigned char *frame, unsigned int len)
{
    struct flv *flv = mp->callback_key;
    int c = audio_specific_config (&flv->mpeg_sync, &flv->tag[17]);

    flv_hdr (flv, 2+c);
    flv->tag[15] = 0xAF; // AAC audio, need these codes first
    flv->tag[16] = 0x0;
    memcpy (mp->raw->data, &flv->tag[0], 11+4+2+c);
    connection_bufs_append (&flv->bufs, mp->raw->data, 11+4+2+c);
    mp->raw_offset = 11+4+2+c;
    flv->prev_tagsize = 11 + 2 + c;
    flv->tag[16] = 0x01;   // as per spec. headerless frame follows this
    flv->mpeg_sync.frame_callback = flv_aac_hdr;
    // DEBUG2 ("codes for audiospecificconfig are %x, %x", flv->tag[17], flv->tag[18]);

    return flv_aac_hdr (mp, frame, len);
}


static int send_flv_buffer (client_t *client, struct flv *flv)
{
    int ret = 0;
    unsigned int len = flv->bufs.total - flv->block_pos;

    if (len > 0)
    {
        ret = connection_bufs_send (&client->connection, &flv->bufs, flv->block_pos);
        if (ret < (int)len)
            client->schedule_ms += (ret > 0 ? 50 : 200);
        if (ret > 0)
            flv->block_pos += ret;
    }
    if (flv->block_pos == flv->bufs.total)
    {
        flv->block_pos = flv->mpeg_sync.raw_offset = 0;
        connection_bufs_flush (&flv->bufs);
    }
    return ret;
}


void flv_write_metadata (struct flv *flv, refbuf_t *scmeta, const char *mount)
{
    /* the first assoc block is shoutcast meta, second is flv meta */
    int len;
    struct flvmeta *flvm;
    unsigned char prev_type = flv->tag[4];
    refbuf_t *flvmeta = NULL;
    int meta_copied = 0;
    refbuf_t *raw = flv->mpeg_sync.raw;
    char *src, *dst = raw->data + flv->mpeg_sync.raw_offset;

    if (scmeta)
        flvmeta = scmeta->associated;
    if (flvmeta == NULL)
    {
        char *value = stats_get_value (mount, "server_name");

        flvmeta  = flv_meta_allocate (200);
        if (value)
            flv_meta_append_string (flvmeta, "name", value);
        free (value);
        value = stats_get_value (flv->mpeg_sync.mount, "title");
        if (value)
            flv_meta_append_string (flvmeta, "title", value);
        else
            flv_meta_append_string (flvmeta, "title", "");
        free (value);
        value = stats_get_value (mount, "audio_codecid");
        if (value)
        {
            int id = atoi (value);
            if (id == 2 || id == 10)
                flv_meta_append_number (flvmeta, "audiocodecid", (double)id);
            free (value);
        }
        value = stats_get_value (mount, "ice-bitrate");
        if (value)
        {
            double rate = (double)atoi (value);
            flv_meta_append_number (flvmeta, "audiodatarate", rate);
            free (value);
        }
        value = stats_get_value (mount, "ice-samplerate");
        if (value)
        {
            double rate = (double)atoi (value);
            flv_meta_append_number (flvmeta, "audiosamplerate", rate);
            free (value);
        }
        value = stats_get_value (mount, "ice-channels");
        if (value)
        {
            int chann = atoi (value);
            flv_meta_append_bool (flvmeta, "stereo", chann == 2 ? 1 : 0);
            free (value);
        }
        flv_meta_append_string (flvmeta, NULL, NULL);
        flvm = (struct flvmeta *)flvmeta->data;
        meta_copied  = flvm->meta_pos - sizeof (*flvm);
    }
    else
        flvm = (struct flvmeta *)flvmeta->data;
    len  = flvm->meta_pos - sizeof (*flvm);
    src = (char *)flvm + sizeof (*flvm);

    if (meta_copied + 15 > raw->len)
    {
        int newlen = raw->len + 1024;
        void *p = realloc (raw->data, newlen);
        if (p == NULL)
            return;
        raw->data = p;
        raw->len = newlen;
    }
    flv->tag[4] = 18;    // metadata
    flv_hdr (flv, len);
    memcpy (dst, &flv->tag[0], 15);
    connection_bufs_append (&flv->bufs, dst, 15);
    flv->mpeg_sync.raw_offset += 15;
    dst += 15;
    if (meta_copied)
    {
        memcpy (dst, src, len);
        connection_bufs_append (&flv->bufs, dst, len);
        flv->mpeg_sync.raw_offset += len;
        refbuf_release (flvmeta);
    }
    else
        connection_bufs_append (&flv->bufs, src, len);
    flv->prev_tagsize = len + 11;
    flv->tag[4] = prev_type;
}


int write_flv_buf_to_client (client_t *client) 
{
    refbuf_t *ref = client->refbuf, *scmeta = ref->associated;
    mp3_client_data *client_mp3 = client->format_data;
    struct flv *flv = client_mp3->specific;
    int ret;

    if (client->pos >= ref->len)
    {
        WARN2 ("buffer position invalid (%d, %d)", client->pos, ref->len);
        client->pos = ref->len;
        return -1;
    }

    /* check for metadata updates and insert if needed */
    if (flv->mpeg_sync.raw_offset == 0)
    {
        int unprocessed = mpeg_complete_frames (&flv->mpeg_sync, ref, client->pos);

        if (unprocessed < 0)
            return -1;
        if (unprocessed > 0)
            ref->len += unprocessed;   /* output was truncated, so revert changes */

        if (flv->seen_metadata != scmeta)
            flv_write_metadata (flv, scmeta, client->mount);
    }
    ret = send_flv_buffer (client, flv);
    if (flv->mpeg_sync.raw_offset == 0)
    {
        client->pos = ref->len;
        client->queue_pos += client->refbuf->len;
        if (flv->seen_metadata != scmeta)
            flv->seen_metadata = scmeta;
    }
    return ret;
}


void flv_write_BE64 (void *where, void *val)
{
    int len = sizeof (uint64_t);
    int diff = 1;
    unsigned char *src = val, *dst = where;

    if (*((unsigned char *)&len)) // little endian?
    {
        diff = -1;
        src += sizeof (uint64_t) - 1;
    }
    for (len = sizeof (uint64_t); len; len--, dst++)
    {
        *dst = *src;
        src += diff;
    }
}


void flv_write_UI16 (unsigned char *p, unsigned val)
{
    p[1] = val & 0xFF;
    val >>= 8;
    p[0] = val & 0xFF;
}


refbuf_t *flv_meta_allocate (size_t len)
{
    char *ptr;
    refbuf_t *buffer = refbuf_new (sizeof (struct flvmeta) + len + 30);
    struct flvmeta *flvm = (struct flvmeta *)buffer->data;

    memset (flvm, 0, sizeof (struct flvmeta));
    ptr = buffer->data + sizeof (struct flvmeta);
    memcpy (ptr, "\002\000\012onMetaData", 13);
    memcpy (ptr+13, "\010\000\000\000\000", 5);
    flvm->meta_pos = 18;
    flvm->arraylen = 0;
    flvm->meta_pos += sizeof (struct flvmeta);
    return buffer;
}


static int flv_meta_increase (refbuf_t *buffer, int taglen, int valuelen)
{
    struct flvmeta *flvm = (struct flvmeta *)buffer->data;
    unsigned char *array_size_loc = (unsigned char *)buffer->data + sizeof (*flvm) + 16;

    if (taglen + valuelen + 3 + flvm->meta_pos > buffer->len - 3)
        taglen = 0; // force end of the metadata
    if (taglen == 0)
    {
        DEBUG1 ("%d array elements", flvm->arraylen);
        memcpy (buffer->data+flvm->meta_pos, "\000\000\011", 3);
        flvm->meta_pos += 3;
        return -1;
    }
    flvm->arraylen++;
    flv_write_UI16 (array_size_loc, flvm->arraylen); // over 64k tags not handled
    flvm->meta_pos += (2 + taglen + 1 + valuelen);
    return 0;
}


void flv_meta_append_bool (refbuf_t *buffer, const char *tag, int value)
{
    int taglen = tag ? strlen (tag) : 0;
    struct flvmeta *flvm = (struct flvmeta *)buffer->data;
    unsigned char *ptr = (unsigned char *)buffer->data + flvm->meta_pos;

    if (flv_meta_increase (buffer, taglen, 1) < 0)
        return;

    flv_write_UI16 (ptr, taglen);
    memcpy (ptr+2, tag, taglen);
    ptr += (taglen + 2);
    *ptr = 0x01; // a boolean as UI8
    ptr[1] = value & 0xFF;
}


void flv_meta_append_number (refbuf_t *buffer, const char *tag, double value)
{
    int taglen = 0;
    struct flvmeta *flvm = (struct flvmeta *)buffer->data;
    unsigned char *ptr = (unsigned char *)buffer->data + flvm->meta_pos;

    if (tag)   taglen = strlen (tag);
    if (flv_meta_increase (buffer, taglen, 8) < 0)
        return;

    flv_write_UI16 (ptr, taglen);
    memcpy (ptr+2, tag, taglen);
    ptr += (taglen + 2);
    *ptr = 0x00; // a number
    ptr++;
    flv_write_BE64 (ptr, &value);
    // DEBUG2 ("Appending %s number %g", tag, value);
}


void flv_meta_append_string (refbuf_t *buffer, const char *tag, const char *value)
{
    int taglen = 0, valuelen = 0;
    struct flvmeta *flvm = (struct flvmeta *)buffer->data;
    unsigned char *ptr = (unsigned char *)buffer->data + flvm->meta_pos;

    if (tag)   taglen = strlen (tag);
    if (value) valuelen = strlen (value);

    if (flv_meta_increase (buffer, taglen, valuelen+2) < 0)
        return;

    flv_write_UI16 (ptr, taglen);
    memcpy (ptr+2, tag, taglen);
    ptr += (taglen + 2);
    *ptr = 0x02; // a text string
    ptr++;
    flv_write_UI16 (ptr, valuelen);
    memcpy (ptr+2, value, valuelen);
    // DEBUG2 ("Appending %s string %s", tag, value);
}


void flv_create_client_data (format_plugin_t *plugin, client_t *client)
{
    mp3_client_data *client_mp3 = client->format_data;
    struct flv *flv = calloc (1, sizeof (struct flv));
    int bytes;
    char *ptr = client->refbuf->data;

    mpeg_setup (&flv->mpeg_sync, client->connection.ip);
    mpeg_check_numframes (&flv->mpeg_sync, 1); 
    client_mp3->specific = flv;

    bytes = snprintf (ptr, 200, "HTTP/1.0 200 OK\r\n"
            "content-type: video/x-flv\r\n"
            "Cache-Control: no-cache\r\n"
            "Expires: Thu, 01 Jan 1970 00:00:01 GMT\r\n"
            "Pragma: no-cache\r\n"
            "\r\n"
            "FLV\x1\x4%c%c%c\x9", 0,0,0);

    // only flv headers in here, allows for up to 64 frames per read block, expandable
    flv->mpeg_sync.raw = refbuf_new (1024);
    flv->tag[4] = 8;    // Audio details only
    if (plugin->type == FORMAT_TYPE_AAC)
    {
        flv->mpeg_sync.frame_callback = flv_aac_firsthdr;
    }
    if (plugin->type == FORMAT_TYPE_MPEG)
    {
        flv->tag[15] = 0x22; // MP3, specific settings not available yet
        flv->mpeg_sync.frame_callback = flv_mpX_hdr;
    }
    flv->mpeg_sync.callback_key = flv;
    flv->seen_metadata = (void*)flv; // force metadata initially with non-NULL meta

    client->respcode = 200;
    client->refbuf->len = bytes;
    connection_bufs_init (&flv->bufs, 10);
}


void free_flv_client_data (struct flv *flv)
{
    flv->mpeg_sync.mount = NULL;
    mpeg_cleanup (&flv->mpeg_sync);
    connection_bufs_release (&flv->bufs);
}

