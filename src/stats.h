#ifndef __STATS_H__
#define __STATS_H__

typedef struct _stats_connection_tag
{
	connection_t *con;
	http_parser_t *parser;
} stats_connection_t;

typedef struct _stats_node_tag
{
	char *name;
	char *value;
} stats_node_t;

typedef struct _stats_event_tag
{
	char *source;
	char *name;
	char *value;

	struct _stats_event_tag *next;
} stats_event_t;

typedef struct _stats_source_tag
{
	char *source;
	avl_tree *stats_tree;
} stats_source_t;

typedef struct _stats_tag
{
	avl_tree *global_tree;

	/* global stats
	start_time
	total_users
	max_users
	total_sources
	max_sources
	total_user_connections
	total_source_connections
	*/

	avl_tree *source_tree;

	/* stats by source, and for stats
	start_time
	total_users
	max_users
	*/

} stats_t;

void stats_initialize();
void stats_shutdown();

stats_t *stats_get_stats();

void stats_event(char *source, char *name, char *value);
void stats_event_args(char *source, char *name, char *format, ...);
void stats_event_inc(char *source, char *name);
void stats_event_add(char *source, char *name, unsigned long value);
void stats_event_dec(char *source, char *name);

void *stats_connection(void *arg);
void *stats_callback(void *arg);

void stats_sendxml(client_t *client);

#endif  /* __STATS_H__ */





