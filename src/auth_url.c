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
 */

/* 
 * Client authentication via URL functions
 *
 * authenticate user via a URL, this is done via libcurl so https can also
 * be handled. The request will have POST information about the request in
 * the form of
 *
 * action=auth&client=1&mount=/live&user=fred&pass=mypass&ip=127.0.0.1&agent=""
 *
 * For a user to be accecpted the following HTTP header needs
 * to be returned
 *
 * icecast-auth-user: 1
 *
 * On client disconnection another request is sent to that same URL with the
 * POST information of
 *
 * action=remove&client=1&mount=/live&user=fred&pass=mypass&duration=3600
 *
 * client refers to the icecast client identification number, mount refers
 * to the mountpoint (beginning with /) and duration is the amount of time in
 * seconds
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/wait.h>
#else
#define snprintf _snprintf
#endif

#include <curl/curl.h>

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"

#include "logging.h"
#define CATMODULE "auth_url"

typedef struct {
    char *addurl;
    char *removeurl;
    char *username;
    char *password;
    CURL *handle;
    char errormsg [CURL_ERROR_SIZE];
} auth_url;


static void auth_url_clear(auth_t *self)
{
    auth_url *url = self->state;
    curl_easy_cleanup (url->handle);
    free(url->username);
    free(url->password);
    free(url->removeurl);
    free(url->addurl);
    free(url);
}


static int handle_returned_header (void *ptr, size_t size, size_t nmemb, void *stream)
{
    auth_client *auth_user = stream;
    unsigned bytes = size * nmemb;
    client_t *client = auth_user->client;

    if (strncasecmp (ptr, "icecast-auth-user: 1\r\n", 22) == 0)
        client->authenticated = 1;

    return (int)bytes;
}

/* capture returned data, but don't do anything with it */
static int handle_returned_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
    return (int)(size*nmemb);
}


static auth_result auth_removeurl_client (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_t *auth = client->auth;
    auth_url *url = auth->state;
    time_t duration = time(NULL) - client->con->con_time;
    char *username, *password, *mount;
    char post[1024];

    if (url->removeurl == NULL)
        return AUTH_OK;
    username = util_url_escape (client->username);
    password = util_url_escape (client->password);
    mount = util_url_escape (auth_user->mount);

    snprintf (post, sizeof (post),
            "action=remove&client=%lu&mount=%s&user=%s&pass=%s&duration=%lu",
            client->con->id, mount, username, password, (long unsigned)duration);
    free (mount);
    free (username);
    free (password);

    curl_easy_setopt (url->handle, CURLOPT_URL, url->removeurl);
    curl_easy_setopt (url->handle, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (url->handle, CURLOPT_WRITEHEADER, auth_user);

    if (curl_easy_perform (url->handle))
        WARN2 ("auth to server %s failed with %s", url->removeurl, url->errormsg);

    /* these are needed so the client is not added back onto the auth list */
    auth_release (client->auth);
    client->auth = NULL;

    return AUTH_OK;
}


static auth_result auth_addurl_client (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_t *auth = client->auth;
    auth_url *url = auth->state;
    int res = 0;
    char *agent, *user_agent, *username, *password;
    char *mount, *ipaddr;
    char post[1024];

    agent = httpp_getvar (client->parser, "user-agent");
    if (agent == NULL)
        agent = "-";
    user_agent = util_url_escape (agent);
    username  = util_url_escape (client->username);
    password  = util_url_escape (client->password);
    mount = util_url_escape (auth_user->mount);
    ipaddr = util_url_escape (client->con->ip);

    snprintf (post, sizeof (post),
            "action=auth&client=%lu&mount=%s&user=%s&pass=%s&ip=%s&agent=%s",
            client->con->id, mount, username, password, ipaddr, user_agent);
    free (mount);
    free (user_agent);
    free (username);
    free (password);
    free (ipaddr);

    curl_easy_setopt (url->handle, CURLOPT_URL, url->addurl);
    curl_easy_setopt (url->handle, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (url->handle, CURLOPT_WRITEHEADER, auth_user);

    res = curl_easy_perform (url->handle);

    if (res)
    {
        WARN2 ("auth to server %s failed with %s", url->addurl, url->errormsg);
        return AUTH_FAILED;
    }
    /* we received a response, lets see what it is */
    if (client->authenticated)
    {
        if (auth_postprocess_client (auth_user) < 0)
        {
            /* do cleanup, and exit as the remove does cleanup as well */
            return AUTH_FAILED;
        }
        return AUTH_OK;
    }
    return AUTH_FAILED;
}



static auth_result auth_url_adduser(auth_t *auth, const char *username, const char *password)
{
    return AUTH_FAILED;
}

static auth_result auth_url_deleteuser (auth_t *auth, const char *username)
{
    return AUTH_FAILED;
}

static auth_result auth_url_listuser (auth_t *auth, xmlNodePtr srcnode)
{
    return AUTH_FAILED;
}

int auth_get_url_auth (auth_t *authenticator, config_options_t *options)
{
    auth_url *url_info;

    authenticator->authenticate = auth_addurl_client;
    authenticator->free = auth_url_clear;
    authenticator->adduser = auth_url_adduser;
    authenticator->deleteuser = auth_url_deleteuser;
    authenticator->listuser = auth_url_listuser;
    authenticator->release_client = auth_removeurl_client;

    url_info = calloc(1, sizeof(auth_url));

    while(options) {
        if(!strcmp(options->name, "username"))
            url_info->username = strdup (options->value);
        if(!strcmp(options->name, "password"))
            url_info->password = strdup (options->value);
        if(!strcmp(options->name, "add"))
            url_info->addurl = strdup (options->value);
        if(!strcmp(options->name, "remove"))
            url_info->removeurl = strdup (options->value);
        options = options->next;
    }
    url_info->handle = curl_easy_init ();
    if (url_info->handle == NULL)
    {
        free (url_info);
        free (authenticator);
        return -1;
    }
    curl_easy_setopt (url_info->handle, CURLOPT_HEADERFUNCTION, handle_returned_header);
    curl_easy_setopt (url_info->handle, CURLOPT_WRITEFUNCTION, handle_returned_data);
    curl_easy_setopt (url_info->handle, CURLOPT_WRITEDATA, url_info->handle);
    curl_easy_setopt (url_info->handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt (url_info->handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt (url_info->handle, CURLOPT_ERRORBUFFER, &url_info->errormsg[0]);

    authenticator->state = url_info;
    INFO0("URL based authentication setup");
    return 0;
}

