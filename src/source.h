#ifndef __SOURCE_H__
#define __SOURCE_H__

#include "format.h"

typedef struct source_tag
{
    client_t *client;
	connection_t *con;
	http_parser_t *parser;
	
	char *mount;
	struct _format_plugin_tag *format;

	avl_tree *client_tree;
	avl_tree *pending_tree;

	rwlock_t *shutdown_rwlock;
} source_t;

source_t *source_create(client_t *client, connection_t *con, http_parser_t *parser, const char *mount, format_type_t type);
source_t *source_find_mount(const char *mount);
int source_compare_sources(void *arg, void *a, void *b);
int source_free_source(void *key);
void *source_main(void *arg);

#endif


