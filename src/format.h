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

/* format.h
 **
 ** format plugin header
 **
 */
#ifndef __FORMAT_H__
#define __FORMAT_H__

#include <vorbis/codec.h>

#include "icecasttypes.h"
#include "client.h"
#include "refbuf.h"
#include "cfgfile.h"
#include "common/httpp/httpp.h"

typedef enum _format_type_tag
{
    FORMAT_ERROR, /* No format, source not processable */
    FORMAT_TYPE_OGG,
    FORMAT_TYPE_EBML,
    FORMAT_TYPE_GENERIC
} format_type_t;

typedef struct _format_plugin_tag
{
    format_type_t type;

    /* we need to know the mount to report statistics */
    char *mount;

    const char  *contenttype;
    char        *charset;
    uint64_t    read_bytes;
    uint64_t    sent_bytes;

    refbuf_t *(*get_buffer)(source_t *);
    int (*write_buf_to_client)(client_t *client);
    void (*write_buf_to_file)(source_t *source, refbuf_t *refbuf);
    int (*create_client_data)(source_t *source, client_t *client);
    void (*set_tag)(struct _format_plugin_tag *plugin, const char *tag, const char *value, const char *charset);
    void (*free_plugin)(struct _format_plugin_tag *self);
    void (*apply_settings)(client_t *client, struct _format_plugin_tag *format, mount_proxy *mount);

    /* meta data */
    vorbis_comment vc;

    /* for internal state management */
    void *_state;
} format_plugin_t;

format_type_t format_get_type(const char *contenttype);
char *format_get_mimetype(format_type_t type);
int format_get_plugin(format_type_t type, source_t *source);

int format_generic_write_to_client (client_t *client);
int format_advance_queue (source_t *source, client_t *client);
int format_check_http_buffer (source_t *source, client_t *client);
int format_check_file_buffer (source_t *source, client_t *client);


void format_send_general_headers(format_plugin_t *format, 
        source_t *source, client_t *client);

void format_set_vorbiscomment(format_plugin_t *plugin, const char *tag, const char *value);

#endif  /* __FORMAT_H__ */

