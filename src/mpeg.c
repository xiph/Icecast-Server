/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2009-2010,  Karl Heyes <karl@xiph.org>
 */

/* mpeg.c
 *
 * routines to handle locating the frame sync markers for mpeg/1/2/3/aac streams.
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "compat.h"
#include "mpeg.h"
#include "format_mp3.h"
#include "global.h"

#define CATMODULE "mpeg"
#include "logging.h"

int aacp_sample_freq[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, -1, -1, -1, -1
};

int aacp_num_channels[] = {
    -1, 1, 2, 3, 4, 5, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1
};

int mpeg_samplerates [4][4] = {
    { 11025, 0, 22050, 44100},
    { 12000, 0, 24000, 48000 },
    {  8000, 0, 16000, 32000 },
    { 0,0,0 } };

#define LAYER_1     3
#define LAYER_2     2
#define LAYER_3     1


static int get_aac_frame_len (unsigned char *p)
{
    return ((p[3] & 0x3) << 11) + (p[4] << 3) + ((p[5] & 0xE0) >> 5);
}

static int handle_aac_frame (struct mpeg_sync *mp, unsigned char *p, int len)
{
    int frame_len = get_aac_frame_len (p);
    int blocks, header_len = 9;
    unsigned char *s = p;
    if (len - frame_len < 0)
        return 0;

    blocks = (p[6] & 0x3) + 1;
    if (p[1] & 0x1) // no crc
        header_len -= 2;
    s += header_len;
    mp->sample_count = (blocks * 1024);
    if (mp->raw)
    {
        unsigned raw_frame_len = frame_len - header_len;

        if (mp->frame_callback)
            if (mp->frame_callback (mp, s, raw_frame_len) < 0)
                return -1;
    }
    return frame_len;
}

static int get_mpegframe_samplerate (unsigned char *p)
{
    int ver = (p[1] & 0x18) >> 3;
    return mpeg_samplerates [(p[2]&0xC) >> 2][ver];
}

static int get_mpeg_bitrate (struct mpeg_sync *mp, unsigned char *p)
{
    int bitrate = -1;
    int bitrate_code = (p[2] & 0xF0) >> 4;

    if (mp->ver == 3) // MPEG1
    {
        static int bitrates [3][16] = {
            { 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, -1 },
            { 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 348, -1 },
            { 0, 32, 54, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1 } };
        if (mp->layer != 0)
            bitrate = bitrates [mp->layer-1][bitrate_code];
    }
    else // MPEG2/2.5
    {
        static int bitrates [2][16] = { 
            { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, -1 },
            { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1 } };
        if (mp->layer == 3)
            bitrate = bitrates [1][bitrate_code];
        else
            bitrate = bitrates [0][bitrate_code];
    }
    return bitrate;
}


static int get_samples_per_mpegframe(int version, int layer)
{
    int samples_per_frame [4][4] = {
        { -1,  576, 1152, 384 },    /* v2.5 - L3, L2, L1 */
        { -1,   -1,   -1,  -1 },
        { -1,  576, 1152, 384 },    /* v2 - L3, L2, L1 */
        { -1, 1152, 1152, 576 }     /* v1 - L3, L2, L1 */
    };
    return samples_per_frame [version] [layer];
}


static int get_mpeg_frame_length (struct mpeg_sync *mp, unsigned char *p)
{
    int padding = (p[2] & 0x2) >> 1;
    int frame_len = 0;

    int64_t bitrate = get_mpeg_bitrate (mp, p);
    int samples = get_samples_per_mpegframe (mp->ver, mp->layer);
    int samplerate = get_mpegframe_samplerate (p);

    if (samplerate == 0 || (mp->samplerate && mp->samplerate != samplerate))
        return -1;
    mp->sample_count = samples;
    if (bitrate > 0 && samples > 0)
    {
        bitrate *= 1000;
        if (mp->layer == LAYER_1)
        {
            frame_len = (int)(12 * bitrate / samplerate + padding) * 4; // ??
        }
        else
        {
            frame_len = (int)(samples / 8 * bitrate / samplerate + padding);
        }
    }
    return frame_len;
}


static int handle_mpeg_frame (struct mpeg_sync *mp, unsigned char *p, int remaining)
{
    int frame_len = get_mpeg_frame_length (mp, p);

    if (frame_len <= 0)
        return -1;
    if (remaining - frame_len < 0)
        return 0;
    if (mp->raw)
    {
        if (mp->frame_callback)
            if (mp->frame_callback (mp, p, frame_len) < 0)
                return -1;
    }
    return frame_len;
}

static int handle_ts_frame (struct mpeg_sync *mp, unsigned char *p, int remaining)
{
    int frame_len = mp->raw_offset;

    if (remaining - frame_len < 0)
        return 0;
    return frame_len;
}


/* return -1 for no valid frame at this specified address, 0 for more data needed */
static int check_for_aac (struct mpeg_sync *mp, unsigned char *p, unsigned remaining)
{
    //nocrc = p[1] & 0x1;
    if (mp->layer == 0 && (p[1] >= 0xF0))
    {
        int samplerate_idx = (p[2] & 0x3C) >> 2,
            v = (p[2] << 8) + p[3],
            channels_idx = (v & 0x1C0) >> 6;
        int id =  p[1] & 0x8;
        int checking = mp->check_numframes;
        unsigned char *fh = p;

        while (checking)
        {
            //DEBUG1 ("checking frame %d", 5-checking);
            int frame_len = get_aac_frame_len (fh);
            if (frame_len <= 0 || frame_len > 8192)
                return -1;
            if (frame_len+5 >= remaining)
                return 0;
            if (fh[frame_len] != 255 || fh[frame_len+1] != p[1] || fh[frame_len+2] != p[2]
                    || (fh[frame_len+3]&0xF0) != (p[3]&0xF0))
                return -1;
            remaining -= frame_len;
            fh += frame_len;
            checking--;
        }
        // profile = p[1] & 0xC0;
        mp->samplerate = aacp_sample_freq [samplerate_idx];
        mp->channels = aacp_num_channels [channels_idx];
        if (mp->samplerate == -1 || mp->channels == -1)
        {
            DEBUG0 ("ADTS samplerate/channel setting invalid");
            return -1;
        }
        mp->syncbytes = 3;
        memcpy (&mp->fixed_headerbits[0], p, 3);
        if (mp->check_numframes > 1)
            INFO4 ("detected AAC MPEG-%s, rate %d, channels %d on %s", id ? "2" : "4", mp->samplerate, mp->channels, mp->mount);
        mp->process_frame = handle_aac_frame;
        return 1;
    }
    return -1;
}

static int check_for_mp3 (struct mpeg_sync *mp, unsigned char *p, unsigned remaining)
{
    if (mp->layer && (p[1] >= 0xE0))
    {
        const char *version[] = { "MPEG 2.5", NULL, "MPEG 2", "MPEG 1" };
        const char *layer[] =   { NULL, "Layer 3", "Layer 2", "Layer 1" };
        mp->ver = (p[1] & 0x18) >> 3;
        if (mp->layer && version [mp->ver] && layer[mp->layer]) 
        {
            int checking = mp->check_numframes, samplerate;
            unsigned char *fh = p;
            char stream_type[20];

            // au.crc = (p[1] & 0x1) == 0;
            samplerate = get_mpegframe_samplerate (p);
            if (samplerate == 0)
                return -1;
            do
            {
                int frame_len;

                if (remaining <= 4)
                    return 0;
                if (fh [0] != 255 || fh [1] != p[1])
                    return -1;
                frame_len = get_mpeg_frame_length (mp, fh);
                if (frame_len <= 0 || frame_len > 3000)
                {
                    //DEBUG2 ("checking frame %d, but len %d invalid", 5-checking, frame_len);
                    return -1;
                }
                if (frame_len > remaining)
                {
                    //DEBUG3 ("checking frame %d, but need more data (%d,%d)", 5-checking, frame_len, remaining);
                    return 0;
                }
                if (samplerate != get_mpegframe_samplerate (fh))
                    return -1;
                //DEBUG4 ("frame %d checked, next header codes are %x %x %x", 5-checking, fh[frame_len], fh[frame_len+1], fh[frame_len+2]);
                remaining -= frame_len;
                fh += frame_len;
            } while (--checking);
            mp->samplerate = samplerate;
            if  (((p[3] & 0xC0) >> 6) == 3)
                mp->channels = 1;
            else
                mp->channels = 2;
            mp->syncbytes = 2;
            memcpy (&mp->fixed_headerbits[0], p, 2);
            snprintf (stream_type, sizeof (stream_type), "%s %s", version [mp->ver], layer[mp->layer]);
            if (mp->check_numframes > 1)
                INFO4 ("%s detected (%d, %d) on %s", stream_type, mp->samplerate, mp->channels, mp->mount);
            mp->process_frame = handle_mpeg_frame;
            return 1;
        }
    }
    return -1;
}

static int check_for_ts (struct mpeg_sync *mp, unsigned char *p, unsigned remaining)
{
    int pkt_len = 188, checking;
    do
    {
        int offset = 0;
        checking = 4;
        while (checking)
        {
            if (offset > remaining) return 0;
            if (p [offset] != 0x47)
            {
                switch (pkt_len) {
                    case 204: pkt_len = 208; break;
                    case 188: pkt_len = 204; break;
                    default:  return -1;
                }
                break;
            }
            //DEBUG2 ("found 0x37 checking %d (%d)", checking, pkt_len);
            offset += pkt_len;
            checking--;
        }
    } while (checking);
    INFO2 ("detected TS (%d) on %s", pkt_len, mp->mount);
    mp->process_frame = handle_ts_frame;
    mp->syncbytes = 1;
    mp->fixed_headerbits [0] = 0x47;
    mp->raw_offset = pkt_len;
    return 1;
}


/* return -1 for no valid frame at this specified address, 0 for more data needed */
static int get_initial_frame (struct mpeg_sync *mp, unsigned char *p, unsigned remaining)
{
    int ret;

    if (p[0] == 0x47)
        return check_for_ts (mp, p, remaining);
    if (p[1] < 0xE0)
        return -1;
    mp->layer = (p[1] & 0x6) >> 1;
    ret = check_for_aac (mp, p, remaining);
    if (ret < 0)
        ret = check_for_mp3 (mp, p, remaining);
    if (ret > 0) mp->resync_count = 0;
    return ret;
}

/* return number from 1 to remaining */
static int find_align_sync (mpeg_sync *mp, unsigned char *start, int remaining)
{
    int skip = 0;
    unsigned char *p = start;
    if (mp->syncbytes)
        p = memchr (start, mp->fixed_headerbits[0], remaining);
    else
    {
        int offset = remaining;
        for (; offset && *p != 0xFF && *p != 0x47; offset--)
            p++;
        if (offset == 0) p = NULL;
    }
    if (p)
    {
        skip = p - start;
        memmove (start, p, remaining - skip);
    }
    return skip;
}

static int is_sync_byte (mpeg_sync *mp, unsigned char *p)
{
    if (mp->syncbytes)
        return *p == mp->fixed_headerbits[0];
    return *p == 0xFF || *p == 0x47;
}


int mpeg_complete_frames (mpeg_sync *mp, refbuf_t *new_block, unsigned offset)
{
    unsigned char *start, *end;
    int remaining, frame_len = 0, completed = 0;

    if (mp == NULL)
        return 0;  /* leave as-is */
    
    mp->sample_count = 0;
    if (mp->surplus)
    {
        if (offset >= mp->surplus->len)
            offset -= mp->surplus->len;
        else
        {
            int new_len = mp->surplus->len + new_block->len;
            unsigned char *p = realloc (mp->surplus->data, new_len);

            memcpy (p+mp->surplus->len, new_block->data, new_block->len);
            mp->surplus->data = new_block->data;
            new_block->data = (void*)p;
            new_block->len = new_len;
        }
        refbuf_release (mp->surplus);
        mp->surplus = NULL;
    }
    start = (unsigned char *)new_block->data + offset;
    remaining = new_block->len - offset;
    while (1)
    {
        end = (unsigned char*)new_block->data + new_block->len;
        remaining = end - start;
        //DEBUG2 ("block size %d, remaining now %d", new_block->len, remaining);
        if (remaining < 10) /* make sure we have some bytes to check */
            break;
        if (!is_sync_byte (mp, start))
        {
            int ret = find_align_sync (mp, start, remaining);
            if (ret == 0)
                break; // no sync in the rest, so dump it
            DEBUG2 ("no frame sync, re-checking after skipping %d (%d)", ret, remaining);
            new_block->len -= ret;
            mp->resync_count++;
            mp->syncbytes = 0; /* force an initial recheck */
            continue;
        }
        if (mp->syncbytes == 0)
        {
            int ret = get_initial_frame (mp, start, remaining);
            if (ret < 0)
            {
                // failed to detect a complete frame, try again
                memmove (start, start+1, remaining-1);
                new_block->len--;
                continue;
            }
            if (ret == 0)
            {
                if (remaining > 20000)
                    return -1;
                new_block->len = offset;
                return remaining;
            }
        }
        if (memcmp (start, &mp->fixed_headerbits[0], mp->syncbytes) != 0)
        {
            memmove (start, start+1, remaining-1);
            new_block->len--;
            continue;
        }
        frame_len = mp->process_frame (mp, start, remaining);
        //DEBUG2 ("seen frame of %d (%d) bytes", frame_len, remaining);
        if (frame_len <= 0)  // frame fragment at the end
            break;
        start += frame_len;
        completed++;
    }
    if (remaining < 0 || remaining > new_block->len)
    {
        ERROR2 ("block inconsistency (%d, %d)", remaining, new_block->len);
        abort();
    }
    new_block->len -= remaining;
    return remaining;
}


void mpeg_data_insert (mpeg_sync *mp, refbuf_t *inserted)
{
    if (mp)
        mp->surplus = inserted;
}

void mpeg_setup (mpeg_sync *mpsync, const char *mount)
{
    memset (mpsync, 0, sizeof (mpeg_sync));
    mpsync->check_numframes = 4;
    mpsync->mount = mount;
}

void mpeg_check_numframes (mpeg_sync *mpsync, unsigned count)
{
    if (count && count < 100)
        mpsync->check_numframes = count;
}

void mpeg_cleanup (mpeg_sync *mpsync)
{
    if (mpsync)
    {
        refbuf_release (mpsync->surplus);
        refbuf_release (mpsync->raw);
        mpsync->mount = NULL;
    }
}
