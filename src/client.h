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

#include "common/httpp/httpp.h"
#include "common/httpp/encoding.h"

#include "icecasttypes.h"
#include "errors.h"
#include "refbuf.h"
#include "module.h"

#define CLIENT_DEFAULT_REPORT_XSL_HTML                  "report-html.xsl"
#define CLIENT_DEFAULT_REPORT_XSL_PLAINTEXT             "report-plaintext.xsl"
#define CLIENT_DEFAULT_ERROR_XSL_HTML                   "error-html.xsl"
#define CLIENT_DEFAULT_ERROR_XSL_PLAINTEXT              "error-plaintext.xsl"
#define CLIENT_DEFAULT_ADMIN_FORMAT                     ADMIN_FORMAT_HTML

typedef enum _document_domain_tag {
    DOCUMENT_DOMAIN_WEB,
    DOCUMENT_DOMAIN_ADMIN
} document_domain_t;

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

typedef enum {
    CLIENT_SLURP_ERROR,
    CLIENT_SLURP_NEEDS_MORE_DATA,
    CLIENT_SLURP_BUFFER_TO_SMALL,
    CLIENT_SLURP_SUCCESS
} client_slurp_result_t;

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

    /* http request body length
     * -1 for streaming (e.g. chunked), 0 for no body, >0 for NNN bytes
     */
    ssize_t request_body_length;

    /* http request body length read so far */
    size_t request_body_read;

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

    /* URI */
    char *uri;

    /* Handler module and function */
    module_t *handler_module;
    char *handler_function;

    /* is client getting intro data */
    long intro_offset;

    /* where in the queue the client is */
    refbuf_t *refbuf;

    /* position in first buffer */
    unsigned int pos;

    /* auth used for this client */
    auth_t *auth;

    /* Format-handler-specific data for this client */
    void *format_data;

    /* function to call to release format specific resources */
    void (*free_client_data)(client_t *client);

    /* write out data associated with client */
    int (*write_to_client)(client_t *client);

    /* function to check if refbuf needs updating */
    int (*check_buffer)(source_t *source, client_t *client);
};

int client_create (client_t **c_ptr, connection_t *con, http_parser_t *parser);
void client_complete(client_t *client);
void client_destroy(client_t *client);
void client_send_error_by_id(client_t *client, icecast_error_id_t id);
void client_send_101(client_t *client, reuse_t reuse);
void client_send_204(client_t *client);
void client_send_426(client_t *client, reuse_t reuse);
void client_send_reportxml(client_t *client, reportxml_t *report, document_domain_t domain, const char *xsl, admin_format_t admin_format_hint, int status);
reportxml_t *client_get_reportxml(const char *state_definition, const char *state_akindof, const char *state_text);
admin_format_t client_get_admin_format_by_content_negotiation(client_t *client);
int client_send_bytes (client_t *client, const void *buf, unsigned len);
int client_read_bytes (client_t *client, void *buf, unsigned len);
void client_set_queue (client_t *client, refbuf_t *refbuf);
ssize_t client_body_read(client_t *client, void *buf, size_t len);
int client_body_eof(client_t *client);
client_slurp_result_t client_body_slurp(client_t *client, void *buf, size_t *len);
client_slurp_result_t client_body_skip(client_t *client);
ssize_t client_get_baseurl(client_t *client, listensocket_t *listensocket, char *buf, size_t len, const char *user, const char *pw, const char *prefix, const char *suffix0, const char *suffix1);

#endif  /* __CLIENT_H__ */
