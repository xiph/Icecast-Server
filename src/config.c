#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "config.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h" 

#define CATMODULE "CONFIG"
#define CONFIG_DEFAULT_LOCATION "Earth"
#define CONFIG_DEFAULT_ADMIN "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT 256
#define CONFIG_DEFAULT_SOURCE_LIMIT 16
#define CONFIG_DEFAULT_QUEUE_SIZE_LIMIT (100*1024)
#define CONFIG_DEFAULT_THREADPOOL_SIZE 4
#define CONFIG_DEFAULT_CLIENT_TIMEOUT 30
#define CONFIG_DEFAULT_HEADER_TIMEOUT 15
#define CONFIG_DEFAULT_SOURCE_TIMEOUT 10
#define CONFIG_DEFAULT_SOURCE_PASSWORD "changeme"
#define CONFIG_DEFAULT_RELAY_PASSWORD "changeme"
#define CONFIG_DEFAULT_ICE_LOGIN 0
#define CONFIG_DEFAULT_FILESERVE 1
#define CONFIG_DEFAULT_TOUCH_FREQ 5
#define CONFIG_DEFAULT_HOSTNAME "localhost"
#define CONFIG_DEFAULT_PORT 8888
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
#else
#define CONFIG_DEFAULT_BASE_DIR ".\\"
#define CONFIG_DEFAULT_LOG_DIR ".\\logs"
#define CONFIG_DEFAULT_WEBROOT_DIR ".\\webroot"
#endif

ice_config_t _configuration;
char *_config_filename;

static void _set_defaults(void);
static void _parse_root(xmlDocPtr doc, xmlNodePtr node);
static void _parse_limits(xmlDocPtr doc, xmlNodePtr node);
static void _parse_directory(xmlDocPtr doc, xmlNodePtr node);
static void _parse_paths(xmlDocPtr doc, xmlNodePtr node);
static void _parse_logging(xmlDocPtr doc, xmlNodePtr node);
static void _parse_security(xmlDocPtr doc, xmlNodePtr node);
static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node);
static void _parse_relay(xmlDocPtr doc, xmlNodePtr node);
static void _parse_mount(xmlDocPtr doc, xmlNodePtr node);
static void _add_server(xmlDocPtr doc, xmlNodePtr node);

void config_initialize(void)
{
	memset(&_configuration, 0, sizeof(ice_config_t));
	_set_defaults();
	_config_filename = NULL;
}

void config_shutdown(void)
{
	ice_config_dir_t *dirnode, *nextdirnode;
    ice_config_t *c = &_configuration;
    relay_server *relay, *nextrelay;
    mount_proxy *mount, *nextmount;

	if (_config_filename) free(_config_filename);

	if (c->location && c->location != CONFIG_DEFAULT_LOCATION) 
        xmlFree(c->location);
	if (c->admin && c->admin != CONFIG_DEFAULT_ADMIN) 
        xmlFree(c->admin);
	if (c->source_password && c->source_password != CONFIG_DEFAULT_SOURCE_PASSWORD)
        xmlFree(c->source_password);
	if (c->relay_password && c->relay_password != CONFIG_DEFAULT_SOURCE_PASSWORD)
        xmlFree(c->relay_password);
	if (c->admin_username)
        xmlFree(c->admin_username);
	if (c->admin_password)
        xmlFree(c->admin_password);
	if (c->hostname && c->hostname != CONFIG_DEFAULT_HOSTNAME) 
        xmlFree(c->hostname);
	if (c->base_dir && c->base_dir != CONFIG_DEFAULT_BASE_DIR) 
        xmlFree(c->base_dir);
	if (c->log_dir && c->log_dir != CONFIG_DEFAULT_LOG_DIR) 
        xmlFree(c->log_dir);
    if (c->webroot_dir && c->webroot_dir != CONFIG_DEFAULT_WEBROOT_DIR)
        xmlFree(c->webroot_dir);
	if (c->access_log && c->access_log != CONFIG_DEFAULT_ACCESS_LOG) 
        xmlFree(c->access_log);
	if (c->error_log && c->error_log != CONFIG_DEFAULT_ERROR_LOG) 
        xmlFree(c->error_log);
    if (c->bind_address) xmlFree(c->bind_address);
    if (c->master_server) xmlFree(c->master_server);
    if (c->master_password) xmlFree(c->master_password);
    if (c->user) xmlFree(c->user);
    if (c->group) xmlFree(c->group);
    relay = _configuration.relay;
    while(relay) {
        nextrelay = relay->next;
        xmlFree(relay->server);
        xmlFree(relay->mount);
        if(relay->localmount)
            xmlFree(relay->localmount);
        free(relay);
        relay = nextrelay;
    }
    mount = _configuration.mounts;
    while(mount) {
        nextmount = mount->next;
        xmlFree(mount->mountname);
        xmlFree(mount->username);
        xmlFree(mount->password);
        xmlFree(mount->dumpfile);
        xmlFree(mount->fallback_mount);
        free(mount);
        mount = nextmount;
    }
    dirnode = _configuration.dir_list;
    while(dirnode) {
        nextdirnode = dirnode->next;
        xmlFree(dirnode->host);
        free(dirnode);
        dirnode = nextdirnode;
    }

    memset(c, 0, sizeof(ice_config_t));
}

int config_parse_file(const char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr node;

	if (filename == NULL || strcmp(filename, "") == 0) return CONFIG_EINSANE;
	
	_config_filename = (char *)strdup(filename);

    xmlInitParser();
	doc = xmlParseFile(_config_filename);
	if (doc == NULL) {
		return CONFIG_EPARSE;
	}

	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
		xmlFreeDoc(doc);
        xmlCleanupParser();
		return CONFIG_ENOROOT;
	}

	if (strcmp(node->name, "icecast") != 0) {
		xmlFreeDoc(doc);
        xmlCleanupParser();
		return CONFIG_EBADROOT;
	}

	_parse_root(doc, node->xmlChildrenNode);

	xmlFreeDoc(doc);
    xmlCleanupParser();

	return 0;
}

int config_parse_cmdline(int arg, char **argv)
{
	return 0;
}

int config_rehash(void)
{
	return 0;
}

ice_config_t *config_get_config(void)
{
	return &_configuration;
}

static void _set_defaults(void)
{
	_configuration.location = CONFIG_DEFAULT_LOCATION;
	_configuration.admin = CONFIG_DEFAULT_ADMIN;
	_configuration.client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
	_configuration.source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
	_configuration.queue_size_limit = CONFIG_DEFAULT_QUEUE_SIZE_LIMIT;
	_configuration.threadpool_size = CONFIG_DEFAULT_THREADPOOL_SIZE;
	_configuration.client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
	_configuration.header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
	_configuration.source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
	_configuration.source_password = CONFIG_DEFAULT_SOURCE_PASSWORD;
	_configuration.relay_password = CONFIG_DEFAULT_RELAY_PASSWORD;
	_configuration.ice_login = CONFIG_DEFAULT_ICE_LOGIN;
	_configuration.fileserve = CONFIG_DEFAULT_FILESERVE;
	_configuration.touch_interval = CONFIG_DEFAULT_TOUCH_FREQ;
	_configuration.dir_list = NULL;
	_configuration.hostname = CONFIG_DEFAULT_HOSTNAME;
	_configuration.port = CONFIG_DEFAULT_PORT;
	_configuration.bind_address = NULL;
	_configuration.master_server = NULL;
	_configuration.master_server_port = CONFIG_DEFAULT_PORT;
    _configuration.master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
	_configuration.master_password = NULL;
	_configuration.base_dir = CONFIG_DEFAULT_BASE_DIR;
	_configuration.log_dir = CONFIG_DEFAULT_LOG_DIR;
    _configuration.webroot_dir = CONFIG_DEFAULT_WEBROOT_DIR;
	_configuration.access_log = CONFIG_DEFAULT_ACCESS_LOG;
	_configuration.error_log = CONFIG_DEFAULT_ERROR_LOG;
	_configuration.loglevel = CONFIG_DEFAULT_LOG_LEVEL;
    _configuration.chroot = CONFIG_DEFAULT_CHROOT;
    _configuration.chuid = CONFIG_DEFAULT_CHUID;
    _configuration.user = CONFIG_DEFAULT_USER;
    _configuration.group = CONFIG_DEFAULT_GROUP;
    _configuration.num_yp_directories = 0;
}

static void _parse_root(xmlDocPtr doc, xmlNodePtr node)
{
	char *tmp;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "location") == 0) {
			if (_configuration.location && _configuration.location != CONFIG_DEFAULT_LOCATION) xmlFree(_configuration.location);
			_configuration.location = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "admin") == 0) {
			if (_configuration.admin && _configuration.admin != CONFIG_DEFAULT_ADMIN) xmlFree(_configuration.admin);
			_configuration.admin = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if(strcmp(node->name, "authentication") == 0) {
			_parse_authentication(doc, node->xmlChildrenNode);
        } else if (strcmp(node->name, "source-password") == 0) {
            /* TODO: This is the backwards-compatibility location */
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, "mount")) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
			    if (_configuration.source_password && _configuration.source_password != CONFIG_DEFAULT_SOURCE_PASSWORD) xmlFree(_configuration.source_password);
			    _configuration.source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (strcmp(node->name, "relay-password") == 0) {
            /* TODO: This is the backwards-compatibility location */
			if (_configuration.relay_password && _configuration.relay_password != CONFIG_DEFAULT_RELAY_PASSWORD) xmlFree(_configuration.relay_password);
			_configuration.relay_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "icelogin") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.ice_login = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "fileserve") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.fileserve = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "hostname") == 0) {
			if (_configuration.hostname && _configuration.hostname != CONFIG_DEFAULT_HOSTNAME) xmlFree(_configuration.hostname);
			_configuration.hostname = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "port") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.port = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "bind-address") == 0) {
			if (_configuration.bind_address) xmlFree(_configuration.bind_address);
			_configuration.bind_address = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "master-server") == 0) {
			if (_configuration.master_server) xmlFree(_configuration.master_server);
			_configuration.master_server = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "master-password") == 0) {
			if (_configuration.master_password) xmlFree(_configuration.master_password);
			_configuration.master_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "master-server-port") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.master_server_port = atoi(tmp);
        } else if (strcmp(node->name, "master-update-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            _configuration.master_update_interval = atoi(tmp);
		} else if (strcmp(node->name, "limits") == 0) {
			_parse_limits(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "relay") == 0) {
			_parse_relay(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "mount") == 0) {
			_parse_mount(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "directory") == 0) {
			_parse_directory(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "paths") == 0) {
			_parse_paths(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "logging") == 0) {
			_parse_logging(doc, node->xmlChildrenNode);
        } else if (strcmp(node->name, "security") == 0) {
            _parse_security(doc, node->xmlChildrenNode);
		}
	} while ((node = node->next));
}

static void _parse_limits(xmlDocPtr doc, xmlNodePtr node)
{
	char *tmp;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "clients") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.client_limit = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "sources") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.source_limit = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "queue-size") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.queue_size_limit = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "threadpool") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.threadpool_size = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "client-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.client_timeout = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "header-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.header_timeout = atoi(tmp);
			if (tmp) xmlFree(tmp);
		} else if (strcmp(node->name, "source-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.source_timeout = atoi(tmp);
			if (tmp) xmlFree(tmp);
		}
	} while ((node = node->next));
}

static void _parse_mount(xmlDocPtr doc, xmlNodePtr node)
{
    char *tmp;
    mount_proxy *mount = calloc(1, sizeof(mount_proxy));
    mount_proxy *current = _configuration.mounts;
    mount_proxy *last=NULL;
    
    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = mount;
    else
        _configuration.mounts = mount;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "mount-name") == 0) {
            mount->mountname = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "username") == 0) {
            mount->username = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "password") == 0) {
            mount->password = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "dump-file") == 0) {
            mount->dumpfile = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "fallback-mount") == 0) {
            mount->fallback_mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "max-listeners") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->max_listeners = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
	} while ((node = node->next));
}

static void _parse_relay(xmlDocPtr doc, xmlNodePtr node)
{
    char *tmp;
    relay_server *relay = calloc(1, sizeof(relay_server));
    relay_server *current = _configuration.relay;
    relay_server *last=NULL;
    
    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = relay;
    else
        _configuration.relay = relay;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "server") == 0) {
			relay->server = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "port") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->port = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "mount") == 0) {
			relay->mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "local-mount") == 0) {
			relay->localmount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "relay-shoutcast-metadata") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->mp3metadata = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
	} while ((node = node->next));
}

static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node)
{
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "source-password") == 0) {
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, "mount")) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
			    if (_configuration.source_password && 
                        _configuration.source_password != 
                        CONFIG_DEFAULT_SOURCE_PASSWORD) 
                    xmlFree(_configuration.source_password);
			    _configuration.source_password = 
                    (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (strcmp(node->name, "relay-password") == 0) {
			if (_configuration.relay_password && 
                    _configuration.relay_password != 
                    CONFIG_DEFAULT_RELAY_PASSWORD) 
                xmlFree(_configuration.relay_password);
			_configuration.relay_password = 
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "admin-password") == 0) {
            if(_configuration.admin_password)
                xmlFree(_configuration.admin_password);
            _configuration.admin_password =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "admin-user") == 0) {
            if(_configuration.admin_username)
                xmlFree(_configuration.admin_username);
            _configuration.admin_username =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        }
	} while ((node = node->next));
}

static void _parse_directory(xmlDocPtr doc, xmlNodePtr node)
{
	char *tmp;

	if (_configuration.num_yp_directories >= MAX_YP_DIRECTORIES) {
		ERROR0("Maximum number of yp directories exceeded!");
		return;
	}
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "yp-url") == 0) {
			if (_configuration.yp_url[_configuration.num_yp_directories]) 
                xmlFree(_configuration.yp_url[_configuration.num_yp_directories]);
			_configuration.yp_url[_configuration.num_yp_directories] = 
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "yp-url-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            _configuration.yp_url_timeout[_configuration.num_yp_directories] = 
                atoi(tmp);
		} else if (strcmp(node->name, "server") == 0) {
			_add_server(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "touch-interval") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.touch_interval = atoi(tmp);
			if (tmp) xmlFree(tmp);
		}
	} while ((node = node->next));
	_configuration.num_yp_directories++;
}

static void _parse_paths(xmlDocPtr doc, xmlNodePtr node)
{
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "basedir") == 0) {
			if (_configuration.base_dir && _configuration.base_dir != CONFIG_DEFAULT_BASE_DIR) xmlFree(_configuration.base_dir);
			_configuration.base_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "logdir") == 0) {
			if (_configuration.log_dir && _configuration.log_dir != CONFIG_DEFAULT_LOG_DIR) xmlFree(_configuration.log_dir);
			_configuration.log_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "webroot") == 0) {
			if (_configuration.webroot_dir && _configuration.webroot_dir != CONFIG_DEFAULT_WEBROOT_DIR) xmlFree(_configuration.webroot_dir);
			_configuration.webroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(_configuration.webroot_dir[strlen(_configuration.webroot_dir)-1] == '/')
                _configuration.webroot_dir[strlen(_configuration.webroot_dir)-1] = 0;

		}
	} while ((node = node->next));
}

static void _parse_logging(xmlDocPtr doc, xmlNodePtr node)
{
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "accesslog") == 0) {
			if (_configuration.access_log && _configuration.access_log != CONFIG_DEFAULT_ACCESS_LOG) xmlFree(_configuration.access_log);
			_configuration.access_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "errorlog") == 0) {
			if (_configuration.error_log && _configuration.error_log != CONFIG_DEFAULT_ERROR_LOG) xmlFree(_configuration.error_log);
			_configuration.error_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "loglevel") == 0) {
           char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           _configuration.loglevel = atoi(tmp);
           if (tmp) xmlFree(tmp);
        }
	} while ((node = node->next));
}

static void _parse_security(xmlDocPtr doc, xmlNodePtr node)
{
   char *tmp;
   xmlNodePtr oldnode;

   do {
       if (node == NULL) break;
       if (xmlIsBlankNode(node)) continue;

       if (strcmp(node->name, "chroot") == 0) {
           tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           _configuration.chroot = atoi(tmp);
           if (tmp) xmlFree(tmp);
       } else if (strcmp(node->name, "changeowner") == 0) {
           _configuration.chuid = 1;
           oldnode = node;
           node = node->xmlChildrenNode;
           do {
               if(node == NULL) break;
               if(xmlIsBlankNode(node)) continue;
               if(strcmp(node->name, "user") == 0) {
                   if(_configuration.user) xmlFree(_configuration.user);
                   _configuration.user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               } else if(strcmp(node->name, "group") == 0) {
                   if(_configuration.group) xmlFree(_configuration.group);
                   _configuration.group = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               }
           } while((node = node->next));
           node = oldnode;
       }
   } while ((node = node->next));
}

static void _add_server(xmlDocPtr doc, xmlNodePtr node)
{
	ice_config_dir_t *dirnode, *server;
	int addnode;
	char *tmp;

	server = (ice_config_dir_t *)malloc(sizeof(ice_config_dir_t));
	server->touch_interval = _configuration.touch_interval;
	server->host = NULL;
	addnode = 0;
	
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "host") == 0) {
			server->host = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
			addnode = 1;
		} else if (strcmp(node->name, "touch-interval") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			server->touch_interval = atoi(tmp);
			if (tmp) xmlFree(tmp);
		}
		server->next = NULL;
	} while ((node = node->next));

	if (addnode) {
		dirnode = _configuration.dir_list;
		if (dirnode == NULL) {
			_configuration.dir_list = server;
		} else {
			while (dirnode->next) dirnode = dirnode->next;
			
			dirnode->next = server;
		}
		
		server = NULL;
		addnode = 0;
	}
	
}


