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
#define CONFIG_DEFAULT_LOG_LEVEL 3
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
#define MIMETYPESFILE "/etc/mime.types"
#else
#define CONFIG_DEFAULT_BASE_DIR ".\\"
#define CONFIG_DEFAULT_LOG_DIR ".\\logs"
#define CONFIG_DEFAULT_WEBROOT_DIR ".\\webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR ".\\admin"
#define MIMETYPESFILE ".\\mime.types"
#endif

static ice_config_t _current_configuration;
static ice_config_locks _locks;

static void _set_defaults(ice_config_t *c);
static void _parse_root(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_limits(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_directory(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_paths(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_logging(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_security(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *c);
static void _parse_relay(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_mount(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_listen_socket(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *c);
static void _add_server(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);

static void create_locks(void) {
    thread_mutex_create(&_locks.relay_lock);
    thread_rwlock_create(&_locks.config_lock);
}

static void release_locks(void) {
    thread_mutex_destroy(&_locks.relay_lock);
    thread_rwlock_destroy(&_locks.config_lock);
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

static void config_clear_mount (mount_proxy *mount)
{
    config_options_t *option;

    if (mount->mountname)       xmlFree (mount->mountname);
    if (mount->username)        xmlFree (mount->username);
    if (mount->password)        xmlFree (mount->password);
    if (mount->dumpfile)        xmlFree (mount->dumpfile);
    if (mount->intro_filename)  xmlFree (mount->intro_filename);
    if (mount->on_connect)      xmlFree (mount->on_connect);
    if (mount->on_disconnect)   xmlFree (mount->on_disconnect);
    if (mount->fallback_mount)  xmlFree (mount->fallback_mount);
    if (mount->stream_name)     xmlFree (mount->stream_name);
    if (mount->stream_description)  xmlFree (mount->stream_description);
    if (mount->stream_url)      xmlFree (mount->stream_url);
    if (mount->stream_genre)    xmlFree (mount->stream_genre);
    if (mount->bitrate)         xmlFree (mount->bitrate);
    if (mount->type)            xmlFree (mount->type);
    if (mount->charset)         xmlFree (mount->charset);
    if (mount->cluster_password)    xmlFree (mount->cluster_password);

    if (mount->auth_type)       xmlFree (mount->auth_type);
    option = mount->auth_options;
    while (option)
    {
        config_options_t *nextopt = option->next;
        if (option->name)   xmlFree (option->name);
        if (option->value)  xmlFree (option->value);
        free (option);
        option = nextopt;
    }
    auth_release (mount->auth);
    free (mount);
}

listener_t *config_clear_listener (listener_t *listener)
{
    listener_t *next = NULL;
    if (listener)
    {
        next = listener->next;
        if (listener->bind_address)     xmlFree (listener->bind_address);
        if (listener->shoutcast_mount)  xmlFree (listener->shoutcast_mount);
        free (listener);
    }
    return next;
}

void config_clear(ice_config_t *c)
{
    ice_config_dir_t *dirnode, *nextdirnode;
    relay_server *relay, *nextrelay;
    mount_proxy *mount, *nextmount;
    aliases *alias, *nextalias;
    int i;

    free(c->config_filename);

    xmlFree (c->server_id);
    if (c->location) xmlFree(c->location);
    if (c->admin) xmlFree(c->admin);
    if (c->source_password) xmlFree(c->source_password);
    if (c->admin_username)
        xmlFree(c->admin_username);
    if (c->admin_password)
        xmlFree(c->admin_password);
    if (c->relay_username)
        xmlFree(c->relay_username);
    if (c->relay_password)
        xmlFree(c->relay_password);
    if (c->hostname) xmlFree(c->hostname);
    if (c->base_dir) xmlFree(c->base_dir);
    if (c->log_dir) xmlFree(c->log_dir);
    if (c->webroot_dir) xmlFree(c->webroot_dir);
    if (c->adminroot_dir) xmlFree(c->adminroot_dir);
    if (c->cert_file) xmlFree(c->cert_file);
    if (c->pidfile)
        xmlFree(c->pidfile);
    if (c->banfile) xmlFree(c->banfile);
    if (c->allowfile) xmlFree(c->allowfile);
    if (c->playlist_log) xmlFree(c->playlist_log);
    if (c->access_log) xmlFree(c->access_log);
    if (c->error_log) xmlFree(c->error_log);
    if (c->shoutcast_mount) xmlFree(c->shoutcast_mount);
    if (c->master_server) xmlFree(c->master_server);
    if (c->master_username) xmlFree(c->master_username);
    if (c->master_password) xmlFree(c->master_password);
    if (c->user) xmlFree(c->user);
    if (c->group) xmlFree(c->group);
    if (c->mimetypes_fn) xmlFree (c->mimetypes_fn);

    while ((c->listen_sock = config_clear_listener (c->listen_sock)))
        ;

    thread_mutex_lock(&(_locks.relay_lock));
    relay = c->relay;
    while(relay) {
        nextrelay = relay->next;
        xmlFree(relay->server);
        xmlFree(relay->mount);
        xmlFree(relay->localmount);
        free(relay);
        relay = nextrelay;
    }
    thread_mutex_unlock(&(_locks.relay_lock));

    mount = c->mounts;
    while(mount) {
        nextmount = mount->next;
        config_clear_mount (mount);
        mount = nextmount;
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

    if (xmlStrcmp (node->name, XMLSTR("icecast")) != 0) {
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return CONFIG_EBADROOT;
    }

    config_init_configuration(configuration);

    configuration->config_filename = strdup (filename);

    _parse_root(doc, node->xmlChildrenNode, configuration);

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
    configuration->location = (char *)xmlCharStrdup (CONFIG_DEFAULT_LOCATION);
    configuration->server_id = (char *)xmlCharStrdup (ICECAST_VERSION_STRING);
    configuration->admin = (char *)xmlCharStrdup (CONFIG_DEFAULT_ADMIN);
    configuration->client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
    configuration->source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
    configuration->queue_size_limit = CONFIG_DEFAULT_QUEUE_SIZE_LIMIT;
    configuration->threadpool_size = CONFIG_DEFAULT_THREADPOOL_SIZE;
    configuration->client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    configuration->header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
    configuration->source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
    configuration->source_password = (char *)xmlCharStrdup (CONFIG_DEFAULT_SOURCE_PASSWORD);
    configuration->shoutcast_mount = (char *)xmlCharStrdup (CONFIG_DEFAULT_SHOUTCAST_MOUNT);
    configuration->ice_login = CONFIG_DEFAULT_ICE_LOGIN;
    configuration->fileserve = CONFIG_DEFAULT_FILESERVE;
    configuration->touch_interval = CONFIG_DEFAULT_TOUCH_FREQ;
    configuration->on_demand = 0;
    configuration->dir_list = NULL;
    configuration->hostname = (char *)xmlCharStrdup (CONFIG_DEFAULT_HOSTNAME);
    configuration->mimetypes_fn = (char *)xmlCharStrdup (MIMETYPESFILE);
    configuration->master_server = NULL;
    configuration->master_server_port = 0;
    configuration->master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
    configuration->master_username = (char *)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->master_password = NULL;
    configuration->base_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_BASE_DIR);
    configuration->log_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_LOG_DIR);
    configuration->webroot_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_WEBROOT_DIR);
    configuration->adminroot_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_ADMINROOT_DIR);
    configuration->playlist_log = (char *)xmlCharStrdup (CONFIG_DEFAULT_PLAYLIST_LOG);
    configuration->access_log = (char *)xmlCharStrdup (CONFIG_DEFAULT_ACCESS_LOG);
    configuration->error_log = (char *)xmlCharStrdup (CONFIG_DEFAULT_ERROR_LOG);
    configuration->loglevel = CONFIG_DEFAULT_LOG_LEVEL;
    configuration->chroot = CONFIG_DEFAULT_CHROOT;
    configuration->chuid = CONFIG_DEFAULT_CHUID;
    configuration->user = NULL;
    configuration->group = NULL;
    configuration->num_yp_directories = 0;
    configuration->relay_username = (char *)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->relay_password = NULL;
    /* default to a typical prebuffer size used by clients */
    configuration->burst_size = CONFIG_DEFAULT_BURST_SIZE;
}

static void _parse_root(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;

    configuration->listen_sock = calloc (1, sizeof (*configuration->listen_sock));
    configuration->listen_sock->port = 8000;
    configuration->listen_sock_count = 1;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("location")) == 0) {
            if (configuration->location) xmlFree(configuration->location);
            configuration->location = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("admin")) == 0) {
            if (configuration->admin) xmlFree(configuration->admin);
            configuration->admin = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("server-id")) == 0) {
            xmlFree (configuration->server_id);
            configuration->server_id = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if(xmlStrcmp (node->name, XMLSTR("authentication")) == 0) {
            _parse_authentication(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("source-password")) == 0) {
            /* TODO: This is the backwards-compatibility location */
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, XMLSTR("mount"))) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
                if (configuration->source_password) xmlFree(configuration->source_password);
                configuration->source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (xmlStrcmp (node->name, XMLSTR("icelogin")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->ice_login = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("fileserve")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->fileserve = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("relays-on-demand")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->on_demand = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("hostname")) == 0) {
            if (configuration->hostname) xmlFree(configuration->hostname);
            configuration->hostname = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("mime-types")) == 0) {
            if (configuration->mimetypes_fn) xmlFree(configuration->mimetypes_fn);
            configuration->mimetypes_fn = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("listen-socket")) == 0) {
            _parse_listen_socket(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->port = atoi(tmp);
            configuration->listen_sock->port = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("bind-address")) == 0) {
            if (configuration->listen_sock->bind_address) 
                xmlFree(configuration->listen_sock->bind_address);
            configuration->listen_sock->bind_address = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("master-server")) == 0) {
            if (configuration->master_server) xmlFree(configuration->master_server);
            configuration->master_server = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("master-username")) == 0) {
            if (configuration->master_username) xmlFree(configuration->master_username);
            configuration->master_username = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("master-password")) == 0) {
            if (configuration->master_password) xmlFree(configuration->master_password);
            configuration->master_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("master-server-port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->master_server_port = atoi(tmp);
            xmlFree (tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("master-update-interval")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->master_update_interval = atoi(tmp);
            xmlFree (tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("shoutcast-mount")) == 0) {
            if (configuration->shoutcast_mount) xmlFree(configuration->shoutcast_mount);
            configuration->shoutcast_mount = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("limits")) == 0) {
            _parse_limits(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("relay")) == 0) {
            _parse_relay(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("mount")) == 0) {
            _parse_mount(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("directory")) == 0) {
            _parse_directory(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("paths")) == 0) {
            _parse_paths(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("logging")) == 0) {
            _parse_logging(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("security")) == 0) {
            _parse_security(doc, node->xmlChildrenNode, configuration);
        }
    } while ((node = node->next));

    /* drop the first listening socket details if more than one is defined, as we only
     * have port or listen-socket not both */
    if (configuration->listen_sock_count > 1)
    {
        configuration->listen_sock = config_clear_listener (configuration->listen_sock);
        configuration->listen_sock_count--;
    }
    if (configuration->port == 0)
        configuration->port = 8000;
}

static void _parse_limits(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("clients")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->client_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("sources")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->source_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("queue-size")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->queue_size_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("threadpool")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->threadpool_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("client-timeout")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->client_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("header-timeout")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->header_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("source-timeout")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->source_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("burst-on-connect")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (atoi(tmp) == 0)
                configuration->burst_size = 0;
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("burst-size")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->burst_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
}

static void _parse_mount(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;
    mount_proxy *mount = calloc(1, sizeof(mount_proxy));
    mount_proxy *current = configuration->mounts;
    mount_proxy *last=NULL;
    
    /* default <mount> settings */
    mount->max_listeners = -1;
    mount->burst_size = -1;
    mount->mp3_meta_interval = -1;
    mount->yp_public = -1;
    mount->next = NULL;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("mount-name")) == 0) {
            mount->mountname = (char *)xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("username")) == 0) {
            mount->username = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("password")) == 0) {
            mount->password = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("dump-file")) == 0) {
            mount->dumpfile = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("intro")) == 0) {
            mount->intro_filename = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("fallback-mount")) == 0) {
            mount->fallback_mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("fallback-when-full")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->fallback_when_full = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("max-listeners")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->max_listeners = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("charset")) == 0) {
            mount->charset = (char *)xmlNodeListGetString(doc,
                    node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("mp3-metadata-interval")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->mp3_meta_interval = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("fallback-override")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->fallback_override = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("no-mount")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->no_mount = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("no-yp")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = atoi(tmp) == 0 ? -1 : 0;
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("hidden")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->hidden = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("authentication")) == 0) {
            mount->auth = auth_get_authenticator (node);
        }
        else if (xmlStrcmp (node->name, XMLSTR("on-connect")) == 0) {
            mount->on_connect = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("on-disconnect")) == 0) {
            mount->on_disconnect = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("max-listener-duration")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->max_listener_duration = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("queue-size")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->queue_size_limit = atoi (tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("source-timeout")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp)
            {
                mount->source_timeout = atoi (tmp);
                xmlFree(tmp);
            }
        } else if (xmlStrcmp (node->name, XMLSTR("burst-size")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->burst_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("cluster-password")) == 0) {
            mount->cluster_password = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("stream-name")) == 0) {
            mount->stream_name = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("stream-description")) == 0) {
            mount->stream_description = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("stream-url")) == 0) {
            mount->stream_url = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("genre")) == 0) {
            mount->stream_genre = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("bitrate")) == 0) {
            mount->bitrate = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("public")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = atoi (tmp);
            if(tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("type")) == 0) {
            mount->type = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("subtype")) == 0) {
            mount->subtype = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));

    /* make sure we have at least the mountpoint name */
    if (mount->mountname == NULL)
    {
        config_clear_mount (mount);
        return;
    }
    if (mount->auth)
        mount->auth->mount = strdup ((char *)mount->mountname);
    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = mount;
    else
        configuration->mounts = mount;
}


static void _parse_relay(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *tmp;
    relay_server *relay = calloc(1, sizeof(relay_server));
    relay_server *current = configuration->relay;
    relay_server *last=NULL;

    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = relay;
    else
        configuration->relay = relay;

    relay->next = NULL;
    relay->mp3metadata = 1;
    relay->on_demand = configuration->on_demand;
    relay->server = (char *)xmlCharStrdup ("127.0.0.1");
    relay->mount = (char *)xmlCharStrdup ("/");

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("server")) == 0) {
            if (relay->server) xmlFree (relay->server);
            relay->server = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->port = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("mount")) == 0) {
            if (relay->mount) xmlFree (relay->mount);
            relay->mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("local-mount")) == 0) {
            if (relay->localmount) xmlFree (relay->localmount);
            relay->localmount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("relay-shoutcast-metadata")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->mp3metadata = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("username")) == 0) {
            if (relay->username) xmlFree (relay->username);
            relay->username = (char *)xmlNodeListGetString(doc,
                    node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("password")) == 0) {
            if (relay->password) xmlFree (relay->password);
            relay->password = (char *)xmlNodeListGetString(doc,
                    node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("on-demand")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->on_demand = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
    if (relay->localmount == NULL)
        relay->localmount = (char *)xmlStrdup (XMLSTR(relay->mount));
}

static void _parse_listen_socket(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *tmp;
    listener_t *listener = calloc (1, sizeof(listener_t));

    if (listener == NULL)
        return;
    listener->port = 8000;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("port")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->port == 0)
                configuration->port = atoi(tmp);
            listener->port = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("ssl")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            listener->ssl = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("shoutcast-compat")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            listener->shoutcast_compat = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (xmlStrcmp (node->name, XMLSTR("shoutcast-mount")) == 0) {
            if (listener->shoutcast_mount) xmlFree (listener->shoutcast_mount);
            listener->shoutcast_mount = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
        }
        else if (xmlStrcmp (node->name, XMLSTR("bind-address")) == 0) {
            if (listener->bind_address) xmlFree (listener->bind_address);
            listener->bind_address = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));

    /* we know there's at least one of these, so add this new one after the first
     * that way it can be removed easily later on */
    listener->next = configuration->listen_sock->next;
    configuration->listen_sock->next = listener;
    configuration->listen_sock_count++;
    if (listener->shoutcast_mount)
    {
        listener_t *sc_port = calloc (1, sizeof (listener_t));
        sc_port->port = listener->port+1;
        sc_port->shoutcast_compat = 1;
        sc_port->shoutcast_mount = (char*)xmlStrdup (XMLSTR(listener->shoutcast_mount));
        if (listener->bind_address)
            sc_port->bind_address = (char*)xmlStrdup (XMLSTR(listener->bind_address));

        sc_port->next = listener->next;
        listener->next = sc_port;
        configuration->listen_sock_count++;
    }
}

static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("source-password")) == 0) {
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, XMLSTR("mount"))) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
                if (configuration->source_password)
                    xmlFree(configuration->source_password);
                configuration->source_password = 
                    (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (xmlStrcmp (node->name, XMLSTR("admin-password")) == 0) {
            if(configuration->admin_password)
                xmlFree(configuration->admin_password);
            configuration->admin_password =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("admin-user")) == 0) {
            if(configuration->admin_username)
                xmlFree(configuration->admin_username);
            configuration->admin_username =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("relay-password")) == 0) {
            if(configuration->relay_password)
                xmlFree(configuration->relay_password);
            configuration->relay_password =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("relay-user")) == 0) {
            if(configuration->relay_username)
                xmlFree(configuration->relay_username);
            configuration->relay_username =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));
}

static void _parse_directory(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *tmp;

    if (configuration->num_yp_directories >= MAX_YP_DIRECTORIES) {
        ERROR0("Maximum number of yp directories exceeded!");
        return;
    }
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("yp-url")) == 0) {
            if (configuration->yp_url[configuration->num_yp_directories]) 
                xmlFree(configuration->yp_url[configuration->num_yp_directories]);
            configuration->yp_url[configuration->num_yp_directories] = 
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("yp-url-timeout")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->yp_url_timeout[configuration->num_yp_directories] = 
                atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("server")) == 0) {
            _add_server(doc, node->xmlChildrenNode, configuration);
        } else if (xmlStrcmp (node->name, XMLSTR("touch-interval")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->yp_touch_interval[configuration->num_yp_directories] =
                atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
    if (configuration->yp_url [configuration->num_yp_directories] == NULL)
        return;
    configuration->num_yp_directories++;
}

static void _parse_paths(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *temp;
    aliases *alias, *current, *last;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("basedir")) == 0) {
            if (configuration->base_dir) xmlFree(configuration->base_dir);
            configuration->base_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("logdir")) == 0) {
            if (configuration->log_dir) xmlFree(configuration->log_dir);
            configuration->log_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("pidfile")) == 0) {
            if (configuration->pidfile) xmlFree(configuration->pidfile);
            configuration->pidfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("deny-ip")) == 0) {
            if (configuration->banfile) xmlFree(configuration->banfile);
            configuration->banfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("allow-ip")) == 0) {
            if (configuration->allowfile) xmlFree(configuration->allowfile);
            configuration->allowfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("ssl-certificate")) == 0) {
            if (configuration->cert_file) xmlFree(configuration->cert_file);
            configuration->cert_file = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("webroot")) == 0) {
            if (configuration->webroot_dir) xmlFree(configuration->webroot_dir);
            configuration->webroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->webroot_dir[strlen(configuration->webroot_dir)-1] == '/')
                configuration->webroot_dir[strlen(configuration->webroot_dir)-1] = 0;
        } else if (xmlStrcmp (node->name, XMLSTR("adminroot")) == 0) {
            if (configuration->adminroot_dir) 
                xmlFree(configuration->adminroot_dir);
            configuration->adminroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] == '/')
                configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] = 0;
        } else if (xmlStrcmp (node->name, XMLSTR("alias")) == 0) {
            alias = malloc(sizeof(aliases));
            alias->next = NULL;
            alias->source = (char *)xmlGetProp(node, XMLSTR("source"));
            if(alias->source == NULL) {
                free(alias);
                continue;
            }
            alias->destination = (char *)xmlGetProp(node, XMLSTR("dest"));
            if(alias->destination == NULL) {
                xmlFree(alias->source);
                free(alias);
                continue;
            }
            temp = NULL;
            temp = (char *)xmlGetProp(node, XMLSTR("port"));
            if(temp != NULL) {
                alias->port = atoi(temp);
                xmlFree(temp);
            }
            else
                alias->port = -1;
            alias->bind_address = (char *)xmlGetProp(node, XMLSTR("bind-address"));
            current = configuration->aliases;
            last = NULL;
            while(current) {
                last = current;
                current = current->next;
            }
            if(last)
                last->next = alias;
            else
                configuration->aliases = alias;
        }
    } while ((node = node->next));
}

static void _parse_logging(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("accesslog")) == 0) {
            if (configuration->access_log) xmlFree(configuration->access_log);
            configuration->access_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("errorlog")) == 0) {
            if (configuration->error_log) xmlFree(configuration->error_log);
            configuration->error_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("playlistlog")) == 0) {
            if (configuration->playlist_log) xmlFree(configuration->playlist_log);
            configuration->playlist_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (xmlStrcmp (node->name, XMLSTR("logsize")) == 0) {
            char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->logsize = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("loglevel")) == 0) {
           char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->loglevel = atoi(tmp);
           if (tmp) xmlFree(tmp);
        } else if (xmlStrcmp (node->name, XMLSTR("logarchive")) == 0) {
            char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->logarchive = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
}

static void _parse_security(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
   char *tmp;
   xmlNodePtr oldnode;

   do {
       if (node == NULL) break;
       if (xmlIsBlankNode(node)) continue;

       if (xmlStrcmp (node->name, XMLSTR("chroot")) == 0) {
           tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->chroot = atoi(tmp);
           if (tmp) xmlFree(tmp);
       } else if (xmlStrcmp (node->name, XMLSTR("changeowner")) == 0) {
           configuration->chuid = 1;
           oldnode = node;
           node = node->xmlChildrenNode;
           do {
               if(node == NULL) break;
               if(xmlIsBlankNode(node)) continue;
               if(xmlStrcmp (node->name, XMLSTR("user")) == 0) {
                   if(configuration->user) xmlFree(configuration->user);
                   configuration->user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               } else if(xmlStrcmp (node->name, XMLSTR("group")) == 0) {
                   if(configuration->group) xmlFree(configuration->group);
                   configuration->group = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               }
           } while((node = node->next));
           node = oldnode;
       }
   } while ((node = node->next));
}

static void _add_server(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    ice_config_dir_t *dirnode, *server;
    int addnode;
    char *tmp;

    server = (ice_config_dir_t *)malloc(sizeof(ice_config_dir_t));
    server->touch_interval = configuration->touch_interval;
    server->host = NULL;
    addnode = 0;
    
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (xmlStrcmp (node->name, XMLSTR("host")) == 0) {
            server->host = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
            addnode = 1;
        } else if (xmlStrcmp (node->name, XMLSTR("touch-interval")) == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            server->touch_interval = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
        server->next = NULL;
    } while ((node = node->next));

    if (addnode) {
        dirnode = configuration->dir_list;
        if (dirnode == NULL) {
            configuration->dir_list = server;
        } else {
            while (dirnode->next) dirnode = dirnode->next;
            
            dirnode->next = server;
        }
        
        server = NULL;
        addnode = 0;
    }
    else {
        free (server);
    }
    
}


/* return the mount details that match the supplied mountpoint */
mount_proxy *config_find_mount (ice_config_t *config, const char *mount)
{
    mount_proxy *mountinfo = config->mounts;

    while (mountinfo)
    {
        if (strcmp (mountinfo->mountname, mount) == 0)
            break;
        mountinfo = mountinfo->next;
    }
    return mountinfo;
}

/* Helper function to locate the configuration details of the listening 
 * socket
 */
listener_t *config_get_listen_sock (ice_config_t *config, connection_t *con)
{
    listener_t *listener;
    int i = 0;

    listener = config->listen_sock;
    global_lock();
    while (listener)
    {
        if (i >= global.server_sockets)
            listener = NULL;
        else
        {
            if (global.serversock[i] == con->serversock)
                break;
            listener = listener->next;
            i++;
        }
    }
    global_unlock();
    return listener;
}

