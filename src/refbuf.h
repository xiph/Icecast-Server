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

/* refbuf.h
**
** reference counting data buffer
**
*/
#ifndef __REFBUF_H__
#define __REFBUF_H__

typedef struct _refbuf_tag
{
    char *data;
    unsigned len;
    unsigned allocated;
    int idx;
    int sync_point;
    struct _refbuf_tag *associated;
    void (*refbuf_associated_release)(struct _refbuf_tag *);
    void (*refbuf_release)(struct _refbuf_tag *);

    struct _refbuf_tag *next;
} refbuf_t;

void refbuf_initialize(void);
void refbuf_shutdown(void);

void refbuf_free (refbuf_t *refbuf);
refbuf_t *refbuf_new(unsigned long size);
void refbuf_release(refbuf_t *self);


#endif  /* __REFBUF_H__ */








