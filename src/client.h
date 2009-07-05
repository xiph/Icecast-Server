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

typedef struct _client_tag client_t;

#include "cfgfile.h"
#include "connection.h"
#include "refbuf.h"
#include "httpp/httpp.h"

struct _client_tag
{
    /* various states the client could be in */
    unsigned int flags;

    /* position in first buffer */
    unsigned int pos;

    /* the client's connection */
    connection_t *con;
    /* the client's http headers */
    http_parser_t *parser;

    /* reference to incoming connection details */
    listener_t *server_conn;

    /* is client getting intro data */
    long intro_offset;

    /* where in the queue the client is */
    refbuf_t *refbuf;

    /* byte count in queue */
    unsigned int lag;

    /* http response code for this client */
    int respcode;

    /* Client username, if authenticated */
    char *username;

    /* Client password, if authenticated */
    char *password;

    /* Format-handler-specific data for this client */
    void *format_data;

    /* function to call to release format specific resources */
    void (*free_client_data)(struct _client_tag *client);

    /* write out data associated with client */
    int (*write_to_client)(struct _client_tag *client);

    /* function to check if refbuf needs updating */
    int (*check_buffer)(struct source_tag *source, struct _client_tag *client);

    client_t *next;
};

client_t *client_create (connection_t *con, http_parser_t *parser);
void client_destroy(client_t *client);
void client_send_504(client_t *client, char *message);
void client_send_416(client_t *client);
void client_send_404(client_t *client, const char *message);
void client_send_401(client_t *client, const char *realm);
void client_send_403(client_t *client, const char *reason);
void client_send_403redirect (client_t *client, const char *mount, const char *reason);
void client_send_400(client_t *client, char *message);
void client_send_302(client_t *client, const char *location);
int  client_send_bytes (client_t *client, const void *buf, unsigned len);
int  client_read_bytes (client_t *client, void *buf, unsigned len);
void client_set_queue (client_t *client, refbuf_t *refbuf);

/* client flags bitmask */
#define CLIENT_AUTHENTICATED        (002)
#define CLIENT_IS_SLAVE             (004)
#define CLIENT_FORMAT_BIT           (01000)

#endif  /* __CLIENT_H__ */
