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
#ifndef __MPEG_SYNC_H
#define __MPEG_SYNC_H

#include "refbuf.h"

typedef struct mpeg_sync
{
    unsigned char fixed_headerbits[3];
    char syncbytes;
    int (*process_frame) (struct mpeg_sync *mp, unsigned char *p, int len);
    refbuf_t *surplus;
    long sample_count;
    long resync_count;
    void *callback_key;
    int (*frame_callback)(struct mpeg_sync *mp, unsigned char *p, unsigned int len);
    refbuf_t *raw;
    int raw_offset;
    int ver;
    int layer;
    int samplerate;
    int channels;
    const char *mount;
} mpeg_sync;

void mpeg_setup (mpeg_sync *mpsync, const char *mount);
void mpeg_cleanup (mpeg_sync *mpsync);

int  mpeg_complete_frames (mpeg_sync *mp, refbuf_t *new_block, unsigned offset);
void mpeg_data_insert (mpeg_sync *mp, refbuf_t *inserted);

#endif /* __MPEG_SYNC_H */
