#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include <sys/types.h>
#include "compat.h"
#include "httpp.h"
#include "thread.h"
#include "sock.h"

struct _client_tag;

typedef struct connection_tag
{
	unsigned long id;

	time_t con_time;
	uint64_t sent_bytes;

	int sock;
	int error;

	char *ip;
	char *host;

    /* For 'fake' connections */
    int event_number;
    void *event;
} connection_t;

void connection_initialize(void);
void connection_shutdown(void);
void connection_accept_loop(void);
void connection_close(connection_t *con);
connection_t *create_connection(sock_t sock, char *ip);
int connection_create_source(struct _client_tag *client, connection_t *con, 
        http_parser_t *parser, char *mount);

void connection_inject_event(int eventnum, void *event_data);

extern rwlock_t _source_shutdown_rwlock;

#endif  /* __CONNECTION_H__ */
