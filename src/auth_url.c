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
 * Copyright 2011-2014, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/*
 * Client authentication via URL functions
 *
 * authenticate user via a URL, this is done via libcurl so https can also
 * be handled. The request will have POST information about the request in
 * the form of
 *
 * action=listener_add&client=1&server=host&port=8000&mount=/live&user=fred&pass=mypass&ip=127.0.0.1&agent=""
 *
 * For a user to be accecpted the following HTTP header needs
 * to be returned (the actual string can be specified in the xml file)
 *
 * icecast-auth-user: 1
 *
 * A listening client may also be configured as only to stay connected for a
 * certain length of time. eg The auth server may only allow a 15 minute
 * playback by sending back.
 *
 * icecast-auth-timelimit: 900
 *
 * On client disconnection another request can be sent to a URL with the POST
 * information of
 *
 * action=listener_remove&server=host&port=8000&client=1&mount=/live&user=fred&pass=mypass&duration=3600
 *
 * client refers to the icecast client identification number. mount refers
 * to the mountpoint (beginning with / and may contain query parameters eg ?&
 * encoded) and duration is the amount of time in seconds. user and pass
 * setting can be blank
 *
 * On source client connection, a request can be made to trigger a URL request
 * to verify the details externally. Post info is
 *
 * action=stream_auth&mount=/stream&ip=IP&server=SERVER&port=8000&user=fred&pass=pass
 *
 * As admin requests can come in for a stream (eg metadata update) these requests
 * can be issued while stream is active. For these &admin=1 is added to the POST
 * details.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#ifndef _WIN32
#   include <sys/wait.h>
#   include <strings.h>
#else
#   define snprintf _snprintf
#   define strncasecmp strnicmp
#endif

#include "curl.h"
#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "common/httpp/httpp.h"
#include "stats.h"

#include "logging.h"
#define CATMODULE "auth_url"

typedef struct {
    char       *pass_headers; // headers passed from client to addurl.
    char       *prefix_headers; // prefix for passed headers.
    char       *addurl;
    char       *removeurl;
    char       *addaction;
    char       *removeaction;
    char       *username;
    char       *password;
    char       *auth_header;
    int         auth_header_len;
    char       *timelimit_header;
    int         timelimit_header_len;
    char       *userpwd;
    CURL       *handle;
    char        errormsg[CURL_ERROR_SIZE];
    auth_result result;
} auth_url;


static void auth_url_clear(auth_t *self)
{
    auth_url *url;

    ICECAST_LOG_INFO("Doing auth URL cleanup");
    url = self->state;
    self->state = NULL;
    icecast_curl_free(url->handle);
    free(url->username);
    free(url->password);
    free(url->pass_headers);
    free(url->prefix_headers);
    free(url->removeurl);
    free(url->addurl);
    free(url->addaction);
    free(url->removeaction);
    free(url->auth_header);
    free(url->timelimit_header);
    free(url->userpwd);
    free(url);
}

static size_t handle_returned_header(void      *ptr,
                                     size_t    size,
                                     size_t    nmemb,
                                     void      *stream)
{
    auth_client *auth_user = stream;
    unsigned bytes = size * nmemb;
    client_t *client = auth_user->client;

    if (client) {
        auth_t *auth = client->auth;
        auth_url *url = auth->state;
        if (strncasecmp(ptr, url->auth_header, url->auth_header_len) == 0)
            url->result = AUTH_OK;
        if (strncasecmp(ptr, url->timelimit_header,
                url->timelimit_header_len) == 0) {
            unsigned int limit = 0;
            sscanf ((char *)ptr+url->timelimit_header_len, "%u\r\n", &limit);
            client->con->discon_time = time(NULL) + limit;
        }
        if (strncasecmp (ptr, "icecast-auth-message: ", 22) == 0) {
            char *eol;
            snprintf(url->errormsg, sizeof(url->errormsg), "%s", (char*)ptr+22);
            eol = strchr(url->errormsg, '\r');
            if (eol == NULL)
                eol = strchr(url->errormsg, '\n');
            if (eol)
                *eol = '\0';
        }
    }

    return (int)bytes;
}

static auth_result url_remove_client(auth_client *auth_user)
{
    client_t       *client      = auth_user->client;
    auth_t         *auth        = client->auth;
    auth_url       *url         = auth->state;
    time_t          duration    = time(NULL) - client->con->con_time;
    char           *username,
                   *password,
                   *mount,
                   *server;
    const char     *mountreq;
    ice_config_t   *config;
    int             port;
    char           *userpwd     = NULL,
                    post[4096];
    const char     *agent;
    char           *user_agent,
                   *ipaddr;

    if (url->removeurl == NULL)
        return AUTH_OK;

    config = config_get_config();
    server = util_url_escape(config->hostname);
    port = config->port;
    config_release_config();

    agent = httpp_getvar(client->parser, "user-agent");
    if (agent) {
        user_agent = util_url_escape(agent);
    } else {
        user_agent = strdup("-");
    }

    if (client->username) {
        username = util_url_escape(client->username);
    } else {
        username = strdup("");
    }

    if (client->password) {
        password = util_url_escape(client->password);
    } else {
        password = strdup("");
    }

    /* get the full uri (with query params if available) */
    mountreq = httpp_getvar(client->parser, HTTPP_VAR_RAWURI);
    if (mountreq == NULL)
        mountreq = httpp_getvar(client->parser, HTTPP_VAR_URI);
    mount = util_url_escape(mountreq);
    ipaddr = util_url_escape(client->con->ip);

    snprintf(post, sizeof (post),
            "action=%s&server=%s&port=%d&client=%lu&mount=%s"
            "&user=%s&pass=%s&duration=%lu&ip=%s&agent=%s",
            url->removeaction, /* already escaped */
            server, port, client->con->id, mount, username,
            password, (long unsigned)duration, ipaddr, user_agent);

    free(server);
    free(mount);
    free(username);
    free(password);
    free(ipaddr);
    free(user_agent);

    if (strchr (url->removeurl, '@') == NULL) {
        if (url->userpwd) {
            curl_easy_setopt(url->handle, CURLOPT_USERPWD, url->userpwd);
        } else {
            /* auth'd requests may not have a user/pass, but may use query args */
            if (client->username && client->password) {
                size_t len = strlen(client->username) +
                    strlen(client->password) + 2;
                userpwd = malloc(len);
                snprintf(userpwd, len, "%s:%s",
                    client->username, client->password);
                curl_easy_setopt(url->handle, CURLOPT_USERPWD, userpwd);
            } else {
                curl_easy_setopt(url->handle, CURLOPT_USERPWD, "");
            }
        }
    } else {
        /* url has user/pass but libcurl may need to clear any existing settings */
        curl_easy_setopt(url->handle, CURLOPT_USERPWD, "");
    }
    curl_easy_setopt(url->handle, CURLOPT_URL, url->removeurl);
    curl_easy_setopt(url->handle, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt(url->handle, CURLOPT_WRITEHEADER, auth_user);

    if (curl_easy_perform (url->handle))
        ICECAST_LOG_WARN("auth to server %s failed with %s",
            url->removeurl, url->errormsg);

    free(userpwd);

    return AUTH_OK;
}


static auth_result url_add_client(auth_client *auth_user)
{
    client_t       *client      = auth_user->client;
    auth_t         *auth        = client->auth;
    auth_url       *url         = auth->state;
    int             res         = 0,
                    port;
    const char     *agent;
    char           *user_agent,
                   *username,
                   *password;
    const char     *mountreq;
    char           *mount,
                   *ipaddr,
                   *server;
    ice_config_t   *config;
    char           *userpwd    = NULL, post [4096];
    ssize_t         post_offset;
    char           *pass_headers,
                   *cur_header,
                   *next_header;
    const char     *header_val;
    char           *header_valesc;
    char           *current_listeners;

    if (url->addurl == NULL)
        return AUTH_OK;

    config = config_get_config();
    server = util_url_escape(config->hostname);
    port = config->port;
    config_release_config();

    agent = httpp_getvar(client->parser, "user-agent");
    if (agent) {
        user_agent = util_url_escape(agent);
    } else {
        user_agent = strdup("-");
    }

    if (client->username) {
        username = util_url_escape(client->username);
    } else {
        username = strdup("");
    }

    if (client->password) {
        password = util_url_escape(client->password);
    } else {
        password = strdup("");
    }

    /* get the full uri (with query params if available) */
    mountreq = httpp_getvar(client->parser, HTTPP_VAR_RAWURI);
    if (mountreq == NULL)
        mountreq = httpp_getvar(client->parser, HTTPP_VAR_URI);
    mount = util_url_escape(mountreq);
    ipaddr = util_url_escape(client->con->ip);

    current_listeners = stats_get_value(auth->mount, "listeners");

    post_offset = snprintf(post, sizeof (post),
                           "action=%s&server=%s&port=%d&client=%lu&mount=%s"
                           "&user=%s&pass=%s&ip=%s&agent=%s&current_listeners=%s",
                           url->addaction, /* already escaped */
                           server, port, client->con->id, mount, username,
                           password, ipaddr, user_agent, current_listeners);

    free(server);
    free(mount);
    free(user_agent);
    free(username);
    free(password);
    free(ipaddr);

    pass_headers = NULL;
    if (url->pass_headers)
        pass_headers = strdup(url->pass_headers);
    if (pass_headers) {
        cur_header = pass_headers;
        while (cur_header) {
            next_header = strstr(cur_header, ",");
            if (next_header) {
                *next_header=0;
                next_header++;
            }

            header_val = httpp_getvar (client->parser, cur_header);
            if (header_val) {
                header_valesc = util_url_escape (header_val);
                post_offset += snprintf(post + post_offset,
                                        sizeof(post) - post_offset,
                                        "&%s%s=%s",
                                        url->prefix_headers ? url->prefix_headers : "",
                                        cur_header, header_valesc);
                free(header_valesc);
            }

            cur_header = next_header;
        }
    }

    if (strchr(url->addurl, '@') == NULL) {
        if (url->userpwd) {
            curl_easy_setopt(url->handle, CURLOPT_USERPWD, url->userpwd);
        } else {
            /* auth'd requests may not have a user/pass, but may use query args */
            if (client->username && client->password) {
                size_t len = strlen(client->username) + strlen(client->password) + 2;
                userpwd = malloc (len);
                snprintf(userpwd, len, "%s:%s",
                    client->username, client->password);
                curl_easy_setopt(url->handle, CURLOPT_USERPWD, userpwd);
            } else {
                curl_easy_setopt (url->handle, CURLOPT_USERPWD, "");
            }
        }
    } else {
        /* url has user/pass but libcurl may need to clear any existing settings */
        curl_easy_setopt(url->handle, CURLOPT_USERPWD, "");
    }
    curl_easy_setopt(url->handle, CURLOPT_URL, url->addurl);
    curl_easy_setopt(url->handle, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt(url->handle, CURLOPT_WRITEHEADER, auth_user);
    url->errormsg[0] = '\0';

    url->result = AUTH_FAILED;
    res = curl_easy_perform(url->handle);

    free(userpwd);

    if (res) {
        ICECAST_LOG_WARN("auth to server %s failed with %s",
            url->addurl, url->errormsg);
        return AUTH_FAILED;
    }
    /* we received a response, lets see what it is */
    if (url->result == AUTH_FAILED) {
        ICECAST_LOG_INFO("client auth (%s) failed with \"%s\"",
            url->addurl, url->errormsg);
    }
    return url->result;
}

static auth_result auth_url_adduser(auth_t      *auth,
                                    const char  *username,
                                    const char  *password)
{
    return AUTH_FAILED;
}

static auth_result auth_url_deleteuser(auth_t *auth, const char *username)
{
    return AUTH_FAILED;
}

static auth_result auth_url_listuser(auth_t *auth, xmlNodePtr srcnode)
{
    return AUTH_FAILED;
}

int auth_get_url_auth(auth_t *authenticator, config_options_t *options)
{
    auth_url    *url_info;
    const char  *addaction      = "listener_add";
    const char  *removeaction   = "listener_remove";

    authenticator->free         = auth_url_clear;
    authenticator->adduser      = auth_url_adduser;
    authenticator->deleteuser   = auth_url_deleteuser;
    authenticator->listuser     = auth_url_listuser;

    url_info                    = calloc(1, sizeof(auth_url));
    authenticator->state        = url_info;

    /* default headers */
    url_info->auth_header       = strdup("icecast-auth-user: 1\r\n");
    url_info->timelimit_header  = strdup("icecast-auth-timelimit:");

    /* force auth thread to call function. this makes sure the auth_t is attached to client */
    authenticator->authenticate_client = url_add_client;

    while(options) {
        if(strcmp(options->name, "username") == 0) {
            free(url_info->username);
            url_info->username = strdup(options->value);
        } else if(strcmp(options->name, "password") == 0) {
            free(url_info->password);
            url_info->password = strdup(options->value);
        } else if(strcmp(options->name, "headers") == 0) {
            free(url_info->pass_headers);
            url_info->pass_headers = strdup(options->value);
        } else if(strcmp(options->name, "header_prefix") == 0) {
            free(url_info->prefix_headers);
            url_info->prefix_headers = strdup(options->value);
        } else if(strcmp(options->name, "client_add") == 0) {
            free(url_info->addurl);
            url_info->addurl = strdup(options->value);
        } else if(strcmp(options->name, "client_remove") == 0) {
            authenticator->release_client = url_remove_client;
            free(url_info->removeurl);
            url_info->removeurl = strdup(options->value);
        } else if(strcmp(options->name, "action_add") == 0) {
            addaction = options->value;
        } else if(strcmp(options->name, "action_remove") == 0) {
            removeaction = options->value;
        } else if(strcmp(options->name, "auth_header") == 0) {
            free(url_info->auth_header);
            url_info->auth_header = strdup(options->value);
        } else if (strcmp(options->name, "timelimit_header") == 0) {
            free(url_info->timelimit_header);
            url_info->timelimit_header = strdup(options->value);
        } else {
            ICECAST_LOG_ERROR("Unknown option: %s", options->name);
        }
        options = options->next;
    }

    url_info->addaction = util_url_escape(addaction);
    url_info->removeaction = util_url_escape(removeaction);

    url_info->handle = icecast_curl_new(NULL, &url_info->errormsg[0]);
    if (url_info->handle == NULL) {
        auth_url_clear(authenticator);
        return -1;
    }

    if (url_info->auth_header)
        url_info->auth_header_len = strlen (url_info->auth_header);
    if (url_info->timelimit_header)
        url_info->timelimit_header_len = strlen (url_info->timelimit_header);

    curl_easy_setopt(url_info->handle, CURLOPT_HEADERFUNCTION, handle_returned_header);

    if (url_info->username && url_info->password) {
        int len = strlen(url_info->username) + strlen(url_info->password) + 2;
        url_info->userpwd = malloc(len);
        snprintf(url_info->userpwd, len, "%s:%s",
            url_info->username, url_info->password);
    }

    ICECAST_LOG_INFO("URL based authentication setup");
    return 0;
}
