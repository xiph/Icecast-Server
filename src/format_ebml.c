/* Icecast
 *
 * This program is distributed under the GNU General Public License,
 * version 2. A copy of this license is included with this source.
 * At your option, this specific source file can also be distributed
 * under the GNU GPL version 3.
 *
 * Copyright 2012,      David Richards, Mozilla Foundation,
 *                      and others (see AUTHORS for details).
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* format_ebml.c
 *
 * format plugin for WebM/EBML
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <stdint.h>
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

/* The size of the header buffer; should be large enough to contain
 * everything before the first Cluster in a reasonable stream
 */
#define EBML_HEADER_MAX_SIZE 131072

/* The size of the input/staging buffers; this much of a cluster
 * will be buffered before being returned. Should be large enough
 * that the first video block will be encountered before it is full,
 * to allow probing for the keyframe flag while we still have the
 * option to mark the cluster as a sync point.
 */
#define EBML_SLICE_SIZE 4096

/* A value that no EBML var-int is allowed to take. */
#define EBML_UNKNOWN ((uint_least64_t) -1)

/* The magic numbers for each element we are interested in.
 * Defined here:
 * http://www.matroska.org/technical/specs/index.html
 * http://www.webmproject.org/docs/container/
 *
 * Some of the higher-level elements have 4-byte identifiers;
 * The lower-level elements have 1-byte identifiers.
 */
#define UNCOMMON_MAGIC_LEN 4

#define SEGMENT_MAGIC "\x18\x53\x80\x67"
#define CLUSTER_MAGIC "\x1F\x43\xB6\x75"
#define TRACKS_MAGIC "\x16\x54\xAE\x6B"

#define COMMON_MAGIC_LEN 1

#define TRACK_ENTRY_MAGIC "\xAE"
#define TRACK_NUMBER_MAGIC "\xD7"
#define TRACK_TYPE_MAGIC "\x83"
#define SIMPLE_BLOCK_MAGIC "\xA3"

/* If support for Tags gets added, it may make sense
 * to convert this into a pair of flags signaling
 * "new headers" and "new tags"
 */
typedef enum ebml_read_mode {
    /* The header buffer has not been extracted yet */
    EBML_STATE_READING_HEADER = 0,
    /* The header buffer has been read, begin normal operation */
    EBML_STATE_READING_CLUSTERS
} ebml_read_mode;

typedef enum ebml_parsing_state {
    /* Examine EBML elements, output to header buffer */
    EBML_STATE_PARSING_HEADER = 0,

    /* Blindly copy a specified number of bytes to the header buffer */
    EBML_STATE_COPYING_TO_HEADER,

    /* Finalize header buffer and wait for previous cluster to flush (as necessary) */
    EBML_STATE_START_CLUSTER,

    /* Examine EBML elements, output to data buffer */
    EBML_STATE_PARSING_CLUSTERS,

    /* Blindly copy a specified number of bytes to the data buffer */
    EBML_STATE_COPYING_TO_DATA
} ebml_parsing_state;

typedef enum ebml_chunk_type {
    /* This chunk is the header buffer */
    EBML_CHUNK_HEADER = 0,

    /* This chunk starts a cluster that works as a sync point */
    EBML_CHUNK_CLUSTER_START,

    /* This chunk continues the previous cluster, or
     * else starts a non-sync-point cluster
     */
    EBML_CHUNK_CLUSTER_CONTINUE
} ebml_chunk_type;

typedef enum ebml_keyframe_status {
    /* Have not found a video track block yet */
    EBML_KEYFRAME_UNKNOWN = -1,

    /* Found the first video track block, it was not a keyframe */
    EBML_KEYFRAME_DOES_NOT_START_CLUSTER = 0,

    /* Found the first video track block, it was a keyframe */
    EBML_KEYFRAME_STARTS_CLUSTER = 1
} ebml_keyframe_status;

typedef struct ebml_st {

    ebml_read_mode output_state;
    ebml_parsing_state parse_state;
    uint_least64_t copy_len;

    ssize_t cluster_start;
    ebml_keyframe_status cluster_starts_with_keyframe;
    bool flush_cluster;

    size_t position;
    unsigned char *buffer;

    size_t input_position;
    unsigned char *input_buffer;

    size_t header_size;
    size_t header_position;
    size_t header_read_position;
    unsigned char *header;

    uint_least64_t keyframe_track_number;
    uint_least64_t parsing_track_number;
    bool parsing_track_is_video;
} ebml_t;

typedef struct ebml_source_state_st {

    ebml_t *ebml;
    refbuf_t *header;
    bool file_headers_written;

} ebml_source_state_t;

typedef struct ebml_client_data_st {

    refbuf_t *header;
    size_t header_pos;

} ebml_client_data_t;

static void ebml_free_plugin(format_plugin_t *plugin);
static refbuf_t *ebml_get_buffer(source_t *source);
static int ebml_write_buf_to_client(client_t *client);
static void ebml_write_buf_to_file(source_t *source, refbuf_t *refbuf);
static int ebml_create_client_data(source_t *source, client_t *client);
static void ebml_free_client_data(client_t *client);

static ebml_t *ebml_create();
static void ebml_destroy(ebml_t *ebml);
static size_t ebml_read_space(ebml_t *ebml);
static size_t ebml_read(ebml_t *ebml, char *buffer, size_t len, ebml_chunk_type *chunk_type);
static unsigned char *ebml_get_write_buffer(ebml_t *ebml, size_t *bytes);
static ssize_t ebml_wrote(ebml_t *ebml, size_t len);
static ssize_t ebml_parse_tag(unsigned char      *buffer,
                              unsigned char      *buffer_end,
                              uint_least64_t *tag_id,
                              uint_least64_t *payload_length);
static ssize_t ebml_parse_var_int(unsigned char      *buffer,
                                  unsigned char      *buffer_end,
                                  uint_least64_t *out_value);
static ssize_t ebml_parse_sized_int(unsigned char      *buffer,
                                    unsigned char      *buffer_end,
                                    size_t             len,
                                    bool                is_signed,
                                    uint_least64_t *out_value);
static inline void ebml_check_track(ebml_t *ebml);

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

    plugin->contenttype = igloo_httpp_getvar(source->parser, "content-type");

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
    size_t len = EBML_SLICE_SIZE;
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
    ssize_t read_bytes = 0;
    size_t write_bytes = 0;
    ebml_chunk_type chunk_type;
    refbuf_t *refbuf;
    ssize_t ret;

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

            if (chunk_type == EBML_CHUNK_CLUSTER_START)
            {
                refbuf->sync_point = 1;
            }
            return refbuf;

        } else if(read_bytes == 0) {
            /* Feed more bytes into the parser */
            write_buffer = ebml_get_write_buffer(ebml_source_state->ebml, &write_bytes);
            read_bytes = client_body_read(source->client, write_buffer, write_bytes);
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

    if ( ! ebml_source_state->file_headers_written)
    {
        if (fwrite (ebml_source_state->header->data, 1,
                    ebml_source_state->header->len,
                    source->dumpfile) != ebml_source_state->header->len)
            ebml_write_buf_to_file_fail(source);
        else
            ebml_source_state->file_headers_written = true;
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
    ebml->buffer = calloc(1, EBML_SLICE_SIZE);
    ebml->input_buffer = calloc(1, EBML_SLICE_SIZE);

    ebml->cluster_start = -1;

    ebml->keyframe_track_number = EBML_UNKNOWN;
    ebml->parsing_track_number = EBML_UNKNOWN;
    ebml->parsing_track_is_video = false;

    return ebml;

}

/* Return the size of a buffer needed to store the next
 * chunk that ebml_read can yield.
 */
static size_t ebml_read_space(ebml_t *ebml)
{

    size_t read_space;

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

                if (ebml->position == EBML_SLICE_SIZE) {
                    /* The current cluster fills the buffer,
                     * we have no choice but to start flushing it.
                     */

                    ebml->flush_cluster = true;
                }

                if (ebml->flush_cluster) {
                    /* return what we have */
                    read_space = ebml->position;
                } else {
                    /* wait until we've read more, so the parser has
                     * time to gather metadata
                     */
                    read_space = 0;
                }
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
static size_t ebml_read(ebml_t *ebml, char *buffer, size_t len, ebml_chunk_type *chunk_type)
{

    size_t read_space;
    size_t to_read;

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

                if (ebml->cluster_starts_with_keyframe != EBML_KEYFRAME_DOES_NOT_START_CLUSTER) {
                    /* If we positively identified the first video frame as a non-keyframe,
                     * don't use this cluster as a sync point. Since some files lack
                     * video tracks completely, or we may have failed to probe
                     * the first video frame, it's better to be pass through
                     * ambiguous cases to avoid blocking the stream forever.
                     */
                    *chunk_type = EBML_CHUNK_CLUSTER_START;
                }

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
static unsigned char *ebml_get_write_buffer(ebml_t *ebml, size_t *bytes)
{
    *bytes = EBML_SLICE_SIZE - ebml->input_position;
    return ebml->input_buffer + ebml->input_position;
}

/* Process data that has been written to the EBML parser's input buffer.
 */
static ssize_t ebml_wrote(ebml_t *ebml, size_t len)
{
    bool processing = true;
    size_t cursor = 0;
    size_t to_copy;
    unsigned char *end_of_buffer;

    ssize_t tag_length;
    ssize_t value_length;
    ssize_t track_number_length;
    uint_least64_t tag_id;
    uint_least64_t payload_length;
    uint_least64_t data_value;
    uint_least64_t track_number;
    unsigned char flags;
    ebml_parsing_state copy_state;

    ebml->input_position += len;
    end_of_buffer = ebml->input_buffer + ebml->input_position;

    while (processing) {

        switch (ebml->parse_state) {

            case EBML_STATE_PARSING_HEADER:
            case EBML_STATE_PARSING_CLUSTERS:

                if (ebml->parse_state == EBML_STATE_PARSING_HEADER) {
                    copy_state = EBML_STATE_COPYING_TO_HEADER;
                } else {
                    copy_state = EBML_STATE_COPYING_TO_DATA;
                }

                tag_length = ebml_parse_tag(ebml->input_buffer + cursor,
                                            end_of_buffer, &tag_id, &payload_length);

                if (tag_length == 0) {
                    /* Wait for more data */
                    processing = false;
                    break;
                } else if (tag_length < 0) {
                    /* Parse error */
                    return -1;
                }

                if (payload_length == EBML_UNKNOWN) {
                    /* Parse all children for tags we can't skip */
                    payload_length = 0;
                }

                    /* Recognize tags of interest */
                    if (tag_length > UNCOMMON_MAGIC_LEN) {
                        if (!memcmp(ebml->input_buffer + cursor, CLUSTER_MAGIC, UNCOMMON_MAGIC_LEN)) {
                            /* Found a Cluster */
                            ebml->parse_state = EBML_STATE_START_CLUSTER;
                            break;
                        } else if (!memcmp(ebml->input_buffer + cursor, SEGMENT_MAGIC, UNCOMMON_MAGIC_LEN)) {
                            /* Parse all Segment children */
                            payload_length = 0;

                        } else if (!memcmp(ebml->input_buffer + cursor, TRACKS_MAGIC, UNCOMMON_MAGIC_LEN)) {
                            /* Parse all Tracks children */
                            payload_length = 0;

                        }

                    }

                    if (tag_length > COMMON_MAGIC_LEN) {
                        if (!memcmp(ebml->input_buffer + cursor, SIMPLE_BLOCK_MAGIC, COMMON_MAGIC_LEN)) {
                            /* Probe SimpleBlock header for the keyframe status */
                            if (ebml->cluster_starts_with_keyframe == EBML_KEYFRAME_UNKNOWN) {
                                track_number_length = ebml_parse_var_int(ebml->input_buffer + cursor + tag_length,
                                                                  end_of_buffer, &track_number);

                                if (track_number_length == 0) {
                                    /* Wait for more data */
                                    processing = false;
                                } else if (track_number_length < 0) {
                                    return -1;
                                } else if (track_number == ebml->keyframe_track_number) {
                                    /* this block belongs to the video track */

                                    /* skip the 16-bit timecode for now, read the flags byte */
                                    if (cursor + tag_length + track_number_length + 2 >= ebml->input_position) {
                                        /* Wait for more data */
                                        processing = false;
                                    } else {
                                        flags = ebml->input_buffer[cursor + tag_length + track_number_length + 2];

                                        if (flags & 0x80) {
                                            /* "keyframe" flag is set */
                                            ebml->cluster_starts_with_keyframe = EBML_KEYFRAME_STARTS_CLUSTER;
                                            /* ICECAST_LOG_DEBUG("Found keyframe in track %hhu", track_number); */
                                        } else {
                                            ebml->cluster_starts_with_keyframe = EBML_KEYFRAME_DOES_NOT_START_CLUSTER;
                                            /* ICECAST_LOG_DEBUG("Found non-keyframe in track %hhu", track_number); */
                                        }
                                    }

                                }

                            }

                        } else if (!memcmp(ebml->input_buffer + cursor, TRACK_ENTRY_MAGIC, COMMON_MAGIC_LEN)) {
                            /* Parse all TrackEntry children; reset the state */
                            payload_length = 0;
                            ebml->parsing_track_number = EBML_UNKNOWN;
                            ebml->parsing_track_is_video = false;

                        } else if (!memcmp(ebml->input_buffer + cursor, TRACK_NUMBER_MAGIC, COMMON_MAGIC_LEN)) {
                            /* Probe TrackNumber for value */
                            value_length = ebml_parse_sized_int(ebml->input_buffer + cursor + tag_length,
                                                                end_of_buffer, payload_length, 0, &data_value);

                            if (value_length == 0) {
                                /* Wait for more data */
                                processing = false;
                            } else if (value_length < 0) {
                                return -1;
                            } else {
                                ebml->parsing_track_number = data_value;
                                ebml_check_track(ebml);
                            }

                        } else if (!memcmp(ebml->input_buffer + cursor, TRACK_TYPE_MAGIC, COMMON_MAGIC_LEN)) {
                            /* Probe TrackType for a video flag */
                            value_length = ebml_parse_sized_int(ebml->input_buffer + cursor + tag_length,
                                                                end_of_buffer, payload_length, 0, &data_value);

                            if (value_length == 0) {
                                /* Wait for more data */
                                processing = false;
                            } else if (value_length < 0) {
                                return -1;
                            } else {
                                if (data_value & 0x01) {
                                    /* This is a video track (0x01 flag = video) */
                                    ebml->parsing_track_is_video = true;
                                    ebml_check_track(ebml);
                                }
                            }

                        }
                    }

                    if (processing) {
                        /* Moving to next element, copy current to buffer */
                        ebml->copy_len = tag_length + payload_length;
                        ebml->parse_state = copy_state;
                    }

                break;

            case EBML_STATE_START_CLUSTER:
                /* found a cluster; wait to process it until
                 * any previous cluster tag has been flushed
                 * from the read buffer, so as to not lose the
                 * sync point.
                 */
                if (ebml->cluster_start >= 0) {
                    /* Allow the cluster in the read buffer to flush. */
                    ebml->flush_cluster = true;
                    processing = false;
                } else {

                    tag_length = ebml_parse_tag(ebml->input_buffer + cursor,
                                                end_of_buffer, &tag_id, &payload_length);

                    /* The header has been fully read by now, publish its size. */
                    ebml->header_size = ebml->header_position;

                    /* Mark this potential sync point, prepare probe */
                    ebml->cluster_start = ebml->position;
                    ebml->cluster_starts_with_keyframe = EBML_KEYFRAME_UNKNOWN;

                    /* Buffer data to give us time to probe for keyframes, etc. */
                    ebml->flush_cluster = false;

                    /* Copy cluster tag to read buffer */
                    ebml->copy_len = tag_length;
                    ebml->parse_state = EBML_STATE_COPYING_TO_DATA;
                }
                break;

            case EBML_STATE_COPYING_TO_HEADER:
            case EBML_STATE_COPYING_TO_DATA:
                to_copy = ebml->input_position - cursor;
                if (to_copy > ebml->copy_len) {
                    to_copy = ebml->copy_len;
                }

                if (ebml->parse_state == EBML_STATE_COPYING_TO_HEADER) {
                    if ((ebml->header_position + to_copy) > EBML_HEADER_MAX_SIZE) {
                        ICECAST_LOG_ERROR("EBML Header too large, failing");
                        return -1;
                    }

                    memcpy(ebml->header + ebml->header_position, ebml->input_buffer + cursor, to_copy);
                    ebml->header_position += to_copy;

                } else if (ebml->parse_state == EBML_STATE_COPYING_TO_DATA) {
                    if ((ebml->position + to_copy) > EBML_SLICE_SIZE) {
                        to_copy = EBML_SLICE_SIZE - ebml->position;
                    }

                    memcpy(ebml->buffer + ebml->position, ebml->input_buffer + cursor, to_copy);
                    ebml->position += to_copy;
                }

                cursor += to_copy;
                ebml->copy_len -= to_copy;

                if (ebml->copy_len == 0) {
                    /* resume parsing */
                    if (ebml->parse_state == EBML_STATE_COPYING_TO_HEADER) {
                        ebml->parse_state = EBML_STATE_PARSING_HEADER;
                    } else {
                        ebml->parse_state = EBML_STATE_PARSING_CLUSTERS;
                    }
                } else {
                    /* wait for more data */
                    processing = false;
                }

                break;

            default:
                processing = false;

        }

    }

    /* Shift unprocessed data down to the start of the buffer */
    memmove(ebml->input_buffer, ebml->input_buffer + cursor, ebml->input_position - cursor);
    ebml->input_position -= cursor;

    return len;

}

static inline void ebml_check_track(ebml_t *ebml)
{
    if (ebml->keyframe_track_number == EBML_UNKNOWN
        && ebml->parsing_track_is_video
        && ebml->parsing_track_number != EBML_UNKNOWN) {

        ebml->keyframe_track_number = ebml->parsing_track_number;
        ICECAST_LOG_DEBUG("Identified track #%llu as the video track", (long long unsigned int)ebml->keyframe_track_number);
    }
}

/* Try to parse an EBML tag at the given location, returning the
 * length of the tag & the length of the associated payload.
 * 
 * Returns the length of the tag on success, and writes the payload
 * size to *payload_length.
 * 
 * Return 0 if it would be necessary to read past the
 * given end-of-buffer address to read a complete tag.
 * 
 * Returns -1 if the tag is corrupt.
 */

static ssize_t ebml_parse_tag(unsigned char *buffer,
                              unsigned char *buffer_end,
                              uint_least64_t *tag_id,
                              uint_least64_t *payload_length)
{
    ssize_t type_length;
    ssize_t size_length;

    *tag_id = 0;
    *payload_length = 0;

    /* read past the type tag */
    type_length = ebml_parse_var_int(buffer, buffer_end, tag_id);

    if (type_length <= 0) {
        return type_length;
    }

    /* read the length tag */
    size_length = ebml_parse_var_int(buffer + type_length, buffer_end, payload_length);

    if (size_length <= 0) {
        return size_length;
    }

    return type_length + size_length;
}

/* Try to parse an EBML variable-length integer.
 * Returns 0 if there's not enough space to read the number;
 * Returns -1 if the number is malformed.
 * Else, returns the length of the number in bytes and writes the
 * value to *out_value.
 */
static ssize_t ebml_parse_var_int(unsigned char *buffer,
                                 unsigned char *buffer_end,
                                 uint_least64_t *out_value)
{
    ssize_t size = 1;
    ssize_t i;
    unsigned char mask = 0x80;
    uint_least64_t value;
    uint_least64_t unknown_marker;

    if (buffer >= buffer_end) {
        return 0;
    }

    /* find the length marker bit in the first byte */
    value = buffer[0];

    while (mask) {
        if (value & mask) {
            value = value & ~mask;
            unknown_marker = mask - 1;
            break;
        }
        size++;
        mask = mask >> 1;
    }

    /* catch malformed number (no prefix) */
    if (mask == 0) {
        ICECAST_LOG_DEBUG("Corrupt var-int");
        return -1;
    }

    /* catch number bigger than parsing buffer */
    if (buffer + size - 1 >= buffer_end) {
        return 0;
    }

    /* read remaining bytes of (big-endian) number */
    for (i = 1; i < size; i++) {
        value = (value << 8) + buffer[i];
        unknown_marker = (unknown_marker << 8) + 0xFF;
    }

    /* catch special "unknown" length */

    if (value == unknown_marker) {
        *out_value = EBML_UNKNOWN;
    } else {
        *out_value = value;
    }

/*
    ICECAST_LOG_DEBUG("Varint: value %lli, unknown %llu, mask %hhu, size %i", value, unknown_marker, mask, size);
*/

    return size;
}

/* Parse a big-endian int that may be from 1-8 bytes long.
 * Returns 0 if there's not enough space to read the number;
 * Returns -1 if the number is mis-sized.
 * Else, returns the length of the number in bytes and writes the
 * value to *out_value.
 * If is_signed is true, then the int is assumed to be two's complement
 * signed, negative values will be correctly promoted, and the returned
 * unsigned number can be safely cast to a signed number on systems using
 * two's complement arithmatic.
 */
static ssize_t ebml_parse_sized_int(unsigned char       *buffer,
                                    unsigned char       *buffer_end,
                                    size_t              len,
                                    bool                 is_signed,
                                    uint_least64_t  *out_value)
{
    uint_least64_t value;
    size_t i;

    if (len < 1 || len > 8) {
        ICECAST_LOG_DEBUG("Sized int of %i bytes", len);
        return -1;
    }

    if (buffer + len >= buffer_end) {
        return 0;
    }

    if (is_signed && ((signed char) buffer[0]) < 0) {
        value = -1;
    } else {
        value = 0;
    }

    for (i = 0; i < len; i++) {
        value = (value << 8) + ((unsigned char) buffer[i]);
    }

    *out_value = value;

    return len;
}
