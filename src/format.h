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

#include "client.h"
#include "refbuf.h"
#include "httpp/httpp.h"

struct source_tag;
struct _mount_proxy;

typedef enum _format_type_tag
{
    FORMAT_ERROR, /* No format, source not processable */
    FORMAT_TYPE_OGG,
    FORMAT_TYPE_VORBIS,
    FORMAT_TYPE_MP3
} format_type_t;

typedef struct _format_plugin_tag
{
    format_type_t type;

    /* we need to know the mount to report statistics */
    char *mount;

    char *format_description;

    refbuf_t *(*get_buffer)(struct source_tag *);
    int (*write_buf_to_client)(struct source_tag *source, client_t *client);
    void  (*write_buf_to_file)(struct source_tag *source, refbuf_t *refbuf);
    int (*create_client_data)(struct source_tag *source, client_t *client);
    void (*set_tag)(struct _format_plugin_tag *plugin, char *tag, char *value);
    void (*free_plugin)(struct _format_plugin_tag *self);
    void (*prerelease)(struct source_tag *source, refbuf_t *refbuf);
    void (*apply_settings)(struct source_tag *source, struct _mount_proxy *mount);

    /* for internal state management */
    void *_state;
} format_plugin_t;

format_type_t format_get_type(char *contenttype);
char *format_get_mimetype(format_type_t type);
int format_get_plugin(format_type_t type, struct source_tag *source);

int format_generic_write_to_client (struct source_tag *source, client_t *client);
void format_send_general_headers(format_plugin_t *format, 
        struct source_tag *source, client_t *client);
int format_http_write_to_client (struct source_tag *source, client_t *client);
int format_intro_write_to_client (struct source_tag *source, client_t *client);
void format_initialise();

#endif  /* __FORMAT_H__ */

