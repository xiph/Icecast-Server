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


void refbuf_free (refbuf_t *refbuf)
{
    free (refbuf->data);
    free (refbuf);
}


refbuf_t *refbuf_new(unsigned long size)
{
    refbuf_t *refbuf;

    refbuf = malloc (sizeof(refbuf_t));
    if (refbuf)
    {
        refbuf->data = NULL;
        if (size && (refbuf->data = malloc (size)) == NULL)
        {
            free (refbuf);
            return NULL;
        }
        refbuf->len = 0;
        refbuf->sync_point = 0;
        refbuf->allocated = size;
        refbuf->next = NULL;
        refbuf->associated = NULL;
        refbuf->refbuf_associated_release = refbuf_free;
        refbuf->refbuf_release = refbuf_free;
    }

    return refbuf;
}


void refbuf_release(refbuf_t *refbuf)
{
    while (refbuf->associated)
    {
        refbuf_t *ref = refbuf->associated;
        refbuf->associated = ref->next;
        refbuf->refbuf_associated_release (ref);
    }
    refbuf->refbuf_release (refbuf);
}

