#ifndef __SOURCE_H__
#define __SOURCE_H__

#include "config.h"
#include "yp.h"
#include "util.h"
#include "format.h"

#include <stdio.h>

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
    util_dict *audio_info;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

	int	num_yp_directories;
	long listeners;
    long max_listeners;
    int send_return;
} source_t;

source_t *source_create(client_t *client, connection_t *con, 
        http_parser_t *parser, const char *mount, format_type_t type,
        mount_proxy *mountinfo);
source_t *source_find_mount(const char *mount);
int source_compare_sources(void *arg, void *a, void *b);
int source_free_source(void *key);
int source_remove_client(void *key);
void *source_main(void *arg);

#endif


