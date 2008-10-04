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


/* Ogg codec handler for theora logical streams */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <ogg/ogg.h>
#include <theora/theora.h>

typedef struct source_tag source_t;

#include "refbuf.h"
#include "format_ogg.h"
#include "format_theora.h"
#include "client.h"
#include "stats.h"

#define CATMODULE "format-theora"
#include "logging.h"
#ifdef WITH_VIDEO_PREVIEW
#include <png.h>

typedef struct _video_preview_struct
{
	png_byte*   rgb_image                     ;
	int         png_compression_level         ;

	int         video_width                   ;
	int         video_height                  ;
	int         x_crop_offset                 ;
	int         y_crop_offset                 ;
} video_preview_t;
#endif

typedef struct _theora_codec_tag
{
    theora_info     ti;
    theora_comment  tc;
    int             granule_shift;
    ogg_int64_t     last_iframe;
    ogg_int64_t     prev_granulepos;
#ifdef WITH_VIDEO_PREVIEW
    theora_state    td;
    video_preview_t *video_preview;
    int            frame_count;
#endif
} theora_codec_t;


 /* video_preview.c 
 * Copyright 2005, Silvano Galliani aka kysucix <kysucix@dyne.org>
 */


#include "logging.h"


#ifdef WITH_VIDEO_PREVIEW
struct preview_details
{
    unsigned int total_length;
    refbuf_t **last;
};

static void yuv2rgb (yuv_buffer *_yuv, video_preview_t *video_preview);
static int clip (int x);


static void user_write_data (png_structp png_ptr, png_bytep data, png_size_t length)
{
#define BUFFER_BLOCK_SZ 316*1024
    struct preview_details *details = png_get_io_ptr (png_ptr);
    unsigned int offset = 0;
    int c = 0;

    if (*details->last == NULL)
    {
        /* special case */
        refbuf_t *r = refbuf_new (BUFFER_BLOCK_SZ);
        r->len = 0;
        *details->last = r;
        c++;
    }
    while (length)
    {
        unsigned int amount;
        refbuf_t *buffer = *details->last;
        if (buffer->len == BUFFER_BLOCK_SZ)
        {
            refbuf_t *last = buffer;
            buffer = refbuf_new (BUFFER_BLOCK_SZ);
            buffer->len = 0;
            last->next = buffer;
            details->last = &last->next;
            c++;
        }
        amount = BUFFER_BLOCK_SZ - buffer->len;
        if (amount > length)
            amount = length;
        memcpy (buffer->data+buffer->len, data+offset, amount);
        buffer->len += amount;
        offset += amount;
        length -= amount;
    }
    details->total_length += offset;
}


static void user_flush_data (png_structp png_ptr)
{
}

static void user_error (png_structp png_ptr, png_const_charp c)
{
    longjmp (png_ptr->jmpbuf, 1);
}


void free_video_preview (video_preview_t *video_preview)
{
    DEBUG0 ("freeing video preview");
    free (video_preview->rgb_image);
    free (video_preview);
}


video_preview_t *init_video_preview (int width, int height, int _x_crop_offset, int _y_crop_offset) 
{
    DEBUG0("init video preview");

    video_preview_t *video_preview =  calloc (1, sizeof (video_preview_t));

    /* init structure */
    video_preview -> rgb_image              = NULL;
    video_preview -> png_compression_level  = Z_BEST_SPEED;

    video_preview -> video_width            = width;
    video_preview -> video_height           = height;
    video_preview -> x_crop_offset          = _x_crop_offset;
    video_preview -> y_crop_offset          = _y_crop_offset;

    /* malloc rgb image */
    video_preview -> rgb_image = (png_byte *)calloc (1, video_preview -> video_width * video_preview -> video_height * 4 );
    if (video_preview->rgb_image == NULL)
    {
        ERROR0 ("Can't allocate memory for rgb_image");
        free_video_preview (video_preview);
        return NULL;
    }
    return video_preview;
}


static int write_video_preview (client_t *client, video_preview_t *video_preview)
{
    int i;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    struct preview_details preview;

    preview.total_length = 0;
    preview.last = &client->refbuf->next;

    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);
    if (png_ptr == NULL)
        return -1;

    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
    {
        png_destroy_write_struct (&png_ptr, (png_infopp)NULL);
        return -1;
    }

    if (setjmp (png_ptr->jmpbuf))
    {
        png_destroy_write_struct (&png_ptr, (png_infopp)NULL);
        return -1;
    }

    png_set_error_fn (png_ptr, NULL, user_error, user_error);
    png_set_write_fn (png_ptr, &preview, user_write_data, user_flush_data);

    png_set_filter( png_ptr, 0, PNG_FILTER_NONE );

    /* set the zlib compression level */
    png_set_compression_level (png_ptr, video_preview->png_compression_level);

    png_set_IHDR (png_ptr, info_ptr, video_preview->video_width , video_preview->video_height,
            8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info (png_ptr, info_ptr);
    ERROR0("finished writing png header");

    /* write image to hard disk */
    for ( i = 0; i < video_preview -> video_height; i++)
        png_write_row (png_ptr, 
                video_preview->rgb_image + i * (video_preview->video_width) * 4 );

    png_write_end (png_ptr, info_ptr);
    png_destroy_write_struct (&png_ptr, &info_ptr);

    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE, "HTTP/1.0 200 OK\r\n"
            "Content-Length: %d\r\nContentType: image/png\r\n\r\n", preview.total_length);
    client->refbuf->len = strlen (client->refbuf->data);
    client->respcode = 200;

	return 0;
}


static int get_image (client_t *client, struct ogg_codec_tag *codec)
{
    theora_codec_t *theora = codec->specific;
    return write_video_preview (client, theora->video_preview);
}


/* ok it has to be optimized but for now it's clean, and it's ok ;) */
static void yuv2rgb (yuv_buffer *_yuv, video_preview_t *video_preview) 
{
	int                     i,j;
	int                     crop_offset;

	int			y_offset;
	int			ypp_offset;
	int			uv_offset;

	/* rgba surface pointer */
	unsigned char          *prgb;
	yuv_buffer             *yuv;

	unsigned char    	y;
	unsigned char		ypp;
	unsigned char		u;
	unsigned char		v;

	yuv = _yuv;
	
	crop_offset = (video_preview -> x_crop_offset) + 
		(yuv -> y_stride) * (video_preview -> y_crop_offset);
	prgb = (unsigned char *)video_preview -> rgb_image;

	for (i = 0; i < video_preview -> video_height; i++ ) {
		for ( j = 0; j < video_preview -> video_width / 2; j++ ) {

			y_offset	= yuv -> y_stride  *  i    + j*2 + crop_offset;
			ypp_offset	= yuv -> y_stride  *  i    + j*2 + crop_offset + 1;
			uv_offset	= yuv -> uv_stride * (i/2) + j   + crop_offset;

			y		= *(yuv->y + y_offset);
			ypp		= *(yuv->y + ypp_offset);
			u		= *(yuv->u + uv_offset);
			v		= *(yuv->v + uv_offset);

			/* R G B A */
			*prgb				=  clip (y + 1.402f * (v-128)) ;
			prgb++;
			*prgb				=  clip (y - 0.34414f * (u-128) - 0.71414f * (v-128)) ;
			prgb++;
			*prgb				=  clip (y + 1.772 * (u-128)) ;
			prgb++;
			*prgb				= 255;
			prgb++;

			/* R G B A */
			*prgb				=  clip (ypp + 1.402f * (v-128)) ;
			prgb++;
			*prgb				=  clip (ypp - 0.34414f * (u-128) - 0.71414f * (v-128)) ;
			prgb++;
			*prgb				=  clip (ypp + 1.772 * (u-128)) ;
			prgb++;
			*prgb				= 255;
			prgb++;
		}
	}
}

static int clip (int x)
{
	if (x > 255)
		return 255;
	else if (x < 0)
		return 0;
	return x;
}
#endif


static void theora_codec_free (ogg_state_t *ogg_info, ogg_codec_t *codec)
{
    theora_codec_t *theora = codec->specific;

    DEBUG0 ("freeing theora codec");
    stats_event (ogg_info->mount, "video_bitrate", NULL);
    stats_event (ogg_info->mount, "video_quality", NULL);
    stats_event (ogg_info->mount, "frame_rate", NULL);
    stats_event (ogg_info->mount, "frame_size", NULL);
#ifdef WITH_VIDEO_PREVIEW
    stats_event (ogg_info->mount, "video_preview", NULL);
    if (theora->video_preview)
    {
        free_video_preview (theora->video_preview);
        theora_clear (&theora->td);
    }
#endif
    theora_info_clear (&theora->ti);
    theora_comment_clear (&theora->tc);
    ogg_stream_clear (&codec->os);
    free (theora);
    free (codec);
}


/* theora pages are not rebuilt, so here we just for headers and then
 * pass them straight through to the the queue
 */
static refbuf_t *process_theora_page (ogg_state_t *ogg_info, ogg_codec_t *codec, ogg_page *page)
{
    theora_codec_t *theora = codec->specific;
    ogg_packet packet;
    int header_page = 0;
    int has_keyframe = 0;
    refbuf_t *refbuf = NULL;
    ogg_int64_t granulepos;

    if (ogg_stream_pagein (&codec->os, page) < 0)
    {
        ogg_info->error = 1;
        return NULL;
    }
    granulepos = ogg_page_granulepos (page);

    while (ogg_stream_packetout (&codec->os, &packet) > 0)
    {
        if (theora_packet_isheader (&packet))
        {
            if (theora_decode_header (&theora->ti, &theora->tc, &packet) < 0)
            {
                ogg_info->error = 1;
                WARN0 ("problem with theora header");
                return NULL;
            }
            header_page = 1;
            codec->headers++;
            if (codec->headers == 3)
            {
                ogg_info->bitrate += theora->ti.target_bitrate;
                stats_event_args (ogg_info->mount, "video_bitrate", "%ld",
                        (long)theora->ti.target_bitrate);
                stats_event_args (ogg_info->mount, "video_quality", "%ld",
                        (long)theora->ti.quality);
                stats_event_args (ogg_info->mount, "frame_size", "%ld x %ld",
                        (long)theora->ti.frame_width,
                        (long)theora->ti.frame_height);
                stats_event_args (ogg_info->mount, "frame_rate", "%.2f",
                        (float)theora->ti.fps_numerator/theora->ti.fps_denominator);
            }
            continue;
        }
        if (codec->headers < 3)
        {
            ogg_info->error = 1;
            ERROR0 ("Not enough header packets");
            return NULL;
        }
        if (theora_packet_iskeyframe (&packet))
        {
            has_keyframe = 1;
#ifdef WITH_VIDEO_PREVIEW
            if (theora->video_preview) 
            {
                if (theora->frame_count == -1) 
                {
                    theora_decode_init (&theora->td, &theora->ti);
                    theora->frame_count = 0;
                }
                if (theora->frame_count % 16)
                    theora->frame_count++;
                else
                {
                    yuv_buffer      yuv;

                    theora_decode_packetin (&theora->td, &packet);
                    theora_decode_YUVout   (&theora->td, &yuv);
                    yuv2rgb (&yuv, theora->video_preview);
                    theora->frame_count = 1;
                }
            }
#endif
        }
    }
    if (header_page)
    {
        format_ogg_attach_header (codec, page);
        return NULL;
    }

    refbuf = make_refbuf_with_page (codec, page);
    /* DEBUG3 ("refbuf %p has pageno %ld, %llu", refbuf, ogg_page_pageno (page), (uint64_t)granulepos); */

    if (granulepos != theora->prev_granulepos || granulepos == 0)
    {
        refbuf_release (codec->possible_start);
        refbuf_addref (refbuf);
        codec->possible_start = refbuf;
    }
    theora->prev_granulepos = granulepos;
    if (has_keyframe && codec->possible_start)
    {
        codec->possible_start->sync_point = 1;
        refbuf_release (codec->possible_start);
        codec->possible_start = NULL;
    }

    return refbuf;
}


/* Check if specified BOS page is the start of a theora stream and
 * if so, create a codec structure for handling it
 */
ogg_codec_t *initial_theora_page (format_plugin_t *plugin, ogg_page *page)
{
    ogg_state_t *ogg_info = plugin->_state;
    ogg_codec_t *codec = calloc (1, sizeof (ogg_codec_t));
    ogg_packet packet;

    theora_codec_t *theora_codec = calloc (1, sizeof (theora_codec_t));

    ogg_stream_init (&codec->os, ogg_page_serialno (page));
    ogg_stream_pagein (&codec->os, page);

    theora_info_init (&theora_codec->ti);
    theora_comment_init (&theora_codec->tc);

    ogg_stream_packetout (&codec->os, &packet);

    DEBUG0("checking for theora codec");
    if (theora_decode_header (&theora_codec->ti, &theora_codec->tc, &packet) < 0)
    {
        theora_info_clear (&theora_codec->ti);
        theora_comment_clear (&theora_codec->tc);
        ogg_stream_clear (&codec->os);
        free (theora_codec);
        free (codec);
        return NULL;
    }
    INFO0 ("seen initial theora header");
    codec->specific = theora_codec;
    codec->process_page = process_theora_page;
    codec->codec_free = theora_codec_free;
    codec->parent = ogg_info;
    codec->headers = 1;
    if (ogg_info->filter_theora)
        codec->filtered = 1;
    codec->name = "Theora";

#ifdef WITH_VIDEO_PREVIEW
    /* check for video_preview config and video_preview initialization */
    if (1)
    {
        theora_info *ti = &theora_codec->ti;
        theora_codec->video_preview = init_video_preview (ti->width, ti->height, ti->offset_x, ti->offset_y);
        theora_codec->frame_count = -1;
        codec->get_image = get_image;
        stats_event_args (ogg_info->mount, "video_preview", "/admin/showimage?mount=%s&serial=%ld", ogg_info->mount, codec->os.serialno);
        //theora_decode_init (&theora_codec->td, &theora_codec->ti);
        // theora_codec->frame_count = -1;
    }
#endif

    format_ogg_attach_header (codec, page);
    if (codec->filtered == 0)
        ogg_info->codec_sync = codec;
    return codec;
}

