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
	client_t *client = (client_t *)malloc(sizeof(client_t));

	client->con = con;
	client->parser = parser;
	client->queue = NULL;
	client->pos = 0;

	return client;
}

void client_destroy(client_t *client)
{
	refbuf_t *refbuf;

	/* write log entry */
	logging_access(client);
	
	connection_close(client->con);
	httpp_destroy(client->parser);

	while ((refbuf = refbuf_queue_remove(&client->queue)))
		refbuf_release(refbuf);

	free(client);
}
