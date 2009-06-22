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
 * A listening client may be a slave relay and as such you may want it to avoid
 * certain checks like max listeners. Send this header back if to wish icecast
 * to treat the client as a slave relay.
 *
 * icecast-slave: 1
 *
 * On client disconnection another request can be sent to a URL with the POST
 * information of
 *
 * action=listener_remove&server=host&port=8000&client=1&mount=/live&user=fred&pass=mypass&ip=127.0.0.1&duration=3600
 *
 * client refers to the icecast client identification number. mount refers
 * to the mountpoint (beginning with / and may contain query parameters eg ?&
 * encoded) and duration is the amount of time in seconds. user and pass
 * setting can be blank
 *
 * On stream start and end, another url can be issued to help clear any user
 * info stored at the auth server. Useful for abnormal outage/termination
 * cases.
 *
 * action=mount_add&mount=/live&server=myserver.com&port=8000
 * action=mount_remove&mount=/live&server=myserver.com&port=8000
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
#include <sys/wait.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <curl/curl.h>

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"

#include "logging.h"
#define CATMODULE "auth_url"

typedef struct
{
    int id;
    CURL *curl;
    char *server_id;
    char *location;
    char errormsg [CURL_ERROR_SIZE];
} auth_thread_data;

typedef struct {
    char *addurl;
    char *removeurl;
    char *stream_start;
    char *stream_end;
    char *stream_auth;
    char *username;
    char *password;
    char *auth_header;
    int  auth_header_len;
    char *timelimit_header;
    int  timelimit_header_len;
    char *userpwd;
} auth_url;


static void auth_url_clear(auth_t *self)
{
    auth_url *url;

    INFO0 ("Doing auth URL cleanup");
    url = self->state;
    self->state = NULL;
    free (url->username);
    free (url->password);
    free (url->removeurl);
    free (url->addurl);
    free (url->stream_start);
    free (url->stream_end);
    free (url->stream_auth);
    free (url->auth_header);
    free (url->timelimit_header);
    free (url->userpwd);
    free (url);
}


#ifdef CURLOPT_PASSWDFUNCTION
/* make sure that prompting at the console does not occur */
static int my_getpass(void *client, char *prompt, char *buffer, int buflen)
{
    buffer[0] = '\0';
    return 0;
}
#endif


static int handle_returned_header (void *ptr, size_t size, size_t nmemb, void *stream)
{
    auth_client *auth_user = stream;
    unsigned bytes = size * nmemb;
    client_t *client = auth_user->client;
    auth_thread_data *atd = auth_user->thread_data;

    if (client)
    {
        auth_t *auth = auth_user->auth;
        auth_url *url = auth->state;
        int retcode = 0;

        if (sscanf (ptr, "HTTP/%*u.%*u %3d %*c", &retcode) == 1)
        {
            if (retcode == 403)
            {
                char *p = strchr (ptr, ' ') + 1;
                snprintf (atd->errormsg, sizeof(atd->errormsg), p);
                p = strchr (atd->errormsg, '\r');
                if (p) *p='\0';
            }
        }
        if (strncasecmp (ptr, url->auth_header, url->auth_header_len) == 0)
            client->authenticated = 1;
        if (strncasecmp (ptr, url->timelimit_header, url->timelimit_header_len) == 0)
        {
            unsigned int limit = 0;
            sscanf ((char *)ptr+url->timelimit_header_len, "%u\r\n", &limit);
            client->con->discon_time = time(NULL) + limit;
        }
        if (strncasecmp (ptr, "icecast-slave: 1", 16) == 0)
            client->is_slave =1;

        if (strncasecmp (ptr, "icecast-auth-message: ", 22) == 0)
        {
            char *eol;
            snprintf (atd->errormsg, sizeof (atd->errormsg), "%s", (char*)ptr+22);
            eol = strchr (atd->errormsg, '\r');
            if (eol == NULL)
                eol = strchr (atd->errormsg, '\n');
            if (eol)
                *eol = '\0';
        }
        if (strncasecmp (ptr, "Location: ", 10) == 0)
        {
            int len = strcspn ((char*)ptr+10, "\r\n");
            atd->location = malloc (len+1);
            snprintf (atd->location, len+1, "%s", (char *)ptr+10);
        }
        if (strncasecmp (ptr, "Mountpoint: ", 12) == 0)
        {
            int len = strcspn ((char*)ptr+12, "\r\n");
            auth_user->rejected_mount = malloc (len+1);
            snprintf (auth_user->rejected_mount, len+1, "%s", (char *)ptr+12);
        }
    }

    return (int)bytes;
}

/* capture returned data, but don't do anything with it */
static int handle_returned_data (void *ptr, size_t size, size_t nmemb, void *stream)
{
    return (int)(size*nmemb);
}


static auth_result url_remove_listener (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_url *url = auth_user->auth->state;
    auth_thread_data *atd = auth_user->thread_data;
    time_t duration = time(NULL) - client->con->con_time;
    char *username, *password, *mount, *server, *ipaddr;
    const char *qargs;
    char *userpwd = NULL, post [4096];

    if (url->removeurl == NULL)
        return AUTH_OK;
    server = util_url_escape (auth_user->hostname);

    if (client->username)
        username = util_url_escape (client->username);
    else
        username = strdup ("");

    if (client->password)
        password = util_url_escape (client->password);
    else
        password = strdup ("");

    /* get the full uri (with query params if available) */
    qargs = httpp_getvar (client->parser, HTTPP_VAR_QUERYARGS);
    snprintf (post, sizeof post, "%s%s", auth_user->mount, qargs ? qargs : "");
    mount = util_url_escape (post);
    ipaddr = util_url_escape (client->con->ip);

    snprintf (post, sizeof (post),
            "action=listener_remove&server=%s&port=%d&client=%lu&mount=%s"
            "&user=%s&pass=%s&ip=%s&duration=%lu",
            server, auth_user->port, client->con->id, mount, username,
            password, ipaddr, (long unsigned)duration);
    free (ipaddr);
    free (server);
    free (mount);
    free (username);
    free (password);

    if (strchr (url->removeurl, '@') == NULL)
    {
        if (url->userpwd)
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, url->userpwd);
        else
        {
            /* auth'd requests may not have a user/pass, but may use query args */
            if (client->username && client->password)
            {
                int len = strlen (client->username) + strlen (client->password) + 2;
                userpwd = malloc (len);
                snprintf (userpwd, len, "%s:%s", client->username, client->password);
                curl_easy_setopt (atd->curl, CURLOPT_USERPWD, userpwd);
            }
            else
                curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
        }
    }
    else
    {
        /* url has user/pass but libcurl may need to clear any existing settings */
        curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    }
    curl_easy_setopt (atd->curl, CURLOPT_URL, url->removeurl);
    curl_easy_setopt (atd->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEHEADER, auth_user);

    DEBUG1 ("...handler %d sending request", auth_user->handler);
    if (curl_easy_perform (atd->curl))
        WARN2 ("auth to server %s failed with %s", url->removeurl, atd->errormsg);
    else
        DEBUG1 ("...handler %d request complete", auth_user->handler);

    free (userpwd);

    return AUTH_OK;
}


static auth_result url_add_listener (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_t *auth = auth_user->auth;
    auth_url *url = auth->state;
    auth_thread_data *atd = auth_user->thread_data;
    int res = 0, port;
    const char *agent, *qargs;
    char *user_agent, *username, *password;
    char *mount, *ipaddr, *server;
    ice_config_t *config;
    char *userpwd = NULL, post [4096];

    if (url->addurl == NULL)
        return AUTH_OK;

    config = config_get_config ();
    server = util_url_escape (config->hostname);
    port = config->port;
    config_release_config ();
    agent = httpp_getvar (client->parser, "user-agent");
    if (agent == NULL)
        agent = "-";
    user_agent = util_url_escape (agent);
    if (client->username)
        username  = util_url_escape (client->username);
    else
        username = strdup ("");
    if (client->password)
        password  = util_url_escape (client->password);
    else
        password = strdup ("");

    /* get the full uri (with query params if available) */
    qargs = httpp_getvar (client->parser, HTTPP_VAR_QUERYARGS);
    snprintf (post, sizeof post, "%s%s", auth_user->mount, qargs ? qargs : "");
    mount = util_url_escape (post);
    ipaddr = util_url_escape (client->con->ip);

    snprintf (post, sizeof (post),
            "action=listener_add&server=%s&port=%d&client=%lu&mount=%s"
            "&user=%s&pass=%s&ip=%s&agent=%s",
            server, port, client->con->id, mount, username,
            password, ipaddr, user_agent);
    free (server);
    free (mount);
    free (user_agent);
    free (username);
    free (password);
    free (ipaddr);

    if (strchr (url->addurl, '@') == NULL)
    {
        if (url->userpwd)
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, url->userpwd);
        else
        {
            /* auth'd requests may not have a user/pass, but may use query args */
            if (client->username && client->password)
            {
                int len = strlen (client->username) + strlen (client->password) + 2;
                userpwd = malloc (len);
                snprintf (userpwd, len, "%s:%s", client->username, client->password);
                curl_easy_setopt (atd->curl, CURLOPT_USERPWD, userpwd);
            }
            else
                curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
        }
    }
    else
    {
        /* url has user/pass but libcurl may need to clear any existing settings */
        curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    }
    curl_easy_setopt (atd->curl, CURLOPT_URL, url->addurl);
    curl_easy_setopt (atd->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEHEADER, auth_user);
    atd->errormsg[0] = '\0';

    DEBUG1 ("handler %d sending request", auth_user->handler);
    res = curl_easy_perform (atd->curl);
    DEBUG1 ("handler %d request finished", auth_user->handler);

    free (userpwd);

    if (res)
    {
        WARN2 ("auth to server %s failed with %s", url->addurl, atd->errormsg);
        client_send_403 (client, "Authentication not possible");
        auth_user->client = NULL;
        return AUTH_FAILED;
    }
    if (atd->location)
    {
        client_send_302 (client, atd->location+10);
        auth_user->client = NULL;
        free (atd->location);
        atd->location = NULL;
        return AUTH_FAILED;
    }
    /* we received a response, lets see what it is */
    if (client->authenticated)
        return AUTH_OK;
    if (atoi (atd->errormsg) == 403)
    {
        client_send_403 (client, atd->errormsg+4);
        auth_user->client = NULL;
    }
    INFO2 ("client auth (%s) failed with \"%s\"", url->addurl, atd->errormsg);
    return AUTH_FAILED;
}


/* called by auth thread when a source starts, there is no client_t in
 * this case
 */
static void url_stream_start (auth_client *auth_user)
{
    char *mount, *server, *ipaddr;
    client_t *client = auth_user->client;
    auth_url *url = auth_user->auth->state;
    auth_thread_data *atd = auth_user->thread_data;
    char post [4096];

    server = util_url_escape (auth_user->hostname);
    mount = util_url_escape (auth_user->mount);
    if (client && client->con)
        ipaddr = util_url_escape (client->con->ip);
    else
        ipaddr = strdup("");

    snprintf (post, sizeof (post),
            "action=mount_add&mount=%s&server=%s&port=%d&ip=%s", mount, server, auth_user->port, ipaddr);
    free (ipaddr);
    free (server);
    free (mount);

    if (strchr (url->stream_start, '@') == NULL)
    {
        if (url->userpwd)
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, url->userpwd);
        else
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    }
    else
        curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    curl_easy_setopt (atd->curl, CURLOPT_URL, url->stream_start);
    curl_easy_setopt (atd->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEHEADER, auth_user);

    DEBUG1 ("handler %d sending request", auth_user->handler);
    if (curl_easy_perform (atd->curl))
        WARN2 ("auth to server %s failed with %s", url->stream_start, atd->errormsg);
    DEBUG1 ("handler %d request finished", auth_user->handler);
}


static void url_stream_end (auth_client *auth_user)
{
    char *mount, *server, *ipaddr;
    client_t *client = auth_user->client;
    auth_url *url = auth_user->auth->state;
    auth_thread_data *atd = auth_user->thread_data;
    char post [4096];

    server = util_url_escape (auth_user->hostname);
    mount = util_url_escape (auth_user->mount);
    if (client && client->con)
        ipaddr = util_url_escape (client->con->ip);
    else
        ipaddr = strdup("");

    snprintf (post, sizeof (post),
            "action=mount_remove&mount=%s&server=%s&port=%d&ip=%s", mount, server, auth_user->port, ipaddr);
    free (ipaddr);
    free (server);
    free (mount);

    if (strchr (url->stream_end, '@') == NULL)
    {
        if (url->userpwd)
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, url->userpwd);
        else
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    }
    else
        curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    curl_easy_setopt (atd->curl, CURLOPT_URL, url->stream_end);
    curl_easy_setopt (atd->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEHEADER, auth_user);

    DEBUG1 ("handler %d sending request", auth_user->handler);
    if (curl_easy_perform (atd->curl))
        WARN2 ("auth to server %s failed with %s", url->stream_end, atd->errormsg);
    DEBUG1 ("handler %d request finished", auth_user->handler);
}


static void url_stream_auth (auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_url *url = auth_user->auth->state;
    auth_thread_data *atd = auth_user->thread_data;
    char *mount, *host, *user, *pass, *ipaddr, *admin="";
    char post [4096];

    if (strchr (url->stream_auth, '@') == NULL)
    {
        if (url->userpwd)
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, url->userpwd);
        else
            curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    }
    else
        curl_easy_setopt (atd->curl, CURLOPT_USERPWD, "");
    curl_easy_setopt (atd->curl, CURLOPT_URL, url->stream_auth);
    curl_easy_setopt (atd->curl, CURLOPT_POSTFIELDS, post);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEHEADER, auth_user);
    if (strcmp (auth_user->mount, httpp_getvar (client->parser, HTTPP_VAR_URI)) != 0)
        admin = "&admin=1";
    mount = util_url_escape (auth_user->mount);
    host = util_url_escape (auth_user->hostname);
    user = util_url_escape (client->username);
    pass = util_url_escape (client->password);
    ipaddr = util_url_escape (client->con->ip);

    snprintf (post, sizeof (post),
            "action=stream_auth&mount=%s&ip=%s&server=%s&port=%d&user=%s&pass=%s%s",
            mount, ipaddr, host, auth_user->port, user, pass, admin);
    free (ipaddr);
    free (user);
    free (pass);
    free (mount);
    free (host);

    client->authenticated = 0;
    if (curl_easy_perform (atd->curl))
        WARN2 ("auth to server %s failed with %s", url->stream_auth, atd->errormsg);
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

/* This is called with the config lock held */
static void *alloc_thread_data (auth_t *auth)
{
    auth_thread_data *atd = calloc (1, sizeof (auth_thread_data));
    ice_config_t *config = config_get_config_unlocked();
    atd->server_id = strdup (config->server_id);

    atd->curl = curl_easy_init ();
    curl_easy_setopt (atd->curl, CURLOPT_USERAGENT, atd->server_id);
    curl_easy_setopt (atd->curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
    curl_easy_setopt (atd->curl, CURLOPT_WRITEDATA, atd);
    curl_easy_setopt (atd->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt (atd->curl, CURLOPT_TIMEOUT, 6L);
#ifdef CURLOPT_PASSWDFUNCTION
    curl_easy_setopt (atd->curl, CURLOPT_PASSWDFUNCTION, my_getpass);
#endif
    curl_easy_setopt (atd->curl, CURLOPT_ERRORBUFFER, &atd->errormsg[0]);
    INFO0 ("...handler data initialized");
    return atd;
}


static void release_thread_data (auth_t *auth, void *thread_data)
{
    auth_thread_data *atd = thread_data;
    curl_easy_cleanup (atd->curl);
    free (atd->server_id);
    free (atd);
    INFO1 ("...handler destroyed for %s", auth->mount);
}


int auth_get_url_auth (auth_t *authenticator, config_options_t *options)
{
    auth_url *url_info;

    authenticator->release = auth_url_clear;
    authenticator->adduser = auth_url_adduser;
    authenticator->deleteuser = auth_url_deleteuser;
    authenticator->listuser = auth_url_listuser;
    authenticator->alloc_thread_data = alloc_thread_data;
    authenticator->release_thread_data = release_thread_data;

    url_info = calloc(1, sizeof(auth_url));
    url_info->auth_header = strdup ("icecast-auth-user: 1\r\n");
    url_info->timelimit_header = strdup ("icecast-auth-timelimit:");

    while(options) {
        if(!strcmp(options->name, "username"))
        {
            free (url_info->username);
            url_info->username = strdup (options->value);
        }
        if(!strcmp(options->name, "password"))
        {
            free (url_info->password);
            url_info->password = strdup (options->value);
        }
        if(!strcmp(options->name, "listener_add"))
        {
            authenticator->authenticate = url_add_listener;
            free (url_info->addurl);
            url_info->addurl = strdup (options->value);
        }
        if(!strcmp(options->name, "listener_remove"))
        {
            authenticator->release_listener = url_remove_listener;
            free (url_info->removeurl);
            url_info->removeurl = strdup (options->value);
        }
        if(!strcmp(options->name, "mount_add"))
        {
            authenticator->stream_start = url_stream_start;
            free (url_info->stream_start);
            url_info->stream_start = strdup (options->value);
        }
        if(!strcmp(options->name, "mount_remove"))
        {
            authenticator->stream_end = url_stream_end;
            free (url_info->stream_end);
            url_info->stream_end = strdup (options->value);
        }
        if(!strcmp(options->name, "stream_auth"))
        {
            authenticator->stream_auth = url_stream_auth;
            free (url_info->stream_auth);
            url_info->stream_auth = strdup (options->value);
        }
        if(!strcmp(options->name, "auth_header"))
        {
            free (url_info->auth_header);
            url_info->auth_header = strdup (options->value);
        }
        if (strcmp(options->name, "timelimit_header") == 0)
        {
            free (url_info->timelimit_header);
            url_info->timelimit_header = strdup (options->value);
        }
        options = options->next;
    }

    if (url_info->auth_header)
        url_info->auth_header_len = strlen (url_info->auth_header);
    if (url_info->timelimit_header)
        url_info->timelimit_header_len = strlen (url_info->timelimit_header);
    if (url_info->username && url_info->password)
    {
        int len = strlen (url_info->username) + strlen (url_info->password) + 2;
        url_info->userpwd = malloc (len);
        snprintf (url_info->userpwd, len, "%s:%s", url_info->username, url_info->password);
    }

    authenticator->state = url_info;
    return 0;
}

