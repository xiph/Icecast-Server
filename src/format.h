/* format.h
**
** format plugin header
**
*/
#ifndef __FORMAT_H__
#define __FORMAT_H__

typedef enum _format_type_tag
{
	FORMAT_TYPE_VORBIS,
	FORMAT_TYPE_MP3
} format_type_t;

typedef struct _format_plugin_tag
{
	format_type_t type;

	/* we need to know the mount to report statistics */
	char *mount;

	/* set this is the data format has a header that
	** we must send before regular data
	*/
	int has_predata;

    int (*get_buffer)(struct _format_plugin_tag *self, char *data, unsigned long
            len, refbuf_t **buffer);
	refbuf_queue_t *(*get_predata)(struct _format_plugin_tag *self);
	void (*free_plugin)(struct _format_plugin_tag *self);

	/* for internal state management */
	void *_state;
} format_plugin_t;

format_type_t format_get_type(char *contenttype);
format_plugin_t *format_get_plugin(format_type_t type, char *mount);

#endif  /* __FORMAT_H__ */







