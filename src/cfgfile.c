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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "thread/thread.h"
#include "cfgfile.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h" 

#define CATMODULE "CONFIG"
#define CONFIG_DEFAULT_LOCATION "Earth"
#define CONFIG_DEFAULT_ADMIN "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT 256
#define CONFIG_DEFAULT_SOURCE_LIMIT 16
#define CONFIG_DEFAULT_QUEUE_SIZE_LIMIT (500*1024)
#define CONFIG_DEFAULT_BURST_SIZE (64*1024)
#define CONFIG_DEFAULT_THREADPOOL_SIZE 4
#define CONFIG_DEFAULT_CLIENT_TIMEOUT 30
#define CONFIG_DEFAULT_HEADER_TIMEOUT 15
#define CONFIG_DEFAULT_SOURCE_TIMEOUT 10
#define CONFIG_DEFAULT_SOURCE_PASSWORD "changeme"
#define CONFIG_DEFAULT_RELAY_PASSWORD "changeme"
#define CONFIG_DEFAULT_MASTER_USERNAME "relay"
#define CONFIG_DEFAULT_SHOUTCAST_MOUNT "/stream"
#define CONFIG_DEFAULT_ICE_LOGIN 0
#define CONFIG_DEFAULT_FILESERVE 1
#define CONFIG_DEFAULT_TOUCH_FREQ 5
#define CONFIG_DEFAULT_HOSTNAME "localhost"
#define CONFIG_DEFAULT_PLAYLIST_LOG NULL
#define CONFIG_DEFAULT_ACCESS_LOG "access.log"
#define CONFIG_DEFAULT_ERROR_LOG "error.log"
#define CONFIG_DEFAULT_LOG_LEVEL 4
#define CONFIG_DEFAULT_CHROOT 0
#define CONFIG_DEFAULT_CHUID 0
#define CONFIG_DEFAULT_USER NULL
#define CONFIG_DEFAULT_GROUP NULL
#define CONFIG_MASTER_UPDATE_INTERVAL 120
#define CONFIG_YP_URL_TIMEOUT 10

#ifndef _WIN32
#define CONFIG_DEFAULT_BASE_DIR "/usr/local/icecast"
#define CONFIG_DEFAULT_LOG_DIR "/usr/local/icecast/logs"
#define CONFIG_DEFAULT_WEBROOT_DIR "/usr/local/icecast/webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR "/usr/local/icecast/admin"
#else
#define CONFIG_DEFAULT_BASE_DIR ".\\"
#define CONFIG_DEFAULT_LOG_DIR ".\\logs"
#define CONFIG_DEFAULT_WEBROOT_DIR ".\\webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR ".\\admin"
#endif

static ice_config_t _current_configuration;
static ice_config_locks _locks;

static void _set_defaults(ice_config_t *c);
static int  _parse_root (xmlNodePtr node, ice_config_t *config);

static void create_locks(void) {
    thread_mutex_create("relay lock", &_locks.relay_lock);
    thread_rwlock_create(&_locks.config_lock);
}

static void release_locks(void) {
    thread_mutex_destroy(&_locks.relay_lock);
    thread_rwlock_destroy(&_locks.config_lock);
}

/* 
 */
struct cfg_tag
{
    const char *name;
    int (*retrieve) (xmlNodePtr node, void *x);
    void *storage;
};


/* Process xml node for boolean value, it may be true, yes, or 1
 */
int config_get_bool (xmlNodePtr node, void *x)
{   
    char *str = (char *)xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
    if (str == NULL)
        return -1;
    if (strcasecmp (str, "true") == 0)
        *(int*)x = 1;
    else
        if (strcasecmp (str, "yes") == 0)
            *(int*)x = 1;
        else
            *(int*)x = strtol (str, NULL, 0)==0 ? 0 : 1;
    xmlFree (str);
    return 0;
}

int config_get_str (xmlNodePtr node, void *x)
{
    xmlChar *str = xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
    xmlChar *p = *(xmlChar**)x;
    if (str == NULL)
        return -1;
    if (p)
        xmlFree (p);
    *(char **)x = str;
    return 0;
}

int config_get_int (xmlNodePtr node, void *x)
{
    xmlChar *str = xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
    if (str == NULL)
        return -1;
    *(int*)x = strtol ((char*)str, NULL, 0);
    return 0;
}


int parse_xml_tags (xmlNodePtr parent, const struct cfg_tag *args)
{
    int ret = 0;
    xmlNodePtr node = parent->xmlChildrenNode;

    for (; node != NULL && ret == 0; node = node->next)
    {
        const struct cfg_tag *argp;

        if (xmlIsBlankNode (node) || strcmp ((char*)node->name, "comment") == 0 ||
                strcmp ((char*)node->name, "text") == 0)
            continue;
        argp = args;
        while (argp->name)
        {
            if (strcmp ((const char*)node->name, argp->name) == 0)
            {
                ret = argp->retrieve (node, argp->storage);
                break;
            }
            argp++;
        }
        if (argp->name == NULL)
            WARN2 ("unknown element \"%s\" parsing \"%s\"\n", node->name, parent->name);
    }
    return ret;
}


void config_initialize(void) {
    create_locks();
}

void config_shutdown(void) {
    config_get_config();
    config_clear(&_current_configuration);
    config_release_config();
    release_locks();
}

void config_init_configuration(ice_config_t *configuration)
{
    memset(configuration, 0, sizeof(ice_config_t));
    _set_defaults(configuration);
}


static void config_clear_relay (relay_server *relay)
{
    xmlFree (relay->server);
    xmlFree (relay->mount);
    xmlFree (relay->localmount);
    free (relay);
}


static void config_clear_mount (mount_proxy *mount)
{
    config_options_t *option;

    xmlFree (mount->mountname);
    xmlFree (mount->username);
    xmlFree (mount->password);
    xmlFree (mount->dumpfile);
    xmlFree (mount->intro_filename);
    xmlFree (mount->on_connect);
    xmlFree (mount->on_disconnect);
    xmlFree (mount->fallback_mount);
    xmlFree (mount->stream_name);
    xmlFree (mount->stream_description);
    xmlFree (mount->stream_url);
    xmlFree (mount->stream_genre);
    xmlFree (mount->bitrate);
    xmlFree (mount->type);
    xmlFree (mount->cluster_password);

    xmlFree (mount->auth_type);
    option = mount->auth_options;
    while (option)
    {
        config_options_t *nextopt = option->next;
        xmlFree (option->name);
        xmlFree (option->value);
        free (option);
        option = nextopt;
    }
    auth_release (mount->auth);
    free (mount);
}


void config_clear(ice_config_t *c)
{
    ice_config_dir_t *dirnode, *nextdirnode;
    aliases *alias, *nextalias;
    int i;

    if (c->config_filename)
        free(c->config_filename);

    xmlFree (c->server_id);
    if (c->location && c->location != CONFIG_DEFAULT_LOCATION) 
        xmlFree(c->location);
    if (c->admin && c->admin != CONFIG_DEFAULT_ADMIN) 
        xmlFree(c->admin);
    if (c->source_password && c->source_password != CONFIG_DEFAULT_SOURCE_PASSWORD)
        xmlFree(c->source_password);
    if (c->admin_username)
        xmlFree(c->admin_username);
    if (c->admin_password)
        xmlFree(c->admin_password);
    if (c->relay_username)
        xmlFree(c->relay_username);
    if (c->relay_password)
        xmlFree(c->relay_password);
    if (c->hostname && c->hostname != CONFIG_DEFAULT_HOSTNAME) 
        xmlFree(c->hostname);
    if (c->base_dir && c->base_dir != CONFIG_DEFAULT_BASE_DIR) 
        xmlFree(c->base_dir);
    if (c->log_dir && c->log_dir != CONFIG_DEFAULT_LOG_DIR) 
        xmlFree(c->log_dir);
    if (c->webroot_dir && c->webroot_dir != CONFIG_DEFAULT_WEBROOT_DIR)
        xmlFree(c->webroot_dir);
    if (c->adminroot_dir && c->adminroot_dir != CONFIG_DEFAULT_ADMINROOT_DIR)
        xmlFree(c->adminroot_dir);
    if (c->cert_file)
        xmlFree(c->cert_file);
    if (c->pidfile)
        xmlFree(c->pidfile);
    if (c->playlist_log && c->playlist_log != CONFIG_DEFAULT_PLAYLIST_LOG) 
        xmlFree(c->playlist_log);
    if (c->access_log && c->access_log != CONFIG_DEFAULT_ACCESS_LOG) 
        xmlFree(c->access_log);
    if (c->error_log && c->error_log != CONFIG_DEFAULT_ERROR_LOG) 
        xmlFree(c->error_log);
    if (c->shoutcast_mount && c->shoutcast_mount != CONFIG_DEFAULT_SHOUTCAST_MOUNT)
        xmlFree(c->shoutcast_mount);
    for(i=0; i < MAX_LISTEN_SOCKETS; i++) {
        if (c->listeners[i].bind_address) xmlFree(c->listeners[i].bind_address);
    }
    if (c->master_server) xmlFree(c->master_server);
    if (c->master_username) xmlFree(c->master_username);
    if (c->master_password) xmlFree(c->master_password);
    if (c->user) xmlFree(c->user);
    if (c->group) xmlFree(c->group);

    thread_mutex_lock(&(_locks.relay_lock));
    while (c->relay)
    {
        relay_server *to_go = c->relay;
        c->relay = to_go->next;
        config_clear_relay (to_go);
    }
    thread_mutex_unlock(&(_locks.relay_lock));

    while (c->mounts)
    {
        mount_proxy *to_go = c->mounts;
        c->mounts = to_go->next;
        config_clear_mount (to_go);
    }
    alias = c->aliases;
    while(alias) {
        nextalias = alias->next;
        xmlFree(alias->source);
        xmlFree(alias->destination);
        xmlFree(alias->bind_address);
        free(alias);
        alias = nextalias;
    }

    dirnode = c->dir_list;
    while(dirnode) {
        nextdirnode = dirnode->next;
        xmlFree(dirnode->host);
        free(dirnode);
        dirnode = nextdirnode;
    }
#ifdef USE_YP
    i = 0;
    while (i < c->num_yp_directories)
    {
        xmlFree (c->yp_url[i]);
        i++;
    }
#endif

    memset(c, 0, sizeof(ice_config_t));
}

int config_initial_parse_file(const char *filename)
{
    /* Since we're already pointing at it, we don't need to copy it in place */
    return config_parse_file(filename, &_current_configuration);
}

int config_parse_file(const char *filename, ice_config_t *configuration)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    if (filename == NULL || strcmp(filename, "") == 0) return CONFIG_EINSANE;
    
    xmlInitParser();
    doc = xmlParseFile(filename);
    if (doc == NULL) {
        return CONFIG_EPARSE;
    }

    node = xmlDocGetRootElement(doc);
    if (node == NULL) {
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return CONFIG_ENOROOT;
    }

    if (xmlStrcmp(node->name, XMLSTR("icecast")) != 0) {
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return CONFIG_EBADROOT;
    }

    config_init_configuration(configuration);

    configuration->config_filename = (char *)strdup(filename);

    _parse_root (node, configuration);

    xmlFreeDoc(doc);

    return 0;
}

int config_parse_cmdline(int arg, char **argv)
{
    return 0;
}

ice_config_locks *config_locks(void)
{
    return &_locks;
}

void config_release_config(void)
{
    thread_rwlock_unlock(&(_locks.config_lock));
}

ice_config_t *config_get_config(void)
{
    thread_rwlock_rlock(&(_locks.config_lock));
    return &_current_configuration;
}

ice_config_t *config_grab_config(void)
{
    thread_rwlock_wlock(&(_locks.config_lock));
    return &_current_configuration;
}

/* MUST be called with the lock held! */
void config_set_config(ice_config_t *config) {
    memcpy(&_current_configuration, config, sizeof(ice_config_t));
}

ice_config_t *config_get_config_unlocked(void)
{
    return &_current_configuration;
}

static void _set_defaults(ice_config_t *configuration)
{
    configuration->location = xmlCharStrdup (CONFIG_DEFAULT_LOCATION);
    configuration->server_id = (char *)xmlCharStrdup (ICECAST_VERSION_STRING);
    configuration->admin = CONFIG_DEFAULT_ADMIN;
    configuration->client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
    configuration->source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
    configuration->queue_size_limit = CONFIG_DEFAULT_QUEUE_SIZE_LIMIT;
    configuration->threadpool_size = CONFIG_DEFAULT_THREADPOOL_SIZE;
    configuration->client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    configuration->header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
    configuration->source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
    configuration->source_password = CONFIG_DEFAULT_SOURCE_PASSWORD;
    configuration->shoutcast_mount = xmlCharStrdup (CONFIG_DEFAULT_SHOUTCAST_MOUNT);
    configuration->ice_login = CONFIG_DEFAULT_ICE_LOGIN;
    configuration->fileserve = CONFIG_DEFAULT_FILESERVE;
    configuration->touch_interval = CONFIG_DEFAULT_TOUCH_FREQ;
    configuration->on_demand = 0;
    configuration->dir_list = NULL;
    configuration->hostname = xmlCharStrdup (CONFIG_DEFAULT_HOSTNAME);
    configuration->port = 0;
    configuration->listeners[0].port = 0;
    configuration->listeners[0].bind_address = NULL;
    configuration->listeners[0].shoutcast_compat = 0;
    configuration->master_server = NULL;
    configuration->master_server_port = 0;
    configuration->master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
    configuration->master_username = (char*)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->master_password = NULL;
    configuration->master_relay_auth = 0;
    configuration->base_dir = xmlCharStrdup (CONFIG_DEFAULT_BASE_DIR);
    configuration->log_dir = xmlCharStrdup (CONFIG_DEFAULT_LOG_DIR);
    configuration->webroot_dir = xmlCharStrdup (CONFIG_DEFAULT_WEBROOT_DIR);
    configuration->adminroot_dir = xmlCharStrdup (CONFIG_DEFAULT_ADMINROOT_DIR);
    configuration->playlist_log = xmlCharStrdup (CONFIG_DEFAULT_PLAYLIST_LOG);
    configuration->access_log = xmlCharStrdup (CONFIG_DEFAULT_ACCESS_LOG);
    configuration->error_log = xmlCharStrdup (CONFIG_DEFAULT_ERROR_LOG);
    configuration->loglevel = CONFIG_DEFAULT_LOG_LEVEL;
    configuration->chroot = CONFIG_DEFAULT_CHROOT;
    configuration->chuid = CONFIG_DEFAULT_CHUID;
    configuration->user = CONFIG_DEFAULT_USER;
    configuration->group = CONFIG_DEFAULT_GROUP;
    configuration->num_yp_directories = 0;
    configuration->slaves_count = 0;
    configuration->relay_username = (char *)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->relay_password = NULL;
    /* default to a typical prebuffer size used by clients */
    configuration->burst_size = CONFIG_DEFAULT_BURST_SIZE;
}


static int _parse_alias (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    aliases **cur, *alias = calloc (1, sizeof (aliases));
    xmlChar *temp;
    
    alias->source = xmlGetProp (node, "source");
    alias->destination = xmlGetProp (node, "dest");
    if (alias->source == NULL || alias->destination == NULL)
    {
        if (alias->source) xmlFree (alias->source);
        if (alias->destination) xmlFree (alias->destination);
        free (alias);
        WARN0 ("incomplete alias definition");
        return -1;
    }
    alias->bind_address = xmlGetProp (node, "bind-address");
    temp = xmlGetProp(node, "port");
    alias->port = -1;
    if (temp)
    {
        alias->port = atoi ((char*)temp);
        xmlFree (temp);
    }
    cur = &config->aliases;
    while (*cur) cur = &((*cur)->next);
    *cur = alias;
    return 0;
}

static int _parse_authentication (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "source-password",    config_get_str,     &config->source_password },
        { "admin-user",         config_get_str,     &config->admin_username },
        { "admin-password",     config_get_str,     &config->admin_password },
        { "relay-user",         config_get_str,     &config->relay_username },
        { "relay-password",     config_get_str,     &config->relay_password },
        { NULL, NULL, NULL }
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}

static int _parse_chown (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "user",   config_get_str, &config->user },
        { "group",  config_get_str, &config->group },
        { NULL, NULL, NULL }
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}

static int _parse_security (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "chroot",         config_get_bool,    &config->chroot },
        { "changeowner",    _parse_chown,       config },
        { NULL, NULL, NULL }
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}

static int _parse_logging (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "accesslog",      config_get_str,     &config->access_log },
        { "accesslog_lines",
                            config_get_int,     &config->access_log_lines },
        { "errorlog",       config_get_str,     &config->error_log },
        { "errorlog_lines", config_get_int,     &config->error_log_lines },
        { "playlistlog",    config_get_str,     &config->access_log },
        { "playlistlog_lines",
                            config_get_int,     &config->playlist_log_lines },
        { "logsize",        config_get_int,     &config->logsize },
        { "loglevel",       config_get_int,     &config->loglevel },
        { "logarchive",     config_get_bool,    &config->logarchive },
        { NULL, NULL, NULL }
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}

static int _parse_paths (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "basedir",    config_get_str, &config->base_dir },
        { "logdir",     config_get_str, &config->log_dir },
        { "pidfile",    config_get_str, &config->pidfile },
        { "ssl_certificate",
                        config_get_str, &config->cert_file },
        { "webroot",    config_get_str, &config->webroot_dir },
        { "adminroot",  config_get_str, &config->adminroot_dir },
        { "alias",      _parse_alias,   config },
        { NULL, NULL, NULL }
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}

static int _parse_directory (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;

    struct cfg_tag icecast_tags[] =
    {
        { "yp-url",         config_get_str, &config->yp_url [config->num_yp_directories]},
        { "yp-url-timeout", config_get_int, &config->yp_url_timeout [config->num_yp_directories]},
        { "touch-interval", config_get_int, &config->yp_touch_interval [config->num_yp_directories]},
        { NULL, NULL, NULL }
    };

    if (config->num_yp_directories >= MAX_YP_DIRECTORIES)
    {
        ERROR0("Maximum number of yp directories exceeded!");
        return -1;
    }

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    config->num_yp_directories++;
    return 0;
}


static int _parse_mount (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    mount_proxy *mount = calloc(1, sizeof(mount_proxy));

    struct cfg_tag icecast_tags[] =
    {
        { "mount-name",     config_get_str,     &mount->mountname },
        { "source-timeout", config_get_int,     &mount->source_timeout },
        { "queue-size",     config_get_int,     &mount->queue_size_limit },
        { "burst-size",     config_get_int,     &mount->burst_size},
        { "username",       config_get_str,     &mount->username },
        { "password",       config_get_str,     &mount->password },
        { "dump-file",      config_get_str,     &mount->dumpfile },
        { "intro",          config_get_str,     &mount->intro_filename },
        { "fallback-mount", config_get_str,     &mount->fallback_mount },
        { "fallback-override",
                            config_get_bool,    &mount->fallback_override },
        { "fallback-when-full",
                            config_get_bool,    &mount->fallback_when_full },
        { "max-listeners",  config_get_int,     &mount->max_listeners },
        { "filter-theora",  config_get_bool,    &mount->filter_theora },
        { "mp3-metadata-charset",
                            config_get_str,     &mount->mp3_charset },
        { "mp3-metadata-interval",
                            config_get_int,     &mount->mp3_meta_interval },
        { "no-mount",       config_get_bool,    &mount->no_mount },
        { "hidden",         config_get_bool,    &mount->hidden },
        { "authentication", auth_get_authenticator,
                                                &mount->auth },
        { "on-connect",     config_get_str,     &mount->on_connect },
        { "on-disconnect",  config_get_str,     &mount->on_disconnect },
        { "max-listener-duration",
                            config_get_int,     &mount->max_listener_duration },
        /* YP settings */
        { "cluster-password",
                            config_get_str,     &mount->cluster_password },
        { "stream-name",    config_get_str,     &mount->stream_name },
        { "stream-description",
                            config_get_str,     &mount->stream_description },
        { "stream-url",     config_get_str,     &mount->stream_url },
        { "genre",          config_get_str,     &mount->stream_genre },
        { "bitrate",        config_get_str,     &mount->bitrate },
        { "public",         config_get_bool,    &mount->yp_public },
        { "type",           config_get_str,     &mount->type },
        { "subtype",        config_get_str,     &mount->subtype },
        { NULL, NULL, NULL },
    };

    /* default <mount> settings */
    mount->max_listeners = -1;
    mount->burst_size = -1;
    mount->mp3_meta_interval = -1;
    mount->yp_public = -1;

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    
    if (mount->mountname == NULL)
    {
        config_clear_mount (mount);
        return -1;
    }

    mount->next = config->mounts;
    config->mounts = mount;

    return 0;
}


static int _parse_relay (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    relay_server *relay = calloc(1, sizeof(relay_server));

    struct cfg_tag icecast_tags[] =
    {
        { "server",         config_get_str,     &relay->server },
        { "port",           config_get_int,     &relay->port },
        { "mount",          config_get_str,     &relay->mount },
        { "local-mount",    config_get_str,     &relay->localmount },
        { "on-demand",      config_get_bool,    &relay->on_demand },
        { "relay-shoutcast-metadata",
                            config_get_bool,    &relay->mp3metadata },
        { "username",       config_get_str,     &relay->username },
        { "password",       config_get_str,     &relay->password },
        { "enable",         config_get_bool,    &relay->enable },
        { NULL, NULL, NULL },
    };

    relay->mp3metadata = 1;
    relay->enable = 1;
    relay->on_demand = config->on_demand;
    relay->port = config->port;

    if (parse_xml_tags (node, icecast_tags))
        return -1;

    /* check for undefined entries */
    if (relay->server == NULL)
        relay->server = (char *)xmlCharStrdup ("127.0.0.1");
    if (relay->mount == NULL)
        relay->mount = (char*)xmlCharStrdup ("/");
    if (relay->localmount == NULL)
        relay->localmount = (char*)xmlCharStrdup (relay->mount);

    relay->next = config->relay;
    config->relay = relay;

    return 0;
}

static int _parse_limits (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;

    struct cfg_tag icecast_tags[] =
    {
        { "clients",        config_get_int,    &config->client_limit },
        { "sources",        config_get_int,    &config->source_limit },
        { "queue-size",     config_get_int,    &config->queue_size_limit },
        { "burst-size",     config_get_int,    &config->burst_size },
        { "client-timeout", config_get_int,    &config->client_timeout },
        { "header-timeout", config_get_int,    &config->header_timeout },
        { "source-timeout", config_get_int,    &config->source_timeout },
        { NULL, NULL, NULL },
    };
    if (parse_xml_tags (node, icecast_tags))
        return -1;
    return 0;
}


static int _parse_listen_sock (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    listener_t *listener = &config->listeners[0];
    int i;

    for (i=0; i < MAX_LISTEN_SOCKETS; i++)
    {
        if (listener->port <= 0)
        {
            struct cfg_tag icecast_tags[] =
            {
                { "port",               config_get_int,     &listener->port },
                { "shoutcast-compat",   config_get_bool,    &listener->shoutcast_compat },
                { "bind-address",       config_get_str,     &listener->bind_address },
                { "shoutcast-mount",    config_get_str,     &listener->shoutcast_mount },
                { NULL, NULL, NULL },
            };
            if (parse_xml_tags (node, icecast_tags))
                break;
            if (listener->shoutcast_mount)
            {
                listener_t *sc_port = listener+1;
                if (i+1 < MAX_LISTEN_SOCKETS && sc_port->port <= 0)
                {
                    sc_port->port = listener->port+1;
                    sc_port->shoutcast_compat = 1;
                    sc_port->bind_address = xmlStrdup (listener->bind_address);
                    sc_port->shoutcast_mount= xmlStrdup (listener->shoutcast_mount);
                }
            }
            if (config->port == 0)
                config->port = listener->port;
            return 0;
        }
        listener++;
    }
    return -1;
}


static int _parse_root (xmlNodePtr node, ice_config_t *config)
{
    struct cfg_tag icecast_tags[] =
    {
        { "location",           config_get_str,     &config->location },
        { "admin",              config_get_str,     &config->admin },
        { "server_id",          config_get_str,     &config->server_id },
        { "source-password",    config_get_str,     &config->source_password },
        { "hostname",           config_get_str,     &config->hostname },
        { "port",               config_get_int,     &config->port },
        { "fileserve",          config_get_bool,    &config->fileserve },
        { "relays-on-demand",   config_get_bool,    &config->on_demand },
        { "master-server",      config_get_str,     &config->master_server },
        { "master-username",    config_get_str,     &config->master_username },
        { "master-password",    config_get_str,     &config->master_password },
        { "master-server-port", config_get_int,     &config->master_server_port },
        { "master-update-interval",
                                config_get_int,     &config->master_update_interval },
        { "master-relay-auth",  config_get_bool,    &config->master_relay_auth },
        { "master-ssl-port",    config_get_int,     &config->master_ssl_port },
        { "master-redirect",    config_get_bool,    &config->master_redirect },
        { "max-redirect-slaves",
                                config_get_int,     &config->max_redirects },
        { "shoutcast-mount",    config_get_str,     &config->shoutcast_mount },
        { "listen-socket",      _parse_listen_sock, config },
        { "limits",             _parse_limits,      config },
        { "relay",              _parse_relay,       config },
        { "mount",              _parse_mount,       config },
        { "directory",          _parse_directory,   config },
        { "paths",              _parse_paths,       config },
        { "logging",            _parse_logging,     config },
        { "security",           _parse_security,    config },
        { "authentication",     _parse_authentication, config},
        { NULL, NULL, NULL }
    };
    if (parse_xml_tags (node, icecast_tags))
        return -1;

    if (config->max_redirects == 0 && config->master_redirect)
        config->max_redirects = 1;
    return 0;
}


/* return the mount details that match the supplied mountpoint */
mount_proxy *config_find_mount (ice_config_t *config, const char *mount)
{
    mount_proxy *mountinfo = config->mounts, *global = NULL;

    if (mount == NULL)
        return NULL;
    while (mountinfo)
    {
        if (xmlStrcmp (XMLSTR(mountinfo->mountname), XMLSTR ("all")) == 0)
            global = mountinfo;
        if (xmlStrcmp (XMLSTR(mountinfo->mountname), XMLSTR(mount)) == 0)
            break;
        mountinfo = mountinfo->next;
    }
    if (mountinfo == NULL)
        mountinfo = global;
    return mountinfo;
}

