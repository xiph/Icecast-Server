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

/* format_ogg.h
**
** vorbis format plugin header
**
*/
#ifndef __FORMAT_OGG_H__
#define __FORMAT_OGG_H__

#include <ogg/ogg.h>
#include "refbuf.h"
#include "format.h"

typedef struct ogg_state_tag
{
    char *mount;
    ogg_sync_state oy;
    int error;

    int codec_count;
    struct ogg_codec_tag *codecs;
    char *artist;
    char *title;
    int log_metadata;
    refbuf_t *file_headers;
    refbuf_t *header_pages;
    refbuf_t *header_pages_tail;
    refbuf_t **bos_end;
    int bos_completed;
    long bitrate;
    struct ogg_codec_tag *current;
    struct ogg_codec_tag *codec_sync;
} ogg_state_t;


/* per codec/logical structure */
typedef struct ogg_codec_tag
{
    struct ogg_codec_tag *next;
    ogg_stream_state os;
    unsigned headers;
    const char *name;
    void *specific;
    refbuf_t        *possible_start;
    refbuf_t        *page;

    refbuf_t *(*process)(ogg_state_t *ogg_info, struct ogg_codec_tag *codec);
    refbuf_t *(*process_page)(ogg_state_t *ogg_info,
            struct ogg_codec_tag *codec, ogg_page *page);
    void (*codec_free)(ogg_state_t *ogg_info, struct ogg_codec_tag *codec);
} ogg_codec_t;


refbuf_t *make_refbuf_with_page (ogg_page *page);
void format_ogg_attach_header (ogg_state_t *ogg_info, ogg_page *page);
void format_ogg_free_headers (ogg_state_t *ogg_info);
int format_ogg_get_plugin (source_t *source);

#endif  /* __FORMAT_OGG_H__ */
