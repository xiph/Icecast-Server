#ifndef __SOURCE_H__
#define __SOURCE_H__

#include "config.h"
#include "yp.h"
#include "format.h"

typedef struct source_tag
{
    client_t *client;
	connection_t *con;
	http_parser_t *parser;
	
	char *mount;

    /* If this source drops, try to move all clients to this fallback */
    char *fallback_mount;

    /* set to zero to request the source to shutdown without causing a global
     * shutdown */
    int running;

	struct _format_plugin_tag *format;

	avl_tree *client_tree;
	avl_tree *pending_tree;

	rwlock_t *shutdown_rwlock;
	ypdata_t *ypdata[MAX_YP_DIRECTORIES];
	int	num_yp_directories;
	long	listeners;
} source_t;

source_t *source_create(client_t *client, connection_t *con, http_parser_t *parser, const char *mount, format_type_t type);
source_t *source_find_mount(const char *mount);
int source_compare_sources(void *arg, void *a, void *b);
int source_free_source(void *key);
void *source_main(void *arg);

#endif


