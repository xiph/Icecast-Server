#ifndef __CONFIG_H__
#define __CONFIG_H__

#define CONFIG_EINSANE -1
#define CONFIG_ENOROOT -2
#define CONFIG_EBADROOT -3

typedef struct ice_config_dir_tag
{
	char *host;
	int touch_freq;
	struct ice_config_dir_tag *next;
} ice_config_dir_t;

typedef struct ice_config_tag
{
	char *location;
	char *admin;

	int client_limit;
	int source_limit;
	int threadpool_size;
	int client_timeout;
	int header_timeout;

	char *source_password;

	int touch_freq;
	ice_config_dir_t *dir_list;

	char *hostname;
	int port;
	char *bind_address;

	char *base_dir;
	char *log_dir;

	char *access_log;
	char *error_log;
} ice_config_t;

void config_initialize(void);
void config_shutdown(void);

int config_parse_file(const char *filename);
int config_parse_cmdline(int arg, char **argv);

int config_rehash(void);

ice_config_t *config_get_config(void);

#endif  /* __CONFIG_H__ */



