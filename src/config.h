#ifndef __CONFIG_H__
#define __CONFIG_H__

#define CONFIG_EINSANE -1
#define CONFIG_ENOROOT -2
#define CONFIG_EBADROOT -3
#define CONFIG_EPARSE -4

#define MAX_YP_DIRECTORIES 25


#include "thread/thread.h"
#include "avl/avl.h"
#include "global.h"

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
    char *localmount;
    int mp3metadata;
    struct _relay_server *next;
} relay_server;

typedef struct _mount_proxy {
    char *mountname; /* The mountpoint this proxy is used for */

    char *username; /* Username and password for this mountpoint. If unset, */
    char *password; /* falls back to global source password */

    char *dumpfile; /* Filename to dump this stream to (will be appended). NULL
                       to not dump. */
    int max_listeners; /* Max listeners for this mountpoint only. -1 to not 
                          limit here (i.e. only use the global limit) */
    char *fallback_mount;
    struct _mount_proxy *next;
} mount_proxy;

typedef struct {
    int port;
    char *bind_address;
} listener_t;

typedef struct ice_config_tag
{
    char *config_filename;

	char *location;
	char *admin;

	int client_limit;
	int source_limit;
    long queue_size_limit;
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

    listener_t listeners[MAX_LISTEN_SOCKETS];

	char *master_server;
	int master_server_port;
    int master_update_interval;
    char *master_password;

    relay_server *relay;

    mount_proxy *mounts;

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

typedef struct {
    mutex_t config_lock;
    mutex_t relay_lock;
    mutex_t mounts_lock;
} ice_config_locks;

void config_initialize(void);
void config_shutdown(void);

int config_parse_file(const char *filename, ice_config_t *configuration);
int config_initial_parse_file(const char *filename);
int config_parse_cmdline(int arg, char **argv);
void config_set_config(ice_config_t *config);
void config_clear(ice_config_t *config);

int config_rehash(void);

ice_config_locks *config_locks(void);

ice_config_t *config_get_config(void);
void config_release_config(void);

/* To be used ONLY in one-time startup code */
ice_config_t *config_get_config_unlocked(void);

#endif  /* __CONFIG_H__ */



