/* format_mp3.c
**
** format plugin for mp3 (no metadata)
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refbuf.h"

#include "stats.h"
#include "format.h"

void format_mp3_free_plugin(format_plugin_t *self);
int format_mp3_get_buffer(format_plugin_t *self, char *data, unsigned long len, refbuf_t **buffer);
refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self);

format_plugin_t *format_mp3_get_plugin(void)
{
	format_plugin_t *plugin;

	plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

	plugin->type = FORMAT_TYPE_MP3;
	plugin->has_predata = 0;
	plugin->get_buffer = format_mp3_get_buffer;
	plugin->get_predata = format_mp3_get_predata;
	plugin->free_plugin = format_mp3_free_plugin;

	plugin->_state = NULL;

	return plugin;
}

void format_mp3_free_plugin(format_plugin_t *self)
{
	/* free the plugin instance */
	free(self);
}

int format_mp3_get_buffer(format_plugin_t *self, char *data, unsigned long len, refbuf_t **buffer)
{
	refbuf_t *refbuf;
    if(!data) {
        *buffer = NULL;
        return 0;
    }
    refbuf = refbuf_new(len);

    memcpy(refbuf->data, data, len);

    *buffer = refbuf;
	return 0;
}

refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self)
{
    return NULL;
}



