/* format_mp3.c
**
** format plugin for mp3
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "httpp/httpp.h"

#include "log.h"
#include "logging.h"

#include "format_mp3.h"

#define CATMODULE "format-mp3"

#define ICY_METADATA_INTERVAL 16000

static void format_mp3_free_plugin(format_plugin_t *self);
static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
        unsigned long len, refbuf_t **buffer);
static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self);
static void *format_mp3_create_client_data(format_plugin_t *self,
        source_t *source, client_t *client);
static int format_mp3_write_buf_to_client(format_plugin_t *self,
        client_t *client, unsigned char *buf, int len);
static void format_mp3_send_headers(format_plugin_t *self, 
        source_t *source, client_t *client);

typedef struct {
   int interval;
   int offset;
   int metadata;
} mp3_client_data;

format_plugin_t *format_mp3_get_plugin(void)
{
	format_plugin_t *plugin;

	plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

	plugin->type = FORMAT_TYPE_MP3;
	plugin->has_predata = 0;
	plugin->get_buffer = format_mp3_get_buffer;
	plugin->get_predata = format_mp3_get_predata;
    plugin->write_buf_to_client = format_mp3_write_buf_to_client;
    plugin->create_client_data = format_mp3_create_client_data;
    plugin->client_send_headers = format_mp3_send_headers;
	plugin->free_plugin = format_mp3_free_plugin;
    plugin->format_description = "MP3 audio";

	plugin->_state = calloc(1, sizeof(mp3_state));

	return plugin;
}

static int format_mp3_write_buf_to_client(format_plugin_t *self, 
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

static void format_mp3_free_plugin(format_plugin_t *self)
{
	/* free the plugin instance */
	free(self);
}

static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
    unsigned long len, refbuf_t **buffer)
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

static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self)
{
    return NULL;
}

static void *format_mp3_create_client_data(format_plugin_t *self, 
        source_t *source, client_t *client) 
{
    mp3_client_data *data = calloc(1,sizeof(mp3_client_data));
    char *metadata;

    data->interval = ICY_METADATA_INTERVAL;
    data->offset = 0;

    metadata = httpp_getvar(source->parser, "icy-metadata");
    if(metadata)
    data->metadata = atoi(metadata)>0?1:0;

    return data;
}

static void format_mp3_send_headers(format_plugin_t *self,
        source_t *source, client_t *client)
{
    int bytes;
    
    client->respcode = 200;
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\n" 
            "Content-Type: %s\r\n", 
            format_get_mimetype(source->format->type));

    if(bytes > 0) client->con->sent_bytes += bytes;

    format_send_general_headers(self, source, client);

    if(0 && ((mp3_client_data *)(client->format_data))->metadata) {
        int bytes = sock_write(client->con->sock, "icy-metaint: %d\r\n", 
                ICY_METADATA_INTERVAL);
        if(bytes > 0)
            client->con->sent_bytes += bytes;
    }
}




