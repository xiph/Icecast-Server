/* client.h
**
** client data structions and function definitions
**
*/
#ifndef __CLIENT_H__
#define __CLIENT_H__

typedef struct _client_tag
{
	/* the clients connection */
	connection_t *con;
	/* the clients http headers */
	http_parser_t *parser;

	/* http response code for this client */
	int respcode;

	/* buffer queue */
	refbuf_queue_t *queue;
	/* position in first buffer */
	unsigned long pos;
} client_t;

client_t *client_create(connection_t *con, http_parser_t *parser);
void client_destroy(client_t *client);

#endif  /* __CLIENT_H__ */
