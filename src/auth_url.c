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
 * action=auth&id=1&mount=/live&user=fred&pass=mypass&ip=127.0.0.1&agent=""
 *
 * For a user to be accecpted the following HTTP header needs
 * to be returned
 *
 * icecast-auth-user: 1
 *
 * On client disconnection another request is sent to that same URL with the
 * POST information of
 *
 * action=remove&id=1&mount=/live&user=fred&pass=mypass&duration=3600
 *
 * id refers to the icecast client identification, mount refers to the
 * mountpoint (beginning with /) and duration is the amount of time in
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
} auth_url;

typedef struct {
    char *mount;
    client_t *client;
    CURL *auth_server;
    char *addurl;
    char *removeurl;
    int authenticated;
    char *username;
    char *password;
} auth_client;


static void auth_url_clear(auth_t *self)
{
    auth_url *url = self->state;
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

    if (strncasecmp (ptr, "icecast-auth-user: 1", 20) == 0)
        auth_user->authenticated = 1;

    return (int)bytes;
}

/* capture returned data, but don't do anything with it */
static int handle_returned_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
    return (int)(size*nmemb);
}


static void *auth_removeurl_thread (void *arg)
{
    auth_client *auth_user = arg;
    client_t *client = auth_user->client;
    CURL *handle;
    char post[1024];

    if (auth_user->removeurl)
    {
        time_t duration = time(NULL) - client->con->con_time;
        char *username, *password;

        DEBUG0("starting auth thread");
        username = util_url_escape (client->username);
        password = util_url_escape (client->password);
        snprintf (post, sizeof (post), "action=remove&id=%ld&mount=%s&user=%s&pass=%s&duration=%lu",
                client->con->id, auth_user->mount, username, password, (long unsigned)duration);
        free (username);
        free (password);
        handle = curl_easy_init ();
        if (handle)
        {
            int res = 0;
            char errormsg [CURL_ERROR_SIZE];
            char *userpass = NULL;

            if (auth_user->username && auth_user->password)
            {
                unsigned len = strlen (auth_user->username) + strlen (auth_user->password) + 2;
                userpass = malloc (len);
                snprintf (userpass, len, "%s:%s", auth_user->username, auth_user->password);
                curl_easy_setopt (handle, CURLOPT_USERPWD, userpass);
            }
            curl_easy_setopt (handle, CURLOPT_URL, auth_user->removeurl);
            curl_easy_setopt (handle, CURLOPT_POSTFIELDS, post);
            curl_easy_setopt (handle, CURLOPT_HEADERFUNCTION, handle_returned_header);
            curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, handle_returned_data);
            curl_easy_setopt (handle, CURLOPT_WRITEHEADER, auth_user);
            curl_easy_setopt (handle, CURLOPT_WRITEDATA, handle);
            curl_easy_setopt (handle, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt (handle, CURLOPT_TIMEOUT, 15L);
            curl_easy_setopt (handle, CURLOPT_ERRORBUFFER, errormsg);
            res = curl_easy_perform (handle);
            curl_easy_cleanup (handle);
            free (userpass);

            if (res)
                WARN2 ("auth to server %s failed with %s", auth_user->removeurl, errormsg);
        }
    }
    auth_close_client (client);

    free (auth_user->username);
    free (auth_user->password);
    free (auth_user->mount);
    free (auth_user->removeurl);
    free (auth_user);
    return NULL;
}


static void *auth_url_thread (void *arg)
{
    auth_client *auth_user = arg;
    client_t *client = auth_user->client;
    int res = 0;
    char *agent, *user_agent, *username, *password;
    CURL *handle;
    char post[1024];

    DEBUG0("starting auth thread");
    agent = httpp_getvar (client->parser, "user-agent");
    if (agent == NULL)
        agent = "-";
    user_agent = util_url_escape (agent);
    username  = util_url_escape (client->username);
    password  = util_url_escape (client->password);
    snprintf (post, 1024,"action=auth&id=%ld&mount=%s&user=%s&pass=%s&ip=%s&agent=%s",
            client->con->id, auth_user->mount, username, password,
            client->con->ip, user_agent);
    free (user_agent);
    free (username);
    free (password);

    handle = curl_easy_init ();
    if (handle)
    {
        char errormsg [CURL_ERROR_SIZE];
        char *userpass = NULL;

        if (auth_user->username && auth_user->password)
        {
            unsigned len = strlen (auth_user->username) + strlen (auth_user->password) + 2;
            userpass = malloc (len);
            snprintf (userpass, len, "%s:%s", auth_user->username, auth_user->password);
            curl_easy_setopt (handle, CURLOPT_USERPWD, userpass);
        }

        curl_easy_setopt (handle, CURLOPT_URL, auth_user->addurl);
        curl_easy_setopt (handle, CURLOPT_POSTFIELDS, post);
        curl_easy_setopt (handle, CURLOPT_HEADERFUNCTION, handle_returned_header);
        curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, handle_returned_data);
        curl_easy_setopt (handle, CURLOPT_WRITEHEADER, auth_user);
        curl_easy_setopt (handle, CURLOPT_WRITEDATA, handle);
        curl_easy_setopt (handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt (handle, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt (handle, CURLOPT_ERRORBUFFER, errormsg);
        res = curl_easy_perform (handle);
        curl_easy_cleanup (handle);
        free (userpass);
        // auth_user->authenticated = 1;

        if (res)
        {
            WARN2 ("auth to server %s failed with %s", auth_user->addurl, errormsg);
            auth_failed_client (auth_user->mount);
            auth_close_client (client);
        }
        else
        {
            /* we received a response, lets see what it is */
            if (auth_user->authenticated)
            {
                if (auth_postprocess_client (auth_user->mount, client) < 0)
                {
                    /* do cleanup, and exit as the remove does cleanup as well */
                    free (auth_user->addurl);
                    auth_user->addurl = NULL;
                    auth_failed_client (auth_user->mount);
                    auth_removeurl_thread (auth_user);
                    return NULL;
                }
            }
            else
            {
                DEBUG0 ("client authentication failed");
                auth_failed_client (auth_user->mount);
                auth_close_client (client);
            }
        }
    }
    else
    {
        auth_failed_client (auth_user->mount);
        auth_close_client (client);
    }

    free (auth_user->username);
    free (auth_user->password);
    free (auth_user->mount);
    free (auth_user->addurl);
    free (auth_user->removeurl);
    free (auth_user);

    return NULL;
}


static void auth_remove_url_client (source_t *source, client_t *client)
{
    auth_client *auth_user = calloc (1, sizeof (auth_client));
    auth_url *urlinfo = source->authenticator->state;

    if (auth_user == NULL)
        return;

    if (urlinfo->username)
        auth_user->username = strdup (urlinfo->username);
    if (urlinfo->password)
        auth_user->password = strdup (urlinfo->password);
    if (urlinfo->removeurl)
        auth_user->removeurl = strdup (urlinfo->removeurl);
    auth_user->mount = strdup (source->mount);
    auth_user->client = client;
    thread_create("AuthRemove by URL thread", auth_removeurl_thread,
            auth_user, THREAD_DETACHED);
}


static auth_result auth_url_client (source_t *source, client_t *client)
{
    auth_client *auth_user = calloc (1, sizeof (auth_client));
    auth_url *urlinfo = source->authenticator->state;

    if (auth_user == NULL)
        return AUTH_FAILED;

    if (urlinfo->username)
        auth_user->username = strdup (urlinfo->username);
    if (urlinfo->password)
        auth_user->password = strdup (urlinfo->password);
    auth_user->addurl = strdup (urlinfo->addurl);
    auth_user->removeurl = strdup (urlinfo->removeurl);
    auth_user->mount = strdup (source->mount);
    auth_user->client = client;
    thread_create("Auth by URL thread", auth_url_thread, auth_user, THREAD_DETACHED);
    return AUTH_OK;
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

auth_t *auth_get_url_auth (config_options_t *options)
{
    auth_t *authenticator = calloc(1, sizeof(auth_t));
    auth_url *state;

    authenticator->authenticate = auth_url_client;
    authenticator->free = auth_url_clear;
    authenticator->adduser = auth_url_adduser;
    authenticator->deleteuser = auth_url_deleteuser;
    authenticator->listuser = auth_url_listuser;
    authenticator->release_client = auth_remove_url_client;

    state = calloc(1, sizeof(auth_url));

    while(options) {
        if(!strcmp(options->name, "username"))
            state->username = strdup(options->value);
        if(!strcmp(options->name, "password"))
            state->password = strdup(options->value);
        if(!strcmp(options->name, "add"))
            state->addurl = strdup(options->value);
        if(!strcmp(options->name, "remove"))
            state->removeurl = strdup(options->value);
        options = options->next;
    }
    authenticator->state = state;
    INFO0("URL based authentication setup");
    return authenticator;
}

