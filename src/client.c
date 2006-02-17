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

#ifdef HAVE_AIO
#include <errno.h>
#endif

#ifdef _WIN32
#define snprintf _snprintf
#endif

#undef CATMODULE
#define CATMODULE "client"

/* create a client_t with the provided connection and parser details. Return
 * 0 on success, -1 if server limit has been reached.  In either case a
 * client_t is returned just in case a message needs to be returned. Should
 * be called with global lock held.
 */
int client_create (client_t **c_ptr, connection_t *con, http_parser_t *parser)
{
    client_t *client = (client_t *)calloc(1, sizeof(client_t));
    int ret = 0;

    if (client == NULL)
        abort();

    global.clients++;

    /* don't do client limit check if on an SSL socket, as that will be an admin request */
    if (not_ssl_connection (con))
    {
        ice_config_t *config = config_get_config ();

        if (config->client_limit < global.clients)
        {
            WARN2 ("server client limit reached (%d/%d)", config->client_limit, global.clients);
            ret = -1;
        }
        config_release_config ();
    }

    stats_event_args (NULL, "clients", "%d", global.clients);
    client->con = con;
    client->parser = parser;
    client->refbuf = refbuf_new (PER_CLIENT_REFBUF_SIZE);
    client->refbuf->len = 0; /* force reader code to ignore buffer contents */
    client->pos = 0;
    client->write_to_client = format_generic_write_to_client;
    *c_ptr = client;

    return ret;
}

void client_destroy(client_t *client)
{
    if (client == NULL)
        return;

    /* release the buffer now, as the buffer could be on the source queue
     * and may of disappeared after auth completes */
    if (client->refbuf)
    {
        refbuf_release (client->refbuf);
        client->refbuf = NULL;
    }

    if (release_client (client))
        return;

    /* write log entry if ip is set (some things don't set it, like outgoing 
     * slave requests
     */
    if (client->respcode && client->parser)
        logging_access(client);

#ifdef HAVE_AIO
    if (aio_cancel (client->con->sock, NULL) == AIO_NOTCANCELED)
    {
        const struct aiocb *list = &client->aio;
        INFO0 ("having to wait for aio cancellation");
        while (aio_suspend (&list, 1, NULL) < 0)
            ;
    }
#endif

    if (client->con)
        connection_close(client->con);
    if (client->parser)
        httpp_destroy (client->parser);

    global_lock ();
    global.clients--;
    stats_event_args (NULL, "clients", "%d", global.clients);
    global_unlock ();

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
        if (len < client->refbuf->len)
        {
            char *ptr = client->refbuf->data;
            memmove (ptr, ptr+len, client->refbuf->len - len);
        }
        client->refbuf->len -= len;
        return len;
    }
    bytes = client->con->read (client->con, buf, len);

    if (bytes == -1 && client->con->error)
        WARN0 ("reading from connection has failed");

    return bytes;
}


void client_send_302(client_t *client, const char *location) {
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 302 Temporarily Moved\r\n"
            "Content-Type: text/html\r\n"
            "Location: %s\r\n\r\n"
            "Moved <a href=\"%s\">here</a>\r\n", location, location);
    client->respcode = 302;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
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

void client_send_403(client_t *client, const char *reason)
{
    if (reason == NULL)
        reason = "Forbidden";
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 403 %s\r\n\r\n", reason);
    client->respcode = 403;
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


void client_send_401 (client_t *client)
{
    ice_config_t *config = config_get_config ();
    const char *send_realm;
    if (client->auth && client->auth->realm)
        send_realm = client->auth->realm;
    else
        send_realm = config->server_id;

    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 401 Authentication Required\r\n"
            "WWW-Authenticate: Basic realm=\"%s\"\r\n"
            "\r\n"
            "You need to authenticate\r\n", send_realm);
    config_release_config();
    client->respcode = 401;
    auth_release (client->auth);
    client->auth = NULL;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


void client_send_416(client_t *client)
{
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
            "HTTP/1.0 416 Request Range Not Satisfiable\r\n\r\n");
    client->respcode = 416;
    client->refbuf->len = strlen (client->refbuf->data);
    fserve_add_client (client, NULL);
}


/* helper function for sending the data to a client */
int client_send_bytes (client_t *client, const void *buf, unsigned len)
{
#ifdef HAVE_AIO
    int ret, err;
    struct aiocb *aiocbp = &client->aio;

    if (client->pending_io == 0)
    {
        memset (aiocbp, 0 , sizeof (struct aiocb));
        aiocbp->aio_fildes = client->con->sock;
        aiocbp->aio_buf = (void*)buf; /* only read from */
        aiocbp->aio_nbytes = len;

        if (aio_write (aiocbp) < 0)
            return -1;
        client->pending_io = 1;
    }
    if ((err = aio_error (aiocbp)) == EINPROGRESS)
        return -1;
    ret = aio_return (aiocbp);
    if (ret < 0)
        sock_set_error (err); /* make sure errno gets set */

    client->pending_io = 0;
#else
    int ret = client->con->send (client->con, buf, len);

    if (client->con->error)
        DEBUG0 ("Client connection died");

    return ret;
#endif
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

