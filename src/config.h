#ifndef __CONFIG_H__
#define __CONFIG_H__

#define CONFIG_EINSANE -1
#define CONFIG_ENOROOT -2
#define CONFIG_EBADROOT -3
#define CONFIG_EPARSE -4

#define MAX_YP_DIRECTORIES 25

typedef struct ice_config_dir_tag
{
	char *host;
	int touch_interval;
	struct ice_config_dir_tag *next;
} ice_config_dir_t;

typedef struct _relay_server {
    char *server;
    int port;
    char *mount;
    struct _relay_server *next;
} relay_server;

typedef struct ice_config_tag
{
	char *location;
	char *admin;

	int client_limit;
	int source_limit;
	int threadpool_size;
	int client_timeout;
	int header_timeout;
	int source_timeout;
    int ice_login;
    int fileserve;

	char *source_password;
    char *relay_password;
    char *admin_username;
    char *admin_password;

	int touch_interval;
	ice_config_dir_t *dir_list;

	char *hostname;
	int port;
	char *bind_address;
	char *master_server;
	int master_server_port;
    int master_update_interval;
    char *master_password;

    relay_server *relay;

	char *base_dir;
	char *log_dir;
	char *webroot_dir;

	char *access_log;
	char *error_log;
    int loglevel;

    int chroot;
    int chuid;
    char *user;
    char *group;
    char *yp_url[MAX_YP_DIRECTORIES];
    int	yp_url_timeout[MAX_YP_DIRECTORIES];
    int num_yp_directories;
} ice_config_t;

void config_initialize(void);
void config_shutdown(void);

int config_parse_file(const char *filename);
int config_parse_cmdline(int arg, char **argv);

int config_rehash(void);

ice_config_t *config_get_config(void);

#endif  /* __CONFIG_H__ */



