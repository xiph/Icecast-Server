/* format.c
**
** format plugin implementation
**
*/

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "connection.h"
#include "refbuf.h"

#include "format.h"

#include "format_vorbis.h"
#include "format_mp3.h"

format_type_t format_get_type(char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_VORBIS;
    else if(strcmp(contenttype, "audio/mpeg") == 0)
        return FORMAT_TYPE_MP3; 
    else
        return -1;
}

char *format_get_mimetype(format_type_t type)
{
    switch(type) {
        case FORMAT_TYPE_VORBIS:
            return "application/x-ogg";
            break;
        case FORMAT_TYPE_MP3:
            return "audio/mpeg";
            break;
        default:
            return NULL;
    }
}

format_plugin_t *format_get_plugin(format_type_t type, char *mount)
{
	format_plugin_t *plugin;

	switch (type) {
	case FORMAT_TYPE_VORBIS:
		plugin = format_vorbis_get_plugin();
		if (plugin) plugin->mount = mount;
		break;
    case FORMAT_TYPE_MP3:
        plugin = format_mp3_get_plugin();
        if (plugin) plugin->mount = mount;
        break;
	default:
		plugin = NULL;
		break;
	}

	return plugin;
}
