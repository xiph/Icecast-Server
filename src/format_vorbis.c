/* format_vorbis.c
**
** format plugin for vorbis
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"

#define CATMODULE "format-vorbis"
#include "log.h"
#include "logging.h"

#define MAX_HEADER_PAGES 10

typedef struct _vstate_tag
{
    ogg_sync_state oy;
    ogg_stream_state os;
    vorbis_info vi;
    vorbis_comment vc;

    ogg_page og;
    unsigned long serialno;
    int header;
    refbuf_t *headbuf[MAX_HEADER_PAGES];
    int packets;
} vstate_t;

static void format_vorbis_free_plugin(format_plugin_t *self);
static int format_vorbis_get_buffer(format_plugin_t *self, char *data, 
        unsigned long len, refbuf_t **buffer);
static refbuf_queue_t *format_vorbis_get_predata(format_plugin_t *self);
static void *format_vorbis_create_client_data(format_plugin_t *self,
        source_t *source, client_t *client);
static void format_vorbis_send_headers(format_plugin_t *self,
        source_t *source, client_t *client);

format_plugin_t *format_vorbis_get_plugin(void)
{
    format_plugin_t *plugin;
    vstate_t *state;

    plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

    plugin->type = FORMAT_TYPE_VORBIS;
    plugin->has_predata = 1;
    plugin->get_buffer = format_vorbis_get_buffer;
    plugin->get_predata = format_vorbis_get_predata;
    plugin->write_buf_to_client = format_generic_write_buf_to_client;
    plugin->create_client_data = format_vorbis_create_client_data;
    plugin->client_send_headers = format_vorbis_send_headers;
    plugin->free_plugin = format_vorbis_free_plugin;
    plugin->format_description = "Ogg Vorbis";

    state = (vstate_t *)calloc(1, sizeof(vstate_t));
    ogg_sync_init(&state->oy);

    plugin->_state = (void *)state;

    return plugin;
}

void format_vorbis_free_plugin(format_plugin_t *self)
{
    int i;
    vstate_t *state = (vstate_t *)self->_state;

    /* free memory associated with this plugin instance */

    /* free state memory */
    ogg_sync_clear(&state->oy);
    ogg_stream_clear(&state->os);
    vorbis_comment_clear(&state->vc);
    vorbis_info_clear(&state->vi);
    
    for (i = 0; i < MAX_HEADER_PAGES; i++) {
        if (state->headbuf[i]) {
            refbuf_release(state->headbuf[i]);
            state->headbuf[i] = NULL;
        }
    }

    free(state);

    /* free the plugin instance */
    free(self);
}

int format_vorbis_get_buffer(format_plugin_t *self, char *data, unsigned long len, refbuf_t **buffer)
{
    char *buf;
    int i, result;
    ogg_packet op;
    char *tag;
    refbuf_t *refbuf;
    vstate_t *state = (vstate_t *)self->_state;
#ifdef USE_YP
    source_t *source;
    time_t current_time;
#endif

    if (data) {
        /* write the data to the buffer */
        buf = ogg_sync_buffer(&state->oy, len);
        memcpy(buf, data, len);
        ogg_sync_wrote(&state->oy, len);
    }

    refbuf = NULL;
    if (ogg_sync_pageout(&state->oy, &state->og) == 1) {
        refbuf = refbuf_new(state->og.header_len + state->og.body_len);
        memcpy(refbuf->data, state->og.header, state->og.header_len);
        memcpy(&refbuf->data[state->og.header_len], state->og.body, state->og.body_len);

        if (state->serialno != ogg_page_serialno(&state->og)) {
            /* this is a new logical bitstream */
            state->header = 0;
            state->packets = 0;

            /* release old headers, stream state, vorbis data */
            for (i = 0; i < MAX_HEADER_PAGES; i++) {
                if (state->headbuf[i]) {
                    refbuf_release(state->headbuf[i]);
                    state->headbuf[i] = NULL;
                }
            }
            /* Clear old stuff. Rarely but occasionally needed. */
            ogg_stream_clear(&state->os);
            vorbis_comment_clear(&state->vc);
            vorbis_info_clear(&state->vi);

            state->serialno = ogg_page_serialno(&state->og);
            ogg_stream_init(&state->os, state->serialno);
            vorbis_info_init(&state->vi);
            vorbis_comment_init(&state->vc);
        }

        if (state->header >= 0) {
            /* FIXME: In some streams (non-vorbis ogg streams), this could get
             * extras pages beyond the header. We need to collect the pages
             * here anyway, but they may have to be discarded later.
             */
            if (ogg_page_granulepos(&state->og) <= 0) {
                state->header++;
            } else {
                /* we're done caching headers */
                state->header = -1;

                /* put known comments in the stats */
                tag = vorbis_comment_query(&state->vc, "TITLE", 0);
                if (tag) stats_event(self->mount, "title", tag);
                else stats_event(self->mount, "title", "unknown");
                tag = vorbis_comment_query(&state->vc, "ARTIST", 0);
                if (tag) stats_event(self->mount, "artist", tag);
                else stats_event(self->mount, "artist", "unknown");

                /* don't need these now */
                ogg_stream_clear(&state->os);
                vorbis_comment_clear(&state->vc);
                vorbis_info_clear(&state->vi);
#ifdef USE_YP
                /* If we get an update on the mountpoint, force a
                   yp touch */
                avl_tree_rlock(global.source_tree);
                source = source_find_mount(self->mount);
                avl_tree_unlock(global.source_tree);

                if (source) {
                    /* If we get an update on the mountpoint, force a
                       yp touch */
                    current_time = time(NULL);
                    for (i=0; i<source->num_yp_directories; i++) {
                        source->ypdata[i]->yp_last_touch = current_time -
                            source->ypdata[i]->yp_touch_interval + 2;
                    }
                }
#endif

            }
        }

        /* cache header pages */
        if (state->header > 0 && state->packets < 3) {
            if(state->header > MAX_HEADER_PAGES) {
                refbuf_release(refbuf);
                ERROR1("Bad vorbis input: header is more than %d pages long", MAX_HEADER_PAGES);

                return -1;
            }
            refbuf_addref(refbuf);
            state->headbuf[state->header - 1] = refbuf;

            if (state->packets >= 0 && state->packets < 3) {
                ogg_stream_pagein(&state->os, &state->og);
                while (state->packets < 3) {
                    result = ogg_stream_packetout(&state->os, &op);
                    if (result == 0) break; /* need more data */
                    if (result < 0) {
                        state->packets = -1;
                        break;
                    }

                    state->packets++;

                    if (vorbis_synthesis_headerin(&state->vi, &state->vc, &op) < 0) {
                        state->packets = -1;
                        break;
                    }
                }
            }
        }
    }

    *buffer = refbuf;
    return 0;
}

refbuf_queue_t *format_vorbis_get_predata(format_plugin_t *self)
{
    refbuf_queue_t *queue;
    int i;
    vstate_t *state = (vstate_t *)self->_state;

    queue = NULL;
    for (i = 0; i < MAX_HEADER_PAGES; i++) {
        if (state->headbuf[i]) {
            refbuf_addref(state->headbuf[i]);
            refbuf_queue_add(&queue, state->headbuf[i]);
        } else {
            break;
        }
    }

    return queue;
}

static void *format_vorbis_create_client_data(format_plugin_t *self,
        source_t *source, client_t *client) 
{
    return NULL;
}

static void format_vorbis_send_headers(format_plugin_t *self,
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
}


