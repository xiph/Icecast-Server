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

#ifndef __FSERVE_H__
#define __FSERVE_H__

#include <stdio.h>
#include "compat.h"

typedef struct _fserve_t
{
    client_t *client;

    FILE *file;
    int64_t content_length;
    int ready;
    struct _fserve_t *next;
} fserve_t;

void fserve_initialize(void);
void fserve_shutdown(void);
int fserve_client_create(client_t *httpclient, char *path);
const char *fserve_content_type (char *path);


#endif


