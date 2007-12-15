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
    unsigned int len;
    unsigned int _count;
    char *data;
    struct _refbuf_tag *associated;
    struct _refbuf_tag *next;
    int sync_point;

} refbuf_t;

void refbuf_initialize(void);
void refbuf_shutdown(void);

refbuf_t *refbuf_new(unsigned int size);
void refbuf_addref(refbuf_t *self);
void refbuf_release(refbuf_t *self);

#define PER_CLIENT_REFBUF_SIZE  4096

#endif  /* __REFBUF_H__ */

