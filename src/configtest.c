#include <stdio.h>
#include "config.h"

void _dump_config(ice_config_t *config);

int main(void)
{
	ice_config_t *config;

	config_initialize();
	
	config_parse_file("icecast.xml");

	config = config_get_config();

	_dump_config(config);

	config_shutdown();

	return 0;
}

void _dump_config(ice_config_t *config)
{
	ice_config_dir_t *node;

	printf("-----\n");
	printf("location = %s\n", config->location);
	printf("admin = %s\n", config->admin);
	printf("client_limit = %d\n", config->client_limit);
	printf("source_limit = %d\n", config->source_limit);
	printf("threadpool_size = %d\n", config->threadpool_size);
	printf("client_timeout = %d\n", config->client_timeout);
	printf("source_password = %s\n", config->source_password);
	printf("touch_freq = %d\n", config->touch_freq);

	node = config->dir_list;
	while (node) {
		printf("directory.touch_freq = %d\n", node->touch_freq);
		printf("directory.host = %s\n", node->host);
		
		node = node->next;
	}

	printf("hostname = %s\n", config->hostname);
	printf("port = %d\n", config->port);
	printf("bind_address = %s\n", config->bind_address);
	printf("base_dir = %s\n", config->base_dir);
	printf("log_dir = %s\n", config->log_dir);
	printf("access_log = %s\n", config->access_log);
	printf("error_log = %s\n", config->error_log);
	printf("-----\n");
}





