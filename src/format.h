/* format.h
**
** format plugin header
**
*/
#ifndef __FORMAT_H__
#define __FORMAT_H__

#include "client.h"
#include "refbuf.h"

struct source_tag;

typedef enum _format_type_tag
{
	FORMAT_TYPE_VORBIS,
	FORMAT_TYPE_MP3,
    FORMAT_ERROR /* No format, source not processable */
} format_type_t;

typedef struct _format_plugin_tag
{
	format_type_t type;

	/* we need to know the mount to report statistics */
	char *mount;

    char *format_description;

	/* set this is the data format has a header that
	** we must send before regular data
	*/
	int has_predata;

    int (*get_buffer)(struct _format_plugin_tag *self, char *data, unsigned long
            len, refbuf_t **buffer);
	refbuf_queue_t *(*get_predata)(struct _format_plugin_tag *self);
    int (*write_buf_to_client)(struct _format_plugin_tag *format, 
            client_t *client, unsigned char *buf, int len);
    void *(*create_client_data)(struct _format_plugin_tag *format,
            struct source_tag *source, client_t *client);
    void (*client_send_headers)(struct _format_plugin_tag *format, 
            struct source_tag *source, client_t *client);

	void (*free_plugin)(struct _format_plugin_tag *self);

	/* for internal state management */
	void *_state;
} format_plugin_t;

format_type_t format_get_type(char *contenttype);
char *format_get_mimetype(format_type_t type);
format_plugin_t *format_get_plugin(format_type_t type, char *mount);

int format_generic_write_buf_to_client(format_plugin_t *format, 
        client_t *client, unsigned char *buf, int len);
void format_send_general_headers(format_plugin_t *format, 
        struct source_tag *source, client_t *client);

#endif  /* __FORMAT_H__ */







