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

/* client.h
**
** client data structions and function definitions
**
*/
#ifndef __CLIENT_H__
#define __CLIENT_H__

#ifndef WIN32
#include <aio.h>
#endif

#include "connection.h"
#include "refbuf.h"

typedef struct _client_tag
{
    /* the client's connection */
    connection_t *con;
    /* the client's http headers */
    http_parser_t *parser;

    /* http response code for this client */
    int respcode;

    /* auth completed, 0 not yet, 1 passed, 2 failed  */
    int authenticated;

    /* where in the queue the client is */
    refbuf_t *refbuf;

    /* position in first buffer */
    unsigned long pos;

    /* client is a slave server */
    int is_slave;

    /* Client username, if authenticated */
    char *username;

    /* Client password, if authenticated */
    char *password;

#ifdef HAVE_AIO
    /* for handling async IO */
    struct aiocb aio;
    int pending_io;
#endif

    /* Format-handler-specific data for this client */
    void *format_data;

    /* function to call to release format specific resources */
    void (*free_client_data)(struct _client_tag *client);

    char *predata;
    unsigned predata_size;
    unsigned predata_len;
    unsigned predata_offset;

    struct _client_tag *next;
} client_t;

client_t *client_create(connection_t *con, http_parser_t *parser);
void client_destroy(client_t *client);
void client_send_504(client_t *client, char *message);
void client_send_404(client_t *client, char *message);
void client_send_401(client_t *client);
void client_send_400(client_t *client, char *message);
void client_send_302(client_t *client, char *location);
int client_send_bytes (client_t *client, const void *buf, unsigned len);
void client_set_queue (client_t *client, refbuf_t *refbuf);
void client_as_slave (client_t *client);

#endif  /* __CLIENT_H__ */
