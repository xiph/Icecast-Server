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

/* format_mp3.h
**
** mp3 format plugin
**
*/
#ifndef __FORMAT_MP3_H__
#define __FORMAT_MP3_H__

typedef struct {
    char *metadata;
    int metadata_age;
    int metadata_raw;
    mutex_t lock;

    /* These are for inline metadata */
    int inline_metadata_interval;
    int offset;
    int metadata_length;
    char *metadata_buffer;
    int metadata_offset;
} mp3_state;

format_plugin_t *format_mp3_get_plugin(http_parser_t *parser);

#endif  /* __FORMAT_MP3_H__ */
