/* client.c
**
** client interface implementation
**
*/

#include <stdlib.h>
#include <string.h>

#include "thread.h"
#include "avl.h"
#include "httpp.h"

#include "connection.h"
#include "refbuf.h"

#include "client.h"
#include "logging.h"

client_t *client_create(connection_t *con, http_parser_t *parser)
{
	client_t *client = (client_t *)calloc(1, sizeof(client_t));

	client->con = con;
	client->parser = parser;
	client->queue = NULL;
	client->pos = 0;

	return client;
}

void client_destroy(client_t *client)
{
	refbuf_t *refbuf;

	/* write log entry if ip is set (some things don't set it, like outgoing 
     * slave requests
     */
    if(client->con->ip)
    	logging_access(client);
	
	connection_close(client->con);
	httpp_destroy(client->parser);

	while ((refbuf = refbuf_queue_remove(&client->queue)))
		refbuf_release(refbuf);

	free(client);
}

void client_send_400(client_t *client, char *message) {
    int bytes;
    bytes = sock_write(client->con->sock, "HTTP/1.0 404 File Not Found\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 404;
    client_destroy(client);
}

void client_send_404(client_t *client, char *message) {

    int bytes;
    bytes = sock_write(client->con->sock, "HTTP/1.0 404 File Not Found\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<b>%s</b>\r\n", message);
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 404;
    client_destroy(client);
}

void client_send_401(client_t *client) {
    int bytes = sock_write(client->con->sock, 
            "HTTP/1.0 401 Authentication Required\r\n"
            "WWW-Authenticate: Basic realm=\"Icecast2 Server\"\r\n"
            "\r\n"
            "You need to authenticate\r\n");
    if(bytes > 0) client->con->sent_bytes = bytes;
    client->respcode = 401;
    client_destroy(client);
}
