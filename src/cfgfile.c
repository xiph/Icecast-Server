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

#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif
#include "thread/thread.h"
#include "cfgfile.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h" 
#include "global.h"

#define CATMODULE "cfgfile"
#define CONFIG_DEFAULT_LOCATION "Earth"
#define CONFIG_DEFAULT_ADMIN "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT 256
#define CONFIG_DEFAULT_SOURCE_LIMIT 16
#define CONFIG_DEFAULT_QUEUE_SIZE_LIMIT (500*1024)
#define CONFIG_DEFAULT_BURST_SIZE (64*1024)
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
    thread_mutex_create(&_locks.relay_lock);
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
    if (xmlIsBlankNode (node) == 0)
    {
        char *str = (char *)xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
        if (str == NULL)
            return 1;
        if (strcasecmp (str, "true") == 0)
            *(int*)x = 1;
        else
            if (strcasecmp (str, "yes") == 0)
                *(int*)x = 1;
            else
                *(int*)x = strtol (str, NULL, 0)==0 ? 0 : 1;
        xmlFree (str);
    }
    return 0;
}

int config_get_str (xmlNodePtr node, void *x)
{
    if (xmlIsBlankNode (node) == 0)
    {
        xmlChar *str = xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
        xmlChar *p = *(xmlChar**)x;
        if (str == NULL)
            return 1;
        if (p)
            xmlFree (p);
        *(xmlChar **)x = str;
    }
    return 0;
}

int config_get_int (xmlNodePtr node, void *x)
{
    if (xmlIsBlankNode (node) == 0)
    {
        xmlChar *str = xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
        if (str == NULL)
            return 1;
        *(int*)x = strtol ((char*)str, NULL, 0);
        xmlFree (str);
    }
    return 0;
}

int config_get_bitrate (xmlNodePtr node, void *x)
{
    if (xmlIsBlankNode (node) == 0)
    {
        xmlChar *str = xmlNodeListGetString (node->doc, node->xmlChildrenNode, 1);
        int64_t *p = (int64_t*)x;
        char metric = '\0';

        if (str == NULL)
            return 1;
        sscanf ((char*)str, "%"SCNd64 "%c", p, &metric);
        if (metric == 'k' || metric == 'K')
            (*p) *= 1024;
        if (metric == 'm' || metric == 'M')
            (*p) *= 1024*1024;
        xmlFree (str);
    }
    return 0;
}


int parse_xml_tags (xmlNodePtr parent, const struct cfg_tag *args)
{
    int ret = 0, seen_element = 0;
    xmlNodePtr node = parent->xmlChildrenNode;

    for (; node != NULL && ret == 0; node = node->next)
    {
        const struct cfg_tag *argp;

        if (xmlIsBlankNode (node) || node->type != XML_ELEMENT_NODE)
            continue;
        seen_element = 1;
        argp = args;
        while (argp->name)
        {
            if (strcmp ((const char*)node->name, argp->name) == 0)
            {
                ret = argp->retrieve (node, argp->storage);
                if (ret > 0)
                {
                    if (ret == 2)
                    {
                        argp++;
                        ret = 0;
                        continue;
                    }
                    xmlParserWarning (NULL, "skipping element \"%s\" parsing \"%s\" "
                            "at line %ld\n", node->name, parent->name, xmlGetLineNo(node));
                    ret = 0;
                }
                break;
            }
            argp++;
        }
        if (argp->name == NULL)
            WARN3 ("unknown element \"%s\" parsing \"%s\" at line %ld", node->name,
                    parent->name, xmlGetLineNo(node));
    }
    if (ret == 0 && seen_element == 0)
        return 2;
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


relay_server *config_clear_relay (relay_server *relay)
{
    relay_server *next = relay->next;
    while (relay->masters)
    {
        relay_server_master *master = relay->masters;
        relay->masters = master->next;
        if (master->ip) xmlFree (master->ip);
        if (master->bind) xmlFree (master->bind);
        if (master->mount) xmlFree (master->mount);
        free (master);
    }
    if (relay->localmount)  xmlFree (relay->localmount);
    if (relay->username)    xmlFree (relay->username);
    if (relay->password)    xmlFree (relay->password);
    free (relay);
    return next;
}


static void config_clear_mount (mount_proxy *mount)
{
    config_options_t *option;

    if (mount->username)    xmlFree (mount->username);
    if (mount->password)    xmlFree (mount->password);
    if (mount->dumpfile)    xmlFree (mount->dumpfile);
    if (mount->intro_filename) xmlFree (mount->intro_filename);
    if (mount->on_connect)  xmlFree (mount->on_connect);
    if (mount->on_disconnect) xmlFree (mount->on_disconnect);
    if (mount->fallback_mount) xmlFree (mount->fallback_mount);
    if (mount->stream_name) xmlFree (mount->stream_name);
    if (mount->stream_description)  xmlFree (mount->stream_description);
    if (mount->stream_url)  xmlFree (mount->stream_url);
    if (mount->stream_genre) xmlFree (mount->stream_genre);
    if (mount->bitrate)     xmlFree (mount->bitrate);
    if (mount->type)        xmlFree (mount->type);
    if (mount->subtype)     xmlFree (mount->subtype);
    if (mount->charset)     xmlFree (mount->charset);
    if (mount->cluster_password) xmlFree (mount->cluster_password);

    if (mount->auth_type)   xmlFree (mount->auth_type);
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
    if (mount->access_log.logid >= 0)
        log_close (mount->access_log.logid);
    xmlFree (mount->access_log.name);
    xmlFree (mount->access_log.exclude_ext);
    xmlFree (mount->mountname);
    free (mount);
}

listener_t *config_clear_listener (listener_t *listener)
{
    listener_t *next = NULL;
    if (listener)
    {
        next = listener->next;
        listener->refcount--;
        if (listener->refcount == 0)
        {
            if (listener->bind_address)     xmlFree (listener->bind_address);
            if (listener->shoutcast_mount)  xmlFree (listener->shoutcast_mount);
            free (listener);
        }
    }
    return next;
}

void config_clear(ice_config_t *c)
{
    ice_config_dir_t *dirnode, *nextdirnode;
    aliases *alias, *nextalias;
    int i;

    free(c->config_filename);

    xmlFree (c->server_id);
    if (c->location) xmlFree(c->location);
    if (c->admin) xmlFree(c->admin);
    if (c->source_password) xmlFree(c->source_password);
    if (c->admin_username) xmlFree(c->admin_username);
    if (c->admin_password) xmlFree(c->admin_password);
    if (c->relay_username) xmlFree(c->relay_username);
    if (c->relay_password) xmlFree(c->relay_password);
    if (c->hostname) xmlFree(c->hostname);
    if (c->base_dir) xmlFree(c->base_dir);
    if (c->log_dir) xmlFree(c->log_dir);
    if (c->webroot_dir) xmlFree(c->webroot_dir);
    if (c->adminroot_dir) xmlFree(c->adminroot_dir);
    if (c->cert_file) xmlFree(c->cert_file);
    if (c->pidfile) xmlFree(c->pidfile);
    if (c->banfile) xmlFree(c->banfile);
    if (c->allowfile) xmlFree (c->allowfile);
    if (c->agentfile) xmlFree (c->agentfile);
    if (c->playlist_log.name) xmlFree(c->playlist_log.name);
    if (c->access_log.name) xmlFree(c->access_log.name);
    if (c->error_log.name) xmlFree(c->error_log.name);
    if (c->access_log.exclude_ext) xmlFree (c->access_log.exclude_ext);
    if (c->shoutcast_mount) xmlFree(c->shoutcast_mount);

    global_lock();
    while ((c->listen_sock = config_clear_listener (c->listen_sock)))
        ;
    global_unlock();

    if (c->master_server) xmlFree(c->master_server);
    if (c->master_username) xmlFree(c->master_username);
    if (c->master_password) xmlFree(c->master_password);
    if (c->master_bind) xmlFree(c->master_bind);
    if (c->user) xmlFree(c->user);
    if (c->group) xmlFree(c->group);
    if (c->mimetypes_fn) xmlFree (c->mimetypes_fn);

    while (c->relay)
        c->relay = config_clear_relay (c->relay);

    while (c->mounts)
    {
        mount_proxy *to_go = c->mounts;
        c->mounts = to_go->next;
        config_clear_mount (to_go);
    }
    alias = c->aliases;
    while(alias) {
        nextalias = alias->next;
        if (alias->source) xmlFree(alias->source);
        if (alias->destination) xmlFree(alias->destination);
        if (alias->bind_address) xmlFree(alias->bind_address);
        free(alias);
        alias = nextalias;
    }

    dirnode = c->dir_list;
    while(dirnode) {
        nextdirnode = dirnode->next;
        if (dirnode->host) xmlFree(dirnode->host);
        free(dirnode);
        dirnode = nextdirnode;
    }
#ifdef USE_YP
    i = 0;
    while (i < c->num_yp_directories)
    {
        if (c->yp_url[i]) xmlFree (c->yp_url[i]);
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
void config_set_config (ice_config_t *new_config, ice_config_t *old_config)
{
    if (old_config)
        memcpy (old_config, &_current_configuration, sizeof(ice_config_t));
    memcpy(&_current_configuration, new_config, sizeof(ice_config_t));
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
    configuration->workers_count = 1;
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
    configuration->port = 0;
    configuration->master_server = NULL;
    configuration->master_server_port = 0;
    configuration->master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
    configuration->master_username = (char*)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->master_password = NULL;
    configuration->master_bind = NULL;
    configuration->master_relay_auth = 0;
    configuration->base_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_BASE_DIR);
    configuration->log_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_LOG_DIR);
    configuration->webroot_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_WEBROOT_DIR);
    configuration->adminroot_dir = (char *)xmlCharStrdup (CONFIG_DEFAULT_ADMINROOT_DIR);
    configuration->playlist_log.name = (char *)xmlCharStrdup (CONFIG_DEFAULT_PLAYLIST_LOG);
    configuration->access_log.name = (char *)xmlCharStrdup (CONFIG_DEFAULT_ACCESS_LOG);
    configuration->access_log.log_ip = 1;
    configuration->error_log.name = (char *)xmlCharStrdup (CONFIG_DEFAULT_ERROR_LOG);
    configuration->error_log.level = CONFIG_DEFAULT_LOG_LEVEL;
    configuration->chroot = CONFIG_DEFAULT_CHROOT;
    configuration->chuid = CONFIG_DEFAULT_CHUID;
    configuration->user = NULL;
    configuration->group = NULL;
    configuration->num_yp_directories = 0;
    configuration->slaves_count = 0;
    configuration->relay_username = (char *)xmlCharStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->relay_password = NULL;
    /* default to a typical prebuffer size used by clients */
    configuration->min_queue_size = 0;
    configuration->burst_size = CONFIG_DEFAULT_BURST_SIZE;
}


static int _parse_alias (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    aliases **cur, *alias = calloc (1, sizeof (aliases));
    xmlChar *temp;
    
    alias->source = (char *)xmlGetProp (node, XMLSTR ("source"));
    alias->destination = (char *)xmlGetProp (node, XMLSTR ("dest"));
    if (alias->source == NULL || alias->destination == NULL)
    {
        if (alias->source) xmlFree (alias->source);
        if (alias->destination) xmlFree (alias->destination);
        free (alias);
        WARN0 ("incomplete alias definition");
        return -1;
    }
    alias->bind_address = (char *)xmlGetProp (node, XMLSTR("bind-address"));
    temp = xmlGetProp(node, XMLSTR("port"));
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
    config->chuid = 1;
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

static int _parse_accesslog (xmlNodePtr node, void *arg)
{
    access_log *log = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "name",           config_get_str,     &log->name },
        { "ip",             config_get_bool,    &log->log_ip },
        { "archive",        config_get_bool,    &log->archive },
        { "exclude_ext",    config_get_str,     &log->exclude_ext },
        { "display",        config_get_int,     &log->display },
        { "size",           config_get_int,     &log->size },
        { NULL, NULL, NULL }
    };

    log->logid = -1;
    return parse_xml_tags (node, icecast_tags);
}

static int _parse_errorlog (xmlNodePtr node, void *arg)
{
    error_log *log = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "name",           config_get_str,     &log->name },
        { "archive",        config_get_bool,    &log->archive },
        { "display",        config_get_int,     &log->display },
        { "level",          config_get_int,     &log->level },
        { "size",           config_get_int,     &log->size },
        { NULL, NULL, NULL }
    };

    log->logid = -1;
    log->level = 3;
    return parse_xml_tags (node, icecast_tags);
}

static int _parse_playlistlog (xmlNodePtr node, void *arg)
{
    playlist_log *log = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "name",           config_get_str,     &log->name },
        { "archive",        config_get_bool,    &log->archive },
        { "display",        config_get_int,     &log->display },
        { "size",           config_get_int,     &log->size },
        { NULL, NULL, NULL }
    };

    log->logid = -1;
    return parse_xml_tags (node, icecast_tags);
}

static int _parse_logging (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    int old_trigger_size = -1, old_archive = -1;
    struct cfg_tag icecast_tags[] =
    {
        { "accesslog",      _parse_accesslog,   &config->access_log },
        { "errorlog",       _parse_errorlog,    &config->error_log },
        { "playlistlog",    _parse_playlistlog, &config->playlist_log },
        { "accesslog",      config_get_str,     &config->access_log.name },
        { "accesslog_ip",   config_get_bool,    &config->access_log.log_ip },
        { "accesslog_exclude_ext",
                            config_get_str,     &config->access_log.exclude_ext },
        { "accesslog_lines",
                            config_get_int,     &config->access_log.display },
        { "errorlog",       config_get_str,     &config->error_log },
        { "errorlog_lines", config_get_int,     &config->error_log.display },
        { "loglevel",       config_get_int,     &config->error_log.level },
        { "playlistlog",    config_get_str,     &config->playlist_log },
        { "playlistlog_lines",
                            config_get_int,     &config->playlist_log.display },
        { "logsize",        config_get_int,     &old_trigger_size },
        { "logarchive",     config_get_bool,    &old_archive },
        { NULL, NULL, NULL }
    };

    config->access_log.logid = -1;
    config->access_log.display = 100;
    config->access_log.archive = -1;
    config->error_log.logid = -1;
    config->error_log.display = 100;
    config->error_log.archive = -1;
    config->playlist_log.logid = -1;
    config->playlist_log.display = 10;

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    if (old_trigger_size > 0)
    {
        if (config->error_log.size == 0)
            config->error_log.size = old_trigger_size;
        if (config->access_log.size == 0)
            config->access_log.size = old_trigger_size;
    }
    if (old_archive > -1)
    {
        if (config->error_log.archive == -1)
            config->error_log.archive = old_archive;
        if (config->access_log.archive == -1)
            config->access_log.archive = old_archive;
    }
    return 0;
}

static int _parse_paths (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    struct cfg_tag icecast_tags[] =
    {
        { "basedir",        config_get_str, &config->base_dir },
        { "logdir",         config_get_str, &config->log_dir },
        { "mime-types",     config_get_str, &config->mimetypes_fn },
        { "pidfile",        config_get_str, &config->pidfile },
        { "banfile",        config_get_str, &config->banfile },
        { "ban-file",       config_get_str, &config->banfile },
        { "deny-ip",        config_get_str, &config->banfile },
        { "allow-ip",       config_get_str, &config->allowfile },
        { "deny-agents",    config_get_str, &config->agentfile },
        { "ssl-certificate",config_get_str, &config->cert_file },
        { "ssl_certificate",config_get_str, &config->cert_file },
        { "webroot",        config_get_str, &config->webroot_dir },
        { "adminroot",      config_get_str, &config->adminroot_dir },
        { "alias",          _parse_alias,   config },
        { NULL, NULL, NULL }
    };

    config->mimetypes_fn = (char *)xmlCharStrdup (MIMETYPESFILE);
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
        { "mount-name",         config_get_str,     &mount->mountname },
        { "source-timeout",     config_get_int,     &mount->source_timeout },
        { "queue-size",         config_get_int,     &mount->queue_size_limit },
        { "burst-size",         config_get_int,     &mount->burst_size},
        { "min-queue-size",     config_get_int,     &mount->min_queue_size},
        { "username",           config_get_str,     &mount->username },
        { "password",           config_get_str,     &mount->password },
        { "dump-file",          config_get_str,     &mount->dumpfile },
        { "intro",              config_get_str,     &mount->intro_filename },
        { "file-seekable",      config_get_bool,    &mount->file_seekable },
        { "fallback-mount",     config_get_str,     &mount->fallback_mount },
        { "fallback-override",  config_get_bool,    &mount->fallback_override },
        { "fallback-when-full", config_get_bool,    &mount->fallback_when_full },
        { "max-listeners",      config_get_int,     &mount->max_listeners },
        { "max-bandwidth",      config_get_bitrate, &mount->max_bandwidth },
        { "wait-time",          config_get_int,     &mount->wait_time },
        { "filter-theora",      config_get_bool,    &mount->filter_theora },
        { "limit-rate",         config_get_bitrate, &mount->limit_rate },
        { "skip-accesslog",     config_get_bool,    &mount->skip_accesslog },
        { "charset",            config_get_str,     &mount->charset },
        { "qblock-size",        config_get_int,     &mount->queue_block_size },
        { "redirect",           config_get_str,     &mount->redirect },
        { "metadata-interval",  config_get_int,     &mount->mp3_meta_interval },
        { "mp3-metadata-interval",
                                config_get_int,     &mount->mp3_meta_interval },
        { "ogg-passthrough",    config_get_bool,    &mount->ogg_passthrough },
        { "admin_comments_only",config_get_bool,    &mount->admin_comments_only },
        { "allow-url-ogg-metadata",
                                config_get_bool,    &mount->url_ogg_meta },
        { "no-mount",           config_get_bool,    &mount->no_mount },
        { "ban-client",         config_get_int,     &mount->ban_client },
        { "so-sndbuf",          config_get_int,     &mount->so_sndbuf },
        { "hidden",             config_get_bool,    &mount->hidden },
        { "authentication",     auth_get_authenticator, &mount->auth },
        { "on-connect",         config_get_str,     &mount->on_connect },
        { "on-disconnect",      config_get_str,     &mount->on_disconnect },
        { "max-stream-duration",
                                config_get_int,     &mount->max_stream_duration },
        { "max-listener-duration",
                                config_get_int,     &mount->max_listener_duration },
        { "accesslog",          _parse_accesslog,   &mount->access_log },
        /* YP settings */
        { "cluster-password",   config_get_str,     &mount->cluster_password },
        { "stream-name",        config_get_str,     &mount->stream_name },
        { "stream-description", config_get_str,     &mount->stream_description },
        { "stream-url",         config_get_str,     &mount->stream_url },
        { "genre",              config_get_str,     &mount->stream_genre },
        { "bitrate",            config_get_str,     &mount->bitrate },
        { "public",             config_get_bool,    &mount->yp_public },
        { "type",               config_get_str,     &mount->type },
        { "subtype",            config_get_str,     &mount->subtype },
        { NULL, NULL, NULL },
    };

    /* default <mount> settings */
    mount->max_listeners = -1;
    mount->max_bandwidth = -1;
    mount->burst_size = -1;
    mount->min_queue_size = -1;
    mount->mp3_meta_interval = -1;
    mount->yp_public = -1;
    mount->url_ogg_meta = 1;
    mount->source_timeout = config->source_timeout;
    mount->file_seekable = 1;
    mount->access_log.logid = -1;
    mount->access_log.log_ip = 1;

    if (parse_xml_tags (node, icecast_tags))
        return -1;
    
    if (mount->mountname == NULL)
    {
        config_clear_mount (mount);
        return -1;
    }
    if (mount->auth)
        mount->auth->mount = strdup (mount->mountname);
    if (mount->admin_comments_only)
        mount->url_ogg_meta = 1;
    if (mount->url_ogg_meta)
        mount->ogg_passthrough = 0;
    if (mount->min_queue_size < 0)
        mount->min_queue_size = mount->burst_size;
    if (mount->queue_block_size < 100)
        mount->queue_block_size = 1400;
    if (mount->ban_client < 0)
        mount->no_mount = 0;

    mount->next = config->mounts;
    config->mounts = mount;

    return 0;
}


static int _relay_master (xmlNodePtr node, void *arg)
{
    relay_server *relay = arg;
    relay_server_master *last, *master = calloc (1, sizeof (relay_server_master));

    struct cfg_tag icecast_tags[] =
    {
        { "ip",             config_get_str,     &master->ip },
        { "server",         config_get_str,     &master->ip },
        { "port",           config_get_int,     &master->port },
        { "mount",          config_get_str,     &master->mount },
        { "bind",           config_get_str,     &master->bind },
        { "timeout",        config_get_int,     &master->timeout },
        { NULL, NULL, NULL },
    };

    /* default master details taken from the default relay settings */
    master->ip = (char *)xmlCharStrdup (relay->masters->ip);
    master->mount = (char *)xmlCharStrdup (relay->masters->mount);
    if (relay->masters->bind)
        master->bind = (char *)xmlCharStrdup (relay->masters->bind);
    master->port = relay->masters->port;
    master->timeout = relay->masters->timeout;

    if (parse_xml_tags (node, icecast_tags))
        return -1;

    if (master->port < 1 || master->port > 65535)
        master->port = 8000;
    if (master->timeout < 1 || master->timeout > 60)
        master->timeout = 4;

    /* place new details at the end of the list */
    last = relay->masters;
    while (last->next)
        last = last->next;
    last->next = master;

    return 0;
}


static int _parse_relay (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    relay_server *relay = calloc(1, sizeof(relay_server));
    relay_server_master *master = calloc (1, sizeof (relay_server_master));

    struct cfg_tag icecast_tags[] =
    {
        { "master",         _relay_master,      relay },
        { "server",         config_get_str,     &master->ip },
        { "ip",             config_get_str,     &master->ip },
        { "bind",           config_get_str,     &master->bind },
        { "port",           config_get_int,     &master->port },
        { "mount",          config_get_str,     &master->mount },
        { "timeout",        config_get_int,     &master->timeout },
        { "local-mount",    config_get_str,     &relay->localmount },
        { "on-demand",      config_get_bool,    &relay->on_demand },
        { "retry-delay",    config_get_int,     &relay->interval },
        { "relay-shoutcast-metadata",
                            config_get_bool,    &relay->mp3metadata },
        { "username",       config_get_str,     &relay->username },
        { "password",       config_get_str,     &relay->password },
        { "enable",         config_get_bool,    &relay->running },
        { NULL, NULL, NULL },
    };

    relay->mp3metadata = 1;
    relay->running = 1;
    relay->interval = config->master_update_interval;
    relay->on_demand = config->on_demand;
    relay->masters = master;
    /* default settings */
    master->port = config->port;
    master->ip = (char *)xmlCharStrdup ("127.0.0.1");
    master->mount = (char*)xmlCharStrdup ("/");
    master->timeout = 4;

    if (parse_xml_tags (node, icecast_tags))
        return -1;

    /* check for unspecified entries */
    if (relay->localmount == NULL)
        relay->localmount = (char*)xmlCharStrdup (master->mount);

    /* if master is set then remove the default entry at the head of the list */ 
    if (relay->masters->next)
    {
        relay->masters = relay->masters->next;
        if (master->mount)  xmlFree (master->mount);
        if (master->ip)     xmlFree (master->ip);
        if (master->bind)   xmlFree (master->bind);
        free (master);
    }

    relay->next = config->relay;
    config->relay = relay;

    return 0;
}

static int _parse_limits (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;

    struct cfg_tag icecast_tags[] =
    {
        { "max-bandwidth",  config_get_bitrate,&config->max_bandwidth },
        { "clients",        config_get_int,    &config->client_limit },
        { "sources",        config_get_int,    &config->source_limit },
        { "queue-size",     config_get_int,    &config->queue_size_limit },
        { "min-queue-size", config_get_int,    &config->min_queue_size },
        { "burst-size",     config_get_int,    &config->burst_size },
        { "workers",        config_get_int,    &config->workers_count },
        { "client-timeout", config_get_int,    &config->client_timeout },
        { "header-timeout", config_get_int,    &config->header_timeout },
        { "source-timeout", config_get_int,    &config->source_timeout },
        { NULL, NULL, NULL },
    };
    if (parse_xml_tags (node, icecast_tags))
        return -1;
    if (config->workers_count < 1)   config->workers_count = 1;
    if (config->workers_count > 400) config->workers_count = 400;
    return 0;
}

static int _parse_master (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;

    struct cfg_tag icecast_tags[] =
    {
        { "server",             config_get_str,     &config->master_server },
        { "port",               config_get_int,     &config->master_server_port },
        { "ssl-port",           config_get_int,     &config->master_ssl_port },
        { "username",           config_get_str,     &config->master_username },
        { "password",           config_get_str,     &config->master_password },
        { "bind",               config_get_str,     &config->master_bind },
        { "interval",           config_get_int,     &config->master_update_interval },
        { "relay-auth",         config_get_bool,    &config->master_relay_auth },
        { "redirect",           config_get_bool,    &config->master_redirect },
        { NULL, NULL, NULL },
    };

    if (parse_xml_tags (node, icecast_tags))
        return -1;

    return 0;
}


static int _parse_listen_sock (xmlNodePtr node, void *arg)
{
    ice_config_t *config = arg;
    listener_t *listener = calloc (1, sizeof(listener_t));

    struct cfg_tag icecast_tags[] =
    {
        { "port",               config_get_int,     &listener->port },
        { "shoutcast-compat",   config_get_bool,    &listener->shoutcast_compat },
        { "bind-address",       config_get_str,     &listener->bind_address },
        { "queue-len",          config_get_int,     &listener->qlen },
        { "so-sndbuf",          config_get_int,     &listener->so_sndbuf },
        { "ssl",                config_get_bool,    &listener->ssl },
        { "shoutcast-mount",    config_get_str,     &listener->shoutcast_mount },
        { NULL, NULL, NULL },
    };

    listener->refcount = 1;
    listener->port = 8000;
    listener->qlen = ICE_LISTEN_QUEUE;
    if (parse_xml_tags (node, icecast_tags))
    {
        config_clear_listener (listener);
        return -1;
    }

    if (listener->qlen < 1)
        listener->qlen = ICE_LISTEN_QUEUE;
    listener->next = config->listen_sock;
    config->listen_sock = listener;
    config->listen_sock_count++;

    if (listener->shoutcast_mount)
    {
        listener_t *sc_port = calloc (1, sizeof (listener_t));
        sc_port->refcount = 1;
        sc_port->port = listener->port+1;
        sc_port->qlen = listener->qlen;
        sc_port->shoutcast_compat = 1;
        sc_port->shoutcast_mount = (char*)xmlStrdup (XMLSTR(listener->shoutcast_mount));
        if (listener->bind_address)
            sc_port->bind_address = (char*)xmlStrdup (XMLSTR(listener->bind_address));

        sc_port->next = config->listen_sock;
        config->listen_sock = sc_port;
        config->listen_sock_count++;
    }
    else
        listener->shoutcast_mount = (char*)xmlStrdup (XMLSTR(config->shoutcast_mount));

    if (config->port == 0)
        config->port = listener->port;
    return 0;
}


static int _parse_root (xmlNodePtr node, ice_config_t *config)
{
    char *bindaddress = NULL;
    struct cfg_tag icecast_tags[] =
    {
        { "location",           config_get_str,     &config->location },
        { "admin",              config_get_str,     &config->admin },
        { "server_id",          config_get_str,     &config->server_id },
        { "server-id",          config_get_str,     &config->server_id },
        { "source-password",    config_get_str,     &config->source_password },
        { "hostname",           config_get_str,     &config->hostname },
        { "port",               config_get_int,     &config->port },
        { "bind-address",       config_get_str,     &bindaddress },
        { "fileserve",          config_get_bool,    &config->fileserve },
        { "relays-on-demand",   config_get_bool,    &config->on_demand },
        { "master-server",      config_get_str,     &config->master_server },
        { "master-username",    config_get_str,     &config->master_username },
        { "master-password",    config_get_str,     &config->master_password },
        { "master-bind",        config_get_str,     &config->master_bind },
        { "master-server-port", config_get_int,     &config->master_server_port },
        { "master-update-interval",
                                config_get_int,     &config->master_update_interval },
        { "master-relay-auth",  config_get_bool,    &config->master_relay_auth },
        { "master-ssl-port",    config_get_int,     &config->master_ssl_port },
        { "master-redirect",    config_get_bool,    &config->master_redirect },
        { "max-redirect-slaves",config_get_int,     &config->max_redirects },
        { "shoutcast-mount",    config_get_str,     &config->shoutcast_mount },
        { "listen-socket",      _parse_listen_sock, config },
        { "limits",             _parse_limits,      config },
        { "relay",              _parse_relay,       config },
        { "mount",              _parse_mount,       config },
        { "master",             _parse_master,      config },
        { "directory",          _parse_directory,   config },
        { "paths",              _parse_paths,       config },
        { "logging",            _parse_logging,     config },
        { "security",           _parse_security,    config },
        { "authentication",     _parse_authentication, config},
        { NULL, NULL, NULL }
    };

    config->master_relay_auth = 1;
    if (parse_xml_tags (node, icecast_tags))
        return -1;

    if (config->max_redirects == 0 && config->master_redirect)
        config->max_redirects = 1;
    if (config->listen_sock_count == 0)
    {
        if (config->port)
        {
            listener_t *listener = calloc (1, sizeof(listener_t));
            listener->refcount = 1;
            listener->port = config->port;
            listener->qlen = ICE_LISTEN_QUEUE;
            listener->bind_address = (char*)xmlStrdup (XMLSTR(bindaddress));
            listener->next = config->listen_sock;
            config->listen_sock = listener;
            config->listen_sock_count++;
        }
        else
        {
            WARN0 ("No listen-socket defintions");
            return -1;
        }
    }
    return 0;
}


/* return the mount details that match the supplied mountpoint */
mount_proxy *config_find_mount (ice_config_t *config, const char *mount)
{
    mount_proxy *mountinfo = config->mounts, *to_return = NULL;

    if (mount == NULL)
        return NULL;
    while (mountinfo)
    {
        if (fnmatch (mountinfo->mountname, mount, FNM_PATHNAME) == 0)
            to_return = mountinfo;
        mountinfo = mountinfo->next;
    }
    if (mountinfo == NULL)
        mountinfo = to_return;
    return mountinfo;
}

