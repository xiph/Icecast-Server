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

#include "format.h"
#include "client.h"
#include "mpeg.h"


struct flv
{
    int prev_tagsize;
    int block_pos;
    uint64_t prev_ms;
    uint64_t samples;
    mpeg_sync mpeg_sync;
    unsigned char tag[20];
};


int  write_flv_buf_to_client (client_t *client);
void flv_create_client_data (format_plugin_t *plugin, client_t *client);
void free_flv_client_data (struct flv *flv);

refbuf_t *flv_meta_allocate (size_t len);
void flv_meta_append (refbuf_t *buffer, const char *tag, const char *value);
