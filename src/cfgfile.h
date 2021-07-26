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
 * Copyright 2011-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __CFGFILE_H__
#define __CFGFILE_H__

#define CONFIG_EINSANE  -1
#define CONFIG_ENOROOT  -2
#define CONFIG_EBADROOT -3
#define CONFIG_EPARSE   -4

#include <libxml/tree.h>
#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "icecasttypes.h"
#include "compat.h"

#define XMLSTR(str) ((xmlChar *)(str)) 

#define CONFIG_PROBLEM_HOSTNAME         0x0001U
#define CONFIG_PROBLEM_LOCATION         0x0002U
#define CONFIG_PROBLEM_ADMIN            0x0004U
#define CONFIG_PROBLEM_PRNG             0x0008U
#define CONFIG_PROBLEM_UNKNOWN_NODE     0x0010U
#define CONFIG_PROBLEM_OBSOLETE_NODE    0x0020U
#define CONFIG_PROBLEM_INVALID_NODE     0x0040U

typedef enum _http_header_type {
    /* static: headers are passed as is to the client. */
    HTTP_HEADER_TYPE_STATIC,
    /* CORS: headers are only sent to the client if it's a CORS request. */
    HTTP_HEADER_TYPE_CORS
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

struct _config_options {
    char *type;
    char *name;
    char *value;
    config_options_t *next;
};

typedef enum _mount_type {
 MOUNT_TYPE_NORMAL,
 MOUNT_TYPE_DEFAULT
} mount_type;

typedef enum {
    FALLBACK_OVERRIDE_NONE = 0,
    FALLBACK_OVERRIDE_ALL,
    FALLBACK_OVERRIDE_OWN
} fallback_override_t;

typedef struct _mount_proxy {
    /* The mountpoint this proxy is used for */
    char *mountname;
    /* The type of the mount point */
    mount_type mounttype;
    /* Filename to dump this stream to (will be appended).
     * NULL to not dump.
     */
    char *dumpfile;
    /* Send contents of file to client before the stream */
    char *intro_filename;
    /* Switch new listener to fallback source when max listeners reached */
    int fallback_when_full;
    /* Max listeners for this mountpoint only.
     * -1 to not limit here (i.e. only use the global limit)
     */
    int max_listeners;
    /* Fallback mountname */
    char *fallback_mount;
    /* When this source arrives, do we steal back
     * clients from the fallback?
     */
    fallback_override_t fallback_override;
    /* Do we permit direct requests of this mountpoint?
     * (or only indirect, through fallbacks)
     */
    int no_mount;
    /* amount to send to a new client if possible, -1 take
     * from global setting
     */
    int burst_size;
    unsigned int queue_size_limit;
    /* Do we list this on the xsl pages */
    int hidden;
    /* source timeout in seconds */
    unsigned int source_timeout;
    /* character set if not utf8 */
    char *charset;
    /* outgoing per-stream metadata interval */
    int mp3_meta_interval;
    /* additional HTTP headers */
    ice_config_http_header_t *http_headers;

    /* maximum history size of played songs */
    ssize_t max_history;

    struct event_registration_tag *event;

    char *cluster_password;
    auth_stack_t *authstack;
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

#define ALIAS_FLAG_PREFIXMATCH          0x0001

typedef struct _resource {
    char *source;
    char *destination;
    int port;
    char *bind_address;
    char *listen_socket;
    char *vhost;
    char *module;
    char *handler;
    operation_mode omode;
    unsigned int flags;
    struct _resource *next;
} resource_t;

typedef struct _yp_directory {
    char *url;
    int timeout;
    int touch_interval;
    char *listen_socket_id;
    struct _yp_directory *next;
} yp_directory_t;

typedef enum _listener_type_tag {
    LISTENER_TYPE_ERROR,
    LISTENER_TYPE_NORMAL,
    LISTENER_TYPE_VIRTUAL
} listener_type_t;

typedef struct _listener_t {
    struct _listener_t *next;
    char *id;
    char *on_behalf_of;
    listener_type_t type;
    int port;
    int so_sndbuf;
    int listen_backlog;
    char *bind_address;
    int shoutcast_compat;
    char *shoutcast_mount;
    tlsmode_t tls;
    auth_stack_t *authstack;
    /* additional HTTP headers */
    ice_config_http_header_t *http_headers;
} listener_t;

typedef struct _config_tls_context {
    char *cert_file;
    char *key_file;
    char *cipher_list;
} config_tls_context_t;

typedef struct {
    char *server;
    int port;
    char *mount;
    char *username;
    char *password;
    char *bind;
    int mp3metadata;
} relay_config_upstream_t;

typedef struct {
    char *localmount;
    int on_demand;
    size_t upstreams;
    relay_config_upstream_t *upstream;
    relay_config_upstream_t upstream_default;
} relay_config_t;

typedef enum {
    PRNG_SEED_TYPE_READ_ONCE,
    PRNG_SEED_TYPE_READ_WRITE,
    PRNG_SEED_TYPE_DEVICE,
    PRNG_SEED_TYPE_STATIC,
    PRNG_SEED_TYPE_PROFILE
} prng_seed_type_t;

typedef struct prng_seed_config_tag prng_seed_config_t;
struct prng_seed_config_tag {
    char *filename;
    prng_seed_type_t type;
    ssize_t size;
    prng_seed_config_t *next;
};

struct ice_config_tag {
    char *config_filename;

    unsigned int config_problems;

    char *location;
    char *admin;

    int client_limit;
    int source_limit;
    int body_size_limit;
    unsigned int queue_size_limit;
    unsigned int burst_size;
    int client_timeout;
    int header_timeout;
    int source_timeout;
    int body_timeout;
    int fileserve;
    int on_demand; /* global setting for all relays */

    char *shoutcast_mount;
    char *shoutcast_user;
    auth_stack_t *authstack;

    struct event_registration_tag *event;

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

    /* is TLS supported by the server? */
    int tls_ok;

    size_t relay_length;
    relay_config_t **relay;

    mount_proxy *mounts;

    char *server_id;
    char *base_dir;
    char *log_dir;
    char *pidfile;
    char *null_device;
    char *banfile;
    char *allowfile;
    char *webroot_dir;
    char *adminroot_dir;
    prng_seed_config_t *prng_seed;
    resource_t *resources;
    reportxml_database_t *reportxml_db;

    char *access_log;
    char *error_log;
    char *playlist_log;
    int loglevel;
    int logsize;
    int logarchive;
    size_t access_log_lines_kept;
    size_t error_log_lines_kept;
    size_t playlist_log_lines_kept;

    config_tls_context_t tls_context;

    int chroot;
    int chuid;
    char *user;
    char *group;

    yp_directory_t *yp_directories;
};

typedef struct {
    rwlock_t config_lock;
    mutex_t relay_lock;
} ice_config_locks;

void config_initialize(void);
void config_shutdown(void);

operation_mode config_str_to_omode(ice_config_t *configuration, xmlNodePtr node, const char *str);

void config_reread_config(void);
int config_parse_file(const char *filename, ice_config_t *configuration);
int config_initial_parse_file(const char *filename);
int config_parse_cmdline(int arg, char **argv);
void config_set_config(ice_config_t *config);
listener_t *config_clear_listener (listener_t *listener);
void config_clear(ice_config_t *config);
mount_proxy *config_find_mount(ice_config_t *config, const char *mount, mount_type type);

listener_t *config_copy_listener_one(const listener_t *listener);

config_options_t *config_parse_options(xmlNodePtr node);
void config_clear_options(config_options_t *options);

void config_parse_http_headers(xmlNodePtr                  node,
                               ice_config_http_header_t  **http_headers,
                               ice_config_t               *configuration);
void config_clear_http_header(ice_config_http_header_t *header);

int config_rehash(void);

ice_config_locks *config_locks(void);

ice_config_t *config_get_config(void);
ice_config_t *config_grab_config(void);
void config_release_config(void);

/* To be used ONLY in one-time startup code */
ice_config_t *config_get_config_unlocked(void);

#endif  /* __CFGFILE_H__ */
