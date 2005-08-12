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

/* client.c
**
** client interface implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"

#include "cfgfile.h"
#include "connection.h"
#include "refbuf.h"
#include "format.h"
#include "stats.h"
#include "fserve.h"

#include "client.h"
#include "logging.h"

#undef CATMODULE
#define CATMODULE "client"

/* should be called with global lock held */
int client_create (client_t **c_ptr, connection_t *con, http_parser_t *parser)
{
    ice_config_t *config;
    client_t *client = (client_t *)calloc(1, sizeof(client_t));
    int ret = -1;

    if (client == NULL)
        return -1;

    config = config_get_config ();

    global.clients++;
    if (config->client_limit < global.clients)
        WARN2 ("server client limit reached (%d/%d)", config->client_limit, global.clients);
    else
        ret = 0;

    config_release_config ();

    stats_event_args (NULL, "clients", "%d", global.clients);
    client->con = con;
    client->parser = parser;
    client->pos = 0;
    client->write_to_client = format_generic_write_to_client;
    *c_ptr = client;

    return ret;
}

void client_destroy(client_t *client)
{
    if (client == NULL)
        return;

    if (release_client (client))
        return;

    /* write log entry if ip is set (some things don't set it, like outgoing 
     * slave requests
     */
    if (client->respcode && client->parser)
        logging_access(client);
    
    if (client->con)
        connection_close(client->con);
    if (client->parser)
        httpp_destroy(client->parser);

    global_lock ();
    global.clients--;
    stats_event_args (NULL, "clients", "%d", global.clients);
    global_unlock ();

    /* drop ref counts if need be */
    if (client->refbuf)
        refbuf_release (client->refbuf);

    /* we need to free client specific format data (if any) */
    if (client->free_client_data)
        client->free_client_data (client);

    free(client->username);
    free(client->password);

    free(client);
}


/* helper function for reading data from a client */
int client_read_bytes (client_t *client, void *buf, unsigned len)
{
    int bytes;
    
    if (client->refbuf && client->refbuf->len)
    {
        /* we have data to read from a refbuf first */
        if (client->refbuf->len < len)
            len = client->refbuf->len;
        memcpy (buf, client->refbuf->data, len);
        if (client->refbuf->len < len)
        {
            char *ptr = client->refbuf->data;
            memmove (ptr, ptr+len, client->refbuf->len - len);
        }
        client->refbuf->len -= len;
        return len;
    }
    bytes = sock_read_bytes (client->con->sock, buf, len);
    if (bytes > 0)
        return bytes;

    if (bytes < 0)
    {
        if (sock_recoverable (sock_error()))
            return -1;
        WARN0 ("source connection has died");
    }
    client->con->error = 1;
    return -1;
}


void client_send_400(client_t *client, char *message) {
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 400 Bad Request\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    client->respcode = 400;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}

void client_send_404(client_t *client, char *message) {

    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 404 File Not Found\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    client->respcode = 404;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


void client_send_401(client_t *client) {
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 401 Authentication Required\r\n"
            "WWW-Authenticate: Basic realm=\"Icecast2 Server\"\r\n"
            "\r\n"
            "You need to authenticate\r\n");
    client->respcode = 401;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}

void client_send_403(client_t *client) {
    int bytes = sock_write(client->con->sock, 
            "HTTP/1.0 403 Forbidden\r\n"
            "\r\n"
            "Access restricted.\r\n");
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 403;
    client_destroy(client);
}


/* helper function for sending the data to a client */
int client_send_bytes (client_t *client, const void *buf, unsigned len)
{
    int ret = sock_write_bytes (client->con->sock, buf, len);
    if (ret < 0 && !sock_recoverable (sock_error()))
    {
        DEBUG0 ("Client connection died");
        client->con->error = 1;
    }
    if (ret > 0)
        client->con->sent_bytes += ret;
    return ret;
}

void client_set_queue (client_t *client, refbuf_t *refbuf)
{
    refbuf_t *to_release = client->refbuf;

    client->refbuf = refbuf;
    if (refbuf)
        refbuf_addref (client->refbuf);
    client->pos = 0;
    if (to_release)
        refbuf_release (to_release);
}

