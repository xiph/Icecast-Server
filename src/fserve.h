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
#include "cfgfile.h"

typedef void (*fserve_callback_t)(client_t *, void *);

typedef struct _fserve_t
{
    client_t *client;

    FILE *file;
    int ready;
    void (*callback)(client_t *, void *);
    void *arg;
    struct _fserve_t *next;
} fserve_t;

void fserve_initialize(void);
void fserve_shutdown(void);
int fserve_client_create(client_t *httpclient, const char *path);
int fserve_add_client (client_t *client, FILE *file);
void fserve_add_client_callback (client_t *client, fserve_callback_t callback, void *arg);
char *fserve_content_type (const char *path);
void fserve_recheck_mime_types (ice_config_t *config);


#endif


