#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include "config.h"

#define CONFIG_DEFAULT_LOCATION "Earth"
#define CONFIG_DEFAULT_ADMIN "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT 256
#define CONFIG_DEFAULT_SOURCE_LIMIT 16
#define CONFIG_DEFAULT_THREADPOOL_SIZE 4
#define CONFIG_DEFAULT_CLIENT_TIMEOUT 30
#define CONFIG_DEFAULT_HEADER_TIMEOUT 15
#define CONFIG_DEFAULT_SOURCE_TIMEOUT 10
#define CONFIG_DEFAULT_SOURCE_PASSWORD "changeme"
#define CONFIG_DEFAULT_TOUCH_FREQ 5
#define CONFIG_DEFAULT_HOSTNAME "localhost"
#define CONFIG_DEFAULT_PORT 8888
#define CONFIG_DEFAULT_ACCESS_LOG "access.log"
#define CONFIG_DEFAULT_ERROR_LOG "error.log"
#define CONFIG_DEFAULT_CHROOT 0
#define CONFIG_DEFAULT_CHUID 0
#define CONFIG_DEFAULT_USER NULL
#define CONFIG_DEFAULT_GROUP NULL
#define CONFIG_MASTER_UPDATE_INTERVAL 120

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

	if (_config_filename) free(_config_filename);
	if (_configuration.location) free(_configuration.location);
	if (_configuration.admin) free(_configuration.admin);
	if (_configuration.source_password) free(_configuration.source_password);
	if (_configuration.hostname) free(_configuration.hostname);
	if (_configuration.base_dir) free(_configuration.base_dir);
	if (_configuration.log_dir) free(_configuration.log_dir);
	if (_configuration.access_log) free(_configuration.access_log);
	if (_configuration.error_log) free(_configuration.error_log);
    if (_configuration.bind_address) free(_configuration.bind_address);
    if (_configuration.user) free(_configuration.user);
    if (_configuration.group) free(_configuration.group);
    dirnode = _configuration.dir_list;
    while(dirnode) {
        nextdirnode = dirnode->next;
        free(dirnode->host);
        free(dirnode);
        dirnode = nextdirnode;
    }

    memset(&_configuration, 0, sizeof(ice_config_t));
}

int config_parse_file(const char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr node;

	if (filename == NULL || strcmp(filename, "") == 0) return CONFIG_EINSANE;
	
	_config_filename = (char *)strdup(filename);

	doc = xmlParseFile(_config_filename);
	if (doc == NULL) {
		return CONFIG_EPARSE;
	}

	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
		xmlFreeDoc(doc);
		return CONFIG_ENOROOT;
	}

	if (strcmp(node->name, "icecast") != 0) {
		xmlFreeDoc(doc);
		return CONFIG_EBADROOT;
	}

	_parse_root(doc, node->xmlChildrenNode);

	xmlFreeDoc(doc);

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
	_configuration.location = (char *)strdup(CONFIG_DEFAULT_LOCATION);
	_configuration.admin = (char *)strdup(CONFIG_DEFAULT_ADMIN);
	_configuration.client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
	_configuration.source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
	_configuration.threadpool_size = CONFIG_DEFAULT_THREADPOOL_SIZE;
	_configuration.client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
	_configuration.header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
	_configuration.source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
	_configuration.source_password = (char *)strdup(CONFIG_DEFAULT_SOURCE_PASSWORD);
	_configuration.touch_freq = CONFIG_DEFAULT_TOUCH_FREQ;
	_configuration.dir_list = NULL;
	_configuration.hostname = (char *)strdup(CONFIG_DEFAULT_HOSTNAME);
	_configuration.port = CONFIG_DEFAULT_PORT;
	_configuration.bind_address = NULL;
	_configuration.master_server = NULL;
	_configuration.master_server_port = CONFIG_DEFAULT_PORT;
    _configuration.master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
	_configuration.base_dir = (char *)strdup(CONFIG_DEFAULT_BASE_DIR);
	_configuration.log_dir = (char *)strdup(CONFIG_DEFAULT_LOG_DIR);
	_configuration.access_log = (char *)strdup(CONFIG_DEFAULT_ACCESS_LOG);
	_configuration.error_log = (char *)strdup(CONFIG_DEFAULT_ERROR_LOG);
    _configuration.chroot = CONFIG_DEFAULT_CHROOT;
    _configuration.chuid = CONFIG_DEFAULT_CHUID;
    _configuration.user = CONFIG_DEFAULT_USER;
    _configuration.group = CONFIG_DEFAULT_GROUP;
}

static void _parse_root(xmlDocPtr doc, xmlNodePtr node)
{
	char *tmp;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "location") == 0) {
			if (_configuration.location) free(_configuration.location);
			_configuration.location = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "admin") == 0) {
			if (_configuration.admin) free(_configuration.admin);
			_configuration.admin = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "source-password") == 0) {
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, "mount")) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
			    if (_configuration.source_password) free(_configuration.source_password);
			    _configuration.source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
		} else if (strcmp(node->name, "hostname") == 0) {
			if (_configuration.hostname) free(_configuration.hostname);
			_configuration.hostname = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "port") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.port = atoi(tmp);
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "bind-address") == 0) {
			if (_configuration.bind_address) free(_configuration.bind_address);
			_configuration.bind_address = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "master-server") == 0) {
			if (_configuration.master_server) free(_configuration.master_server);
			_configuration.master_server = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "master-server-port") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.master_server_port = atoi(tmp);
        } else if (strcmp(node->name, "master-update-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            _configuration.master_update_interval = atoi(tmp);
		} else if (strcmp(node->name, "limits") == 0) {
			_parse_limits(doc, node->xmlChildrenNode);
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
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "sources") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.source_limit = atoi(tmp);
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "threadpool") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.threadpool_size = atoi(tmp);
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "client-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.client_timeout = atoi(tmp);
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "header-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.header_timeout = atoi(tmp);
			if (tmp) free(tmp);
		} else if (strcmp(node->name, "source-timeout") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.source_timeout = atoi(tmp);
			if (tmp) free(tmp);
		}
	} while ((node = node->next));
}

static void _parse_directory(xmlDocPtr doc, xmlNodePtr node)
{
	char *tmp;

	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "server") == 0) {
			_add_server(doc, node->xmlChildrenNode);
		} else if (strcmp(node->name, "touch-freq") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			_configuration.touch_freq = atoi(tmp);
			if (tmp) free(tmp);
		}
	} while ((node = node->next));
}

static void _parse_paths(xmlDocPtr doc, xmlNodePtr node)
{
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "basedir") == 0) {
			if (_configuration.base_dir) free(_configuration.base_dir);
			_configuration.base_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "logdir") == 0) {
			if (_configuration.log_dir) free(_configuration.log_dir);
			_configuration.log_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "webroot") == 0) {
			if (_configuration.webroot_dir) free(_configuration.webroot_dir);
			_configuration.webroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		}
	} while ((node = node->next));
}

static void _parse_logging(xmlDocPtr doc, xmlNodePtr node)
{
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "accesslog") == 0) {
			if (_configuration.access_log) free(_configuration.access_log);
			_configuration.access_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
		} else if (strcmp(node->name, "errorlog") == 0) {
			if (_configuration.error_log) free(_configuration.error_log);
			_configuration.error_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
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
           if (tmp) free(tmp);
       } else if (strcmp(node->name, "changeowner") == 0) {
           _configuration.chuid = 1;
           oldnode = node;
           node = node->xmlChildrenNode;
           do {
               if(node == NULL) break;
               if(xmlIsBlankNode(node)) continue;
               if(strcmp(node->name, "user") == 0) {
                   if(_configuration.user) free(_configuration.user);
                   _configuration.user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               } else if(strcmp(node->name, "group") == 0) {
                   if(_configuration.group) free(_configuration.group);
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
	server->touch_freq = _configuration.touch_freq;
	server->host = NULL;
	addnode = 0;
	
	do {
		if (node == NULL) break;
		if (xmlIsBlankNode(node)) continue;

		if (strcmp(node->name, "host") == 0) {
			server->host = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			addnode = 1;
		} else if (strcmp(node->name, "touch-freq") == 0) {
			tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			server->touch_freq = atoi(tmp);
			if (tmp) free(tmp);
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
	
	if (server) {
		if (server->host) free(server->host);
		free(server);
		server = NULL;
	}
}


