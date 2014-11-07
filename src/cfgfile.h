/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org>, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 * Copyright 2011,      Dave 'justdave' Miller <justdave@mozilla.com>.
 * Copyright 2011-2014, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __CFGFILE_H__
#define __CFGFILE_H__

#define CONFIG_EINSANE -1
#define CONFIG_ENOROOT -2
#define CONFIG_EBADROOT -3
#define CONFIG_EPARSE -4

#define MAX_YP_DIRECTORIES 25

struct _mount_proxy;

#include "thread/thread.h"
#include "avl/avl.h"
#include "auth.h"
#include "global.h"
#include "connection.h"

#define XMLSTR(str) ((xmlChar *)(str)) 

typedef enum _http_header_type {
 /* static: headers are passed as is to the client. */
 HTTP_HEADER_TYPE_STATIC
} http_header_type;

typedef struct ice_config_http_header_tag {
    /* type of this header. See http_header_type */
    http_header_type type;

    /* name and value of the header */
    char *name;
    char *value;

    /* filters */
    int status;

    /* link to the next list element */
    struct ice_config_http_header_tag *next;
} ice_config_http_header_t;

typedef struct ice_config_dir_tag {
    char *host;
    int touch_interval;
    struct ice_config_dir_tag *next;
} ice_config_dir_t;

typedef struct _config_options {
    char *name;
    char *value;
    struct _config_options *next;
} config_options_t;

typedef enum _mount_type {
 MOUNT_TYPE_NORMAL,
 MOUNT_TYPE_DEFAULT
} mount_type;

typedef struct _mount_proxy {
    char *mountname; /* The mountpoint this proxy is used for */

    mount_type mounttype; /* The type of the mount point */

    char *username; /* Username and password for this mountpoint. If unset, */
    char *password; /* falls back to global source password */

    char *dumpfile; /* Filename to dump this stream to (will be appended). NULL
                       to not dump. */
    char *intro_filename;   /* Send contents of file to client before the stream */
    int fallback_when_full; /* switch new listener to fallback source
                               when max listeners reached */
    int max_listeners; /* Max listeners for this mountpoint only. -1 to not 
                          limit here (i.e. only use the global limit) */
    char *fallback_mount; /* Fallback mountname */

    int fallback_override; /* When this source arrives, do we steal back
                              clients from the fallback? */
    int no_mount; /* Do we permit direct requests of this mountpoint? (or only
                     indirect, through fallbacks) */
    int burst_size; /* amount to send to a new client if possible, -1 take
                     * from global setting */
    unsigned int queue_size_limit;
    int hidden; /* Do we list this on the xsl pages */
    unsigned int source_timeout;  /* source timeout in seconds */
    char *charset;  /* character set if not utf8 */
    int mp3_meta_interval; /* outgoing per-stream metadata interval */

    ice_config_http_header_t *http_headers; /* additional HTTP headers */

    char *auth_type; /* Authentication type */
    struct auth_tag *auth;
    char *cluster_password;
    config_options_t *auth_options; /* Options for this type */
    char *on_connect;
    char *on_disconnect;
    unsigned int max_listener_duration;

    char *stream_name;
    char *stream_description;
    char *stream_url;
    char *stream_genre;
    char *bitrate;
    char *type;
    char *subtype;
    int yp_public;

    struct _mount_proxy *next;
} mount_proxy;

typedef struct _aliases {
    char *source;
    char *destination;
    int port;
    char *bind_address;
    struct _aliases *next;
} aliases;

typedef struct _listener_t {
    struct _listener_t *next;
    int port;
    int so_sndbuf;
    char *bind_address;
    int shoutcast_compat;
    char *shoutcast_mount;
    int ssl;
} listener_t;

typedef struct ice_config_tag {
    char *config_filename;

    char *location;
    char *admin;

    int client_limit;
    int source_limit;
    unsigned int queue_size_limit;
    int threadpool_size;
    unsigned int burst_size;
    int client_timeout;
    int header_timeout;
    int source_timeout;
    int ice_login;
    int fileserve;
    int on_demand; /* global setting for all relays */

    char *shoutcast_mount;
    char *source_password;
    char *admin_username;
    char *admin_password;
    char *relay_username;
    char *relay_password;

    int touch_interval;
    ice_config_dir_t *dir_list;

    char *hostname;
    int port;
    char *mimetypes_fn;

    listener_t *listen_sock;
    unsigned int listen_sock_count;

    char *master_server;
    int master_server_port;
    int master_update_interval;
    char *master_username;
    char *master_password;

    ice_config_http_header_t *http_headers;

    relay_server *relay;

    mount_proxy *mounts;

    char *server_id;
    char *base_dir;
    char *log_dir;
    char *pidfile;
    char *banfile;
    char *allowfile;
    char *cert_file;
    char *cipher_list;
    char *webroot_dir;
    char *adminroot_dir;
    aliases *aliases;

    char *access_log;
    char *error_log;
    char *playlist_log;
    int loglevel;
    int logsize;
    int logarchive;

    int chroot;
    int chuid;
    char *user;
    char *group;
    char *yp_url[MAX_YP_DIRECTORIES];
    int    yp_url_timeout[MAX_YP_DIRECTORIES];
    int    yp_touch_interval[MAX_YP_DIRECTORIES];
    int num_yp_directories;
} ice_config_t;

typedef struct {
    rwlock_t config_lock;
    mutex_t relay_lock;
} ice_config_locks;

void config_initialize(void);
void config_shutdown(void);

int config_parse_file(const char *filename, ice_config_t *configuration);
int config_initial_parse_file(const char *filename);
int config_parse_cmdline(int arg, char **argv);
void config_set_config(ice_config_t *config);
listener_t *config_clear_listener (listener_t *listener);
void config_clear(ice_config_t *config);
mount_proxy *config_find_mount (ice_config_t *config, const char *mount, mount_type type);
listener_t *config_get_listen_sock (ice_config_t *config, connection_t *con);

int config_rehash(void);

ice_config_locks *config_locks(void);

ice_config_t *config_get_config(void);
ice_config_t *config_grab_config(void);
void config_release_config(void);

/* To be used ONLY in one-time startup code */
ice_config_t *config_get_config_unlocked(void);

#endif  /* __CFGFILE_H__ */
