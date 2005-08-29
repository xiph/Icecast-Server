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

/* refbuf.c
**
** reference counting buffer implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "refbuf.h"

void refbuf_initialize(void)
{
}

void refbuf_shutdown(void)
{
}

refbuf_t *refbuf_new(unsigned long size)
{
    refbuf_t *refbuf;

    refbuf = (refbuf_t *)malloc(sizeof(refbuf_t));
    if (refbuf == NULL)
        return NULL;
    refbuf->data = NULL;
    if (size)
    {
        refbuf->data = malloc (size);
        if (refbuf->data == NULL)
        {
            free (refbuf);
            return NULL;
        }
    }
    refbuf->len = size;
    refbuf->sync_point = 0;
    refbuf->_count = 1;
    refbuf->next = NULL;
    refbuf->associated = NULL;

    return refbuf;
}

void refbuf_addref(refbuf_t *self)
{
    self->_count++;
}

void refbuf_release(refbuf_t *self)
{
    if (self == NULL)
        return;
    self->_count--;
    if (self->_count == 0) {
        while (self->associated)
        {
            refbuf_t *ref = self->associated;
            self->associated = ref->next;
            refbuf_release (ref);
        }
        free(self->data);
        free(self);
    }
}

