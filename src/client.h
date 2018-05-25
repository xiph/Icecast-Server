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
 * Copyright 2011-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* client.h
 **
 ** client data structions and function definitions
 **
 */
#ifndef __CLIENT_H__
#define __CLIENT_H__

typedef struct _client_tag client_t;

#include "errors.h"
#include "connection.h"
#include "refbuf.h"
#include "acl.h"
#include "cfgfile.h"
#include "admin.h"
#include "common/httpp/httpp.h"
#include "common/httpp/encoding.h"

#define CLIENT_DEFAULT_ADMIN_FORMAT                     ADMIN_FORMAT_TRANSFORMED

typedef enum _protocol_tag {
    ICECAST_PROTOCOL_HTTP = 0,
    ICECAST_PROTOCOL_SHOUTCAST
} protocol_t;

typedef enum _reuse_tag {
    /* do not reuse */
    ICECAST_REUSE_CLOSE = 0,
    /* reuse */
    ICECAST_REUSE_KEEPALIVE,
    /* Upgrade to TLS */
    ICECAST_REUSE_UPGRADETLS
} reuse_t;

struct _client_tag {
    /* mode of operation for this client */
    operation_mode mode;

    /* the client's connection */
    connection_t *con;

    /* Reuse this connection ... */
    reuse_t reuse;

    /* the client's http headers */
    http_parser_t *parser;

    /* Transfer Encoding if any */
    httpp_encoding_t *encoding;

    /* protocol client uses */
    protocol_t protocol;

    /* http response code for this client */
    int respcode;

    /* admin command if any. ADMIN_COMMAND_ERROR if not an admin command. */
    admin_command_id_t admin_command;

    /* authentication instances we still need to go thru */
    struct auth_stack_tag *authstack;

    /* Client username */
    char *username;

    /* Client password */
    char *password;

    /* Client role */
    char *role;

    /* active ACL, set as soon as the client is authenticated */
    acl_t *acl;

    /* is client getting intro data */
    long intro_offset;

    /* where in the queue the client is */
    refbuf_t *refbuf;

    /* position in first buffer */
    unsigned int pos;

    /* auth used for this client */
    struct auth_tag *auth;

    /* Format-handler-specific data for this client */
    void *format_data;

    /* function to call to release format specific resources */
    void (*free_client_data)(struct _client_tag *client);

    /* write out data associated with client */
    int (*write_to_client)(struct _client_tag *client);

    /* function to check if refbuf needs updating */
    int (*check_buffer)(struct source_tag *source, struct _client_tag *client);
};

int client_create (client_t **c_ptr, connection_t *con, http_parser_t *parser);
void client_destroy(client_t *client);
void client_send_error_by_id(client_t *client, icecast_error_id_t id);
void client_send_101(client_t *client, reuse_t reuse);
void client_send_426(client_t *client, reuse_t reuse);
admin_format_t client_get_admin_format_by_content_negotiation(client_t *client);
int client_send_bytes (client_t *client, const void *buf, unsigned len);
int client_read_bytes (client_t *client, void *buf, unsigned len);
void client_set_queue (client_t *client, refbuf_t *refbuf);

#endif  /* __CLIENT_H__ */
