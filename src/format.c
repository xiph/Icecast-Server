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

#include "log.h"
#include "logging.h"
#define CATMODULE "format"

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

int format_generic_write_buf_to_client(format_plugin_t *format, 
        client_t *client, unsigned char *buf, int len)
{
    int ret;
    
    ret = sock_write_bytes(client->con->sock, buf, len);

    if(ret < 0) {
        if(sock_recoverable(ret)) {
            DEBUG1("Client had recoverable error %ld", ret);
            ret = 0;
        }
    }
    else
        client->con->sent_bytes += ret;

    return ret;
}

