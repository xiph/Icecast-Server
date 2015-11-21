/* Icecast
 *
 * This program is distributed under the GNU General Public License,
 * version 2. A copy of this license is included with this source.
 * At your option, this specific source file can also be distributed
 * under the GNU GPL version 3.
 *
 * Copyright 2012,      David Richards, Mozilla Foundation,
 *                      and others (see AUTHORS for details).
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>.
 */

/* format_ebml.c
 *
 * format plugin for WebM/EBML
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "format_ebml.h"

#define CATMODULE "format-ebml"

#include "logging.h"

#define EBML_HEADER_MAX_SIZE 131072
#define EBML_SLICE_SIZE 4096

typedef enum ebml_read_mode {
    EBML_STATE_READING_HEADER = 0,
    EBML_STATE_READING_CLUSTERS
} ebml_read_mode;


typedef enum ebml_chunk_type {
    EBML_CHUNK_HEADER = 0,
    EBML_CHUNK_CLUSTER_START,
    EBML_CHUNK_CLUSTER_CONTINUE
} ebml_chunk_type;

typedef struct ebml_client_data_st ebml_client_data_t;

struct ebml_client_data_st {

    refbuf_t *header;
    int header_pos;

};

struct ebml_st {

    ebml_read_mode output_state;
    
    char *cluster_id;
    int cluster_start;

    int position;
    unsigned char *buffer;

    int input_position;
    unsigned char *input_buffer;
    
    int header_size;
    int header_position;
    int header_read_position;
    unsigned char *header;

};

static void ebml_free_plugin(format_plugin_t *plugin);
static refbuf_t *ebml_get_buffer(source_t *source);
static int ebml_write_buf_to_client(client_t *client);
static void ebml_write_buf_to_file(source_t *source, refbuf_t *refbuf);
static int ebml_create_client_data(source_t *source, client_t *client);
static void ebml_free_client_data(client_t *client);

static ebml_t *ebml_create();
static void ebml_destroy(ebml_t *ebml);
static int ebml_read_space(ebml_t *ebml);
static int ebml_read(ebml_t *ebml, char *buffer, int len, ebml_chunk_type *chunk_type);
static unsigned char *ebml_get_write_buffer(ebml_t *ebml, int *bytes);
static int ebml_wrote(ebml_t *ebml, int len);

int format_ebml_get_plugin(source_t *source)
{

    ebml_source_state_t *ebml_source_state = calloc(1, sizeof(ebml_source_state_t));
    format_plugin_t *plugin = calloc(1, sizeof(format_plugin_t));

    plugin->get_buffer = ebml_get_buffer;
    plugin->write_buf_to_client = ebml_write_buf_to_client;
    plugin->create_client_data = ebml_create_client_data;
    plugin->free_plugin = ebml_free_plugin;
    plugin->write_buf_to_file = ebml_write_buf_to_file;
    plugin->set_tag = NULL;
    plugin->apply_settings = NULL;

    plugin->contenttype = httpp_getvar(source->parser, "content-type");

    plugin->_state = ebml_source_state;
    vorbis_comment_init(&plugin->vc);
    source->format = plugin;

    ebml_source_state->ebml = ebml_create();
    
    return 0;
}

static void ebml_free_plugin(format_plugin_t *plugin)
{

    ebml_source_state_t *ebml_source_state = plugin->_state;

    refbuf_release(ebml_source_state->header);
    ebml_destroy(ebml_source_state->ebml);
    free(ebml_source_state);
    vorbis_comment_clear(&plugin->vc);
    free(plugin);
}

/* Write to a client from the header buffer.
 */
static int send_ebml_header(client_t *client)
{

    ebml_client_data_t *ebml_client_data = client->format_data;
    int len = EBML_SLICE_SIZE;
    int ret;

    if (ebml_client_data->header->len - ebml_client_data->header_pos < len)
    {
        len = ebml_client_data->header->len - ebml_client_data->header_pos;
    }
    ret = client_send_bytes (client,
                             ebml_client_data->header->data + ebml_client_data->header_pos,
                             len);

    if (ret > 0)
    {
        ebml_client_data->header_pos += ret;
    }

    return ret;

}

/* Initial write-to-client function.
 */
static int ebml_write_buf_to_client (client_t *client)
{

    ebml_client_data_t *ebml_client_data = client->format_data;

    if (ebml_client_data->header_pos != ebml_client_data->header->len)
    {
        return send_ebml_header (client);
    }
    else
    {
        /* Now that the header's sent, short-circuit to the generic
         * write-refbufs function. */
        client->write_to_client = format_generic_write_to_client;
        return client->write_to_client(client);
    }

}

/* Return a refbuf to add to the queue.
 */
static refbuf_t *ebml_get_buffer(source_t *source)
{

    ebml_source_state_t *ebml_source_state = source->format->_state;
    format_plugin_t *format = source->format;
    unsigned char *write_buffer = NULL;
    int read_bytes = 0;
    int write_bytes = 0;
    ebml_chunk_type chunk_type;
    refbuf_t *refbuf;
    int ret;

    while (1)
    {
        read_bytes = ebml_read_space(ebml_source_state->ebml);
        if (read_bytes > 0) {
            /* A chunk is available for reading */
            refbuf = refbuf_new(read_bytes);
            ebml_read(ebml_source_state->ebml, refbuf->data, read_bytes, &chunk_type);

            if (ebml_source_state->header == NULL)
            {
                /* Capture header before adding clusters to the queue */
                ebml_source_state->header = refbuf;
                continue;
            }

/*            ICECAST_LOG_DEBUG("EBML: generated refbuf, size %i : %hhi %hhi %hhi",
 *                            read_bytes, refbuf->data[0], refbuf->data[1], refbuf->data[2]);
 */
                              
            if (chunk_type == EBML_CHUNK_CLUSTER_START)
            {
                refbuf->sync_point = 1;
/*                ICECAST_LOG_DEBUG("EBML: ^ was sync point"); */
            }
            return refbuf;

        } else if(read_bytes == 0) {
            /* Feed more bytes into the parser */
            write_buffer = ebml_get_write_buffer(ebml_source_state->ebml, &write_bytes);
            read_bytes = client_read_bytes (source->client, write_buffer, write_bytes);
            if (read_bytes <= 0) {
                ebml_wrote (ebml_source_state->ebml, 0);
                return NULL;
            }
            format->read_bytes += read_bytes;
            ret = ebml_wrote (ebml_source_state->ebml, read_bytes);
            if (ret != read_bytes) {
                ICECAST_LOG_ERROR("Problem processing stream");
                source->running = 0;
                return NULL;
            }
        } else {
            ICECAST_LOG_ERROR("Problem processing stream");
            source->running = 0;
            return NULL;
        }
    }
}

/* Initialize client state.
 */
static int ebml_create_client_data(source_t *source, client_t *client)
{
    ebml_client_data_t *ebml_client_data;
    ebml_source_state_t *ebml_source_state = source->format->_state;

    if (!ebml_source_state->header)
        return -1;

    ebml_client_data = calloc(1, sizeof(ebml_client_data_t));
    if (!ebml_client_data)
        return -1;

    ebml_client_data->header = ebml_source_state->header;
    refbuf_addref(ebml_client_data->header);
    client->format_data = ebml_client_data;
    client->free_client_data = ebml_free_client_data;
    return 0;
}


static void ebml_free_client_data (client_t *client)
{

    ebml_client_data_t *ebml_client_data = client->format_data;

    refbuf_release (ebml_client_data->header);
    free (client->format_data);
    client->format_data = NULL;
}


static void ebml_write_buf_to_file_fail (source_t *source)
{
    ICECAST_LOG_WARN("Write to dump file failed, disabling");
    fclose (source->dumpfile);
    source->dumpfile = NULL;
}


static void ebml_write_buf_to_file (source_t *source, refbuf_t *refbuf)
{

    ebml_source_state_t *ebml_source_state = source->format->_state;

    if (ebml_source_state->file_headers_written == 0)
    {
        if (fwrite (ebml_source_state->header->data, 1,
                    ebml_source_state->header->len,
                    source->dumpfile) != ebml_source_state->header->len)
            ebml_write_buf_to_file_fail(source);
        else
            ebml_source_state->file_headers_written = 1;
    }

    if (fwrite (refbuf->data, 1, refbuf->len, source->dumpfile) != refbuf->len)
    {
        ebml_write_buf_to_file_fail(source);
    }

}


/* internal ebml parsing */

static void ebml_destroy(ebml_t *ebml)
{

    free(ebml->header);
    free(ebml->input_buffer);
    free(ebml->buffer);
    free(ebml);

}

static ebml_t *ebml_create()
{

    ebml_t *ebml = calloc(1, sizeof(ebml_t));

    ebml->output_state = EBML_STATE_READING_HEADER;

    ebml->header = calloc(1, EBML_HEADER_MAX_SIZE);
    ebml->buffer = calloc(1, EBML_SLICE_SIZE * 4);
    ebml->input_buffer = calloc(1, EBML_SLICE_SIZE);

    ebml->cluster_id = "\x1F\x43\xB6\x75";

    ebml->cluster_start = -1;

    return ebml;

}

/* Return the size of a buffer needed to store the next
 * chunk that ebml_read can yield.
 */
static int ebml_read_space(ebml_t *ebml)
{

    int read_space;

    switch (ebml->output_state) {
        case EBML_STATE_READING_HEADER:
        
            if (ebml->header_size != 0) {
                /* The header can be read */
                return ebml->header_size;
            } else {
                /* The header's not ready yet */
                return 0;
            }
            break;
            
        case EBML_STATE_READING_CLUSTERS:
            
            if (ebml->cluster_start > 0) {
                /* return up until just before a new cluster starts */
                read_space = ebml->cluster_start;
            } else {
                /* return what we have */
                read_space = ebml->position;
            }

            return read_space;
    }
    
    ICECAST_LOG_ERROR("EBML: Invalid parser read state");
    return 0;
}

/* Return a chunk of the EBML/MKV/WebM stream.
 * The header will be buffered until it can be returned as one chunk.
 * A cluster element's opening tag will always start a new chunk.
 * 
 * chunk_type will be set to indicate if the chunk is the header,
 * the start of a cluster, or continuing the current cluster.
 */
static int ebml_read(ebml_t *ebml, char *buffer, int len, ebml_chunk_type *chunk_type)
{

    int read_space;
    int to_read;
    
    *chunk_type = EBML_CHUNK_HEADER;

    if (len < 1) {
        return 0;
    }

    switch (ebml->output_state) {
        case EBML_STATE_READING_HEADER:
        
            if (ebml->header_size != 0)
            {
                /* Can read a chunk of the header */
                read_space = ebml->header_size - ebml->header_read_position;

                if (read_space >= len) {
                    to_read = len;
                } else {
                    to_read = read_space;
                }

                memcpy(buffer, ebml->header, to_read);
                ebml->header_read_position += to_read;
                
                *chunk_type = EBML_CHUNK_HEADER;
                
                if (ebml->header_read_position == ebml->header_size) {
                    ebml->output_state = EBML_STATE_READING_CLUSTERS;
                }
            } else {
                /* The header's not ready yet */
                return 0;
            }
        
            break;
            
        case EBML_STATE_READING_CLUSTERS:
        
            *chunk_type = EBML_CHUNK_CLUSTER_CONTINUE;
            read_space = ebml->position;
            
            if (ebml->cluster_start == 0) {
                /* new cluster is starting now */
                *chunk_type = EBML_CHUNK_CLUSTER_START;
                
                /* mark end of cluster */
                ebml->cluster_start = -1;
            } else if (ebml->cluster_start > 0) {
                /* return up until just before a new cluster starts */
                read_space = ebml->cluster_start;
            }

            if (read_space < 1) {
                return 0;
            }

            if (read_space >= len ) {
                to_read = len;
            } else {
                to_read = read_space;
            }

            memcpy(buffer, ebml->buffer, to_read);
            
            /* Shift unread data down to the start of the buffer */
            memmove(ebml->buffer, ebml->buffer + to_read, ebml->position - to_read);
            ebml->position -= to_read;

            if (ebml->cluster_start > 0) {
                ebml->cluster_start -= to_read;
            }
        
            break;
    }

    return to_read;

}

/* Get pointer & length of the buffer able to accept input.
 * 
 * Returns the start of the writable space;
 * Sets bytes to the amount of space available.
 */
static unsigned char *ebml_get_write_buffer(ebml_t *ebml, int *bytes)
{
    *bytes = EBML_SLICE_SIZE - ebml->input_position;
    return ebml->input_buffer + ebml->input_position;
}

/* Process data that has been written to the EBML parser's input buffer.
 */
static int ebml_wrote(ebml_t *ebml, int len)
{

    int b;

    if (ebml->header_size == 0) {
        /* Still reading header */
        if ((ebml->header_position + len) > EBML_HEADER_MAX_SIZE) {
            ICECAST_LOG_ERROR("EBML Header too large, failing");
            return -1;
        }

/*
        ICECAST_LOG_DEBUG("EBML: Adding to header, ofset is %d size is %d adding %d",
                          ebml->header_size, ebml->header_position, len);
*/

        memcpy(ebml->header + ebml->header_position, ebml->input_buffer, len);
        ebml->header_position += len;
    } else {
        /* Header's already been determined, read into data buffer */
        memcpy(ebml->buffer + ebml->position, ebml->input_buffer, len);
    }

    for (b = 0; b <= len - 4; b++)
    {
        /* Scan for cluster start marker.
         * False positives are possible, but unlikely, and only
         * permanently corrupt a stream if they occur while scanning
         * the initial header. Else, a client can reconnect in a few
         * seconds and get a real sync point.
         */
        if (!memcmp(ebml->input_buffer + b, ebml->cluster_id, 4))
        {
/*
            ICECAST_LOG_DEBUG("EBML: found cluster");
*/

            if (ebml->header_size == 0)
            {
                /* We were looking for the header end; now we've found it */
                ebml->header_size = ebml->header_position - len + b;
                
                /* Shift data after the header into the data buffer */
                memcpy(ebml->buffer, ebml->input_buffer + b, len - b);
                ebml->position = len - b;
                
                /* Mark the start of the data as the first sync point */
                ebml->cluster_start = 0;
                return len;
            } else {
                /* We've located a sync point in the data stream */
                ebml->cluster_start = ebml->position + b;
            }
        }
    }

    ebml->position += len;

    return len;

}
