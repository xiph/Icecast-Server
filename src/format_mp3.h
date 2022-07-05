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
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* format_mp3.h
 **
 ** mp3 format plugin
 **
 */
#ifndef __FORMAT_MP3_H__
#define __FORMAT_MP3_H__

#define MP3_METADATA_TITLE  "X_ICY_TITLE"
#define MP3_METADATA_ARTIST "X_ICY_ARTIST"
#define MP3_METADATA_URL    "X_ICY_URL"

typedef struct {
    /* These are for inline metadata */
    int inline_metadata_interval;
    int offset;
    int interval;
    char *inline_url;
    int update_metadata;

    refbuf_t *metadata;
    refbuf_t *read_data;
    int read_count;
    mutex_t url_lock;

    unsigned build_metadata_len;
    unsigned build_metadata_offset;
    char build_metadata[4081];
} mp3_state;

int format_mp3_get_plugin(struct source_tag *src);

#endif  /* __FORMAT_MP3_H__ */
