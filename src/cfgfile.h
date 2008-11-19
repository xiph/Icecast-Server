/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

#ifndef __CFGFILE_H__
#define __CFGFILE_H__

#define CONFIG_EINSANE -1
#define CONFIG_ENOROOT -2
#define CONFIG_EBADROOT -3
#define CONFIG_EPARSE -4

#define MAX_YP_DIRECTORIES 25

#define XMLSTR		(xmlChar *)

struct _mount_proxy;
struct ice_config_tag;
typedef struct _listener_t listener_t;

#include "thread/thread.h"
#include "avl/avl.h"
#include "auth.h"
#include "global.h"
#include "connection.h"

typedef struct ice_config_dir_tag
{
    char *host;
    int touch_interval;
    struct ice_config_dir_tag *next;
} ice_config_dir_t;

typedef struct _config_options {
    char *name;
    char *value;
    struct _config_options *next;
} config_options_t;

typedef struct _mount_proxy {
    char *mountname; /* The mountpoint this proxy is used for */

    char *username; /* Username and password for this mountpoint. If unset, */
    char *password; /* falls back to global source password */

    char *dumpfile; /* Filename to dump this stream to (will be appended). NULL
                       to not dump. */
    char *intro_filename;   /* Send contents of file to client before the stream */

    /* whether to allow matching files to work with http ranges */
    int file_seekable;

    int fallback_when_full; /* switch new listener to fallback source
                               when max listeners reached */
    /* Max bandwidth (kbps)  for this mountpoint only. -1 (default) is not specified */
    int64_t max_bandwidth;

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
    int queue_block_size; /* for non-ogg streams, try to create blocks of this size */
    int filter_theora; /* prevent theora pages getting queued */
    int url_ogg_meta; /* enable to allow updates via url requests for ogg */
    int ogg_passthrough; /* enable to prevent the ogg stream being rebuilt */
    int admin_comments_only; /* enable to only show comments set from the admin page */

    /* duration in seconds for sampling the bandwidth */
    int avg_bitrate_duration;

    /* trigger level at which a timer is used to prevent excessive incoming bandwidth */
    int64_t limit_rate;

    /* duration (secs) for mountpoint to be kept reserved after source client exits */
    int wait_time;

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
}aliases;

struct _listener_t 
{
    struct _listener_t *next;
    int refcount;
    int port;
    char *bind_address;
    char *shoutcast_mount;
    int qlen;
    int shoutcast_compat;
    int ssl;
};

typedef struct
{
    char *hostname;
    int  port;
    char *username;
    char *password;
} ice_master_details;

typedef struct
{
    char *name;
    int log_ip;
    int archive;
    int display;
    int trigger_size;
    char *exclude_ext;
} access_log;

typedef struct
{
    char *name;
    int archive;
    int display;
    int trigger_size;
    int level;
} error_log;

typedef struct
{
    char *name;
    int archive;
    int display;
    int trigger_size;
} playlist_log;

typedef struct ice_config_tag
{
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
    int64_t max_bandwidth;
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

    ice_master_details *master;

    char *master_server;
    int master_server_port;
    int master_update_interval;
    char *master_bind;
    char *master_username;
    char *master_password;
    int master_relay_auth;
    int master_ssl_port;
    int master_redirect;
    int max_redirects;

    relay_server *relay;

    mount_proxy *mounts;

    char *server_id;
    char *base_dir;
    char *log_dir;
    char *pidfile;
    char *banfile;
    char *allowfile;
    char *agentfile;
    char *cert_file;
    char *webroot_dir;
    char *adminroot_dir;
    aliases *aliases;
    unsigned slaves_count;

    access_log      access_log;
    error_log       error_log;
    playlist_log    playlist_log;

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
mount_proxy *config_find_mount (ice_config_t *config, const char *mount);
listener_t *config_get_listen_sock (ice_config_t *config, sock_t serversock);

int config_rehash(void);

ice_config_locks *config_locks(void);

ice_config_t *config_get_config(void);
ice_config_t *config_grab_config(void);
void config_release_config(void);

/* To be used ONLY in one-time startup code */
ice_config_t *config_get_config_unlocked(void);

#endif  /* __CFGFILE_H__ */



