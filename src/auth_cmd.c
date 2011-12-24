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

/** 
 * Client authentication via command functions
 *
 * The stated program is started and via it's stdin it is passed
 * mountpoint\n
 * username\n
 * password\n
 * a return code of 0 indicates a valid user, authentication failure if
 * otherwise
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#ifndef WIN32
#include <sys/wait.h>
#endif
#ifdef HAVE_POLL
#include <poll.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"
#include "global.h"

#include "logging.h"
#define CATMODULE "auth_cmd"

typedef struct {
    char *listener_add;
    char *listener_remove;
} auth_cmd;

static void cmd_clear(auth_t *self)
{
    auth_cmd *cmd = self->state;
    free (cmd->listener_add);
    free (cmd->listener_remove);
    free(cmd);
}

static void process_header (const char *p, auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (strncasecmp (p, "Mountpoint: ",12) == 0)
    {
        char *new_mount = strdup (p+12);
        if (new_mount)
        {
            free (auth_user->mount);
            auth_user->mount = new_mount;
        }
        return;
    }

    if (strncasecmp (p, "icecast-auth-user: ", 19) == 0)
    {
        if (strcmp (p+19, "withintro") == 0)
            client->flags |= CLIENT_AUTHENTICATED|CLIENT_HAS_INTRO_CONTENT;
        else if (strcmp (p+19, "1") == 0)
            client->flags |= CLIENT_AUTHENTICATED;
        return;
    }
    if (strncasecmp (p, "icecast-auth-timelimit: ", 24) == 0)
    {
        unsigned limit;
        sscanf (p+24, "%u", &limit);
        client->connection.discon_time = time(NULL) + limit;
    }
}

static void process_body (int fd, pid_t pid, auth_client *auth_user)
{
    client_t *client = auth_user->client;

    if (client->flags & CLIENT_HAS_INTRO_CONTENT)
    {
        refbuf_t *head = client->refbuf, *r = head->next;
        client_t *client = auth_user->client;
        head->next = NULL;
        DEBUG0 ("Have intro content from command");

        while (1)
        {
            int ret;
            unsigned remaining = 4096 - r->len;
            char *buf = r->data + r->len;

#if HAVE_POLL
            struct pollfd response;
            response.fd = fd;
            response.events = POLLIN;
            response.revents = 0;
            ret = poll (&response, 1, 1000);
            if (ret == 0)
            {
                kill (pid, SIGTERM);
                WARN1 ("command timeout triggered for %s", auth_user->mount);
                return;
            }
            if (ret < 0)
                continue;
#endif
            ret = read (fd, buf, remaining);
            if (ret > 0)
            {
                r->len += ret;
                if (r->len == 4096)
                {
                    head->next = r;
                    head = r;
                    r = refbuf_new (4096);
                    r->len = 0;
                }
                continue;
            }
            break;
        }
        if (r->len)
            head->next = r;
        else
            refbuf_release (r);
        if (client->refbuf->next == NULL)
            client->flags &= ~CLIENT_HAS_INTRO_CONTENT;
    }
}

static void get_response (int fd, auth_client *auth_user, pid_t pid)
{
    client_t *client = auth_user->client;
    refbuf_t *r = client->refbuf;
    char *buf = r->data, *blankline;
    unsigned remaining = 4095; /* leave a nul char at least */
    int ret;

    memset (r->data, 0, remaining+1);
    while (remaining)
    {
#if HAVE_POLL
        struct pollfd response;
        response.fd = fd;
        response.events = POLLIN;
        response.revents = 0;
        ret = poll (&response, 1, 1000);
        if (ret == 0)
        {
            kill (pid, SIGTERM);
            WARN1 ("command timeout triggered for %s", auth_user->mount);
            return;
        }
        if (ret < 0)
            continue;
#endif
        ret = read (fd, buf, remaining);
        if (ret <= 0)
            break;
        blankline = strstr (r->data, "\n\n");
        if (blankline)
        {
            char *p = r->data;
            do {
                char *nl = strchr (p, '\n');
                *nl = '\0';
                process_header (p, auth_user);
                p = nl+1;
            } while (*p != '\n');
            if (client->flags & CLIENT_HAS_INTRO_CONTENT)
            {
                r->len = (buf+ret) - (blankline + 2);
                if (r->len)
                    memmove (r->data, blankline+2, r->len);
                client->refbuf = refbuf_new (4096);
                client->refbuf->next = r;
            }
            process_body (fd, pid, auth_user);
            return;
        }
        buf += ret;
        remaining -= ret;
    }
    return;
}


static auth_result auth_cmd_client (auth_client *auth_user)
{
    int infd[2], outfd[2];
    pid_t pid;
    client_t *client = auth_user->client;
    auth_t *auth = auth_user->auth;
    auth_cmd *cmd = auth->state;
    int status, len;
    const char *qargs;
    char str[512];

    if (auth->running == 0)
        return AUTH_FAILED;
    if (pipe (infd) < 0 || pipe (outfd) < 0)
    {
        ERROR1 ("pipe failed code %d", errno);
        return AUTH_FAILED;
    }
    pid = fork();
    switch (pid)
    {
        case 0: /* child */
            dup2 (outfd[0], 0);
            dup2 (infd[1], 1);
            close (outfd[1]);
            close (infd[0]);
            execl (cmd->listener_add, cmd->listener_add, NULL);
            ERROR1 ("unable to exec command \"%s\"", cmd->listener_add);
            exit (-1);
        case -1:
            break;
        default: /* parent */
            close (outfd[0]);
            close (infd[1]);
            qargs = httpp_getvar (client->parser, HTTPP_VAR_QUERYARGS);
            len = snprintf (str, sizeof(str),
                    "Mountpoint: %s%s\n"
                    "User: %s\n"
                    "Pass: %s\n"
                    "IP: %s\n"
                    "Agent: %s\n\n"
                    , auth_user->mount, qargs ? qargs : "",
                    client->username ? client->username : "",
                    client->password ? client->password : "",
                    client->connection.ip,
                    httpp_getvar (client->parser, "user-agent"));
            write (outfd[1], str, len);
            close (outfd[1]);
            get_response (infd[0], auth_user, pid);
            close (infd[0]);
            DEBUG1 ("Waiting on pid %ld", (long)pid);
            if (waitpid (pid, &status, 0) < 0)
            {
                DEBUG1("waitpid error %s", strerror(errno));
                break;
            }
            if (client->flags & CLIENT_AUTHENTICATED)
                return AUTH_OK;
    }
    return AUTH_FAILED;
}

static auth_result auth_cmd_adduser(auth_t *auth, const char *username, const char *password)
{
    return AUTH_FAILED;
}

static auth_result auth_cmd_deleteuser (auth_t *auth, const char *username)
{
    return AUTH_FAILED;
}

static auth_result auth_cmd_listuser (auth_t *auth, xmlNodePtr srcnode)
{
    return AUTH_FAILED;
}

int auth_get_cmd_auth (auth_t *authenticator, config_options_t *options)
{
    auth_cmd *state;

    authenticator->authenticate = auth_cmd_client;
    authenticator->release = cmd_clear;
    authenticator->adduser = auth_cmd_adduser;
    authenticator->deleteuser = auth_cmd_deleteuser;
    authenticator->listuser = auth_cmd_listuser;

    state = calloc(1, sizeof(auth_cmd));

    while(options) {
        if (strcmp (options->name, "listener_add") == 0)
            state->listener_add = strdup (options->value);
        if (strcmp (options->name, "listener_remove") == 0)
            state->listener_remove = strdup (options->value);
        options = options->next;
    }
    if (state->listener_add == NULL)
    {
        ERROR0 ("No command specified for authentication");
        return -1;
    }
    authenticator->state = state;
    INFO0("external command based authentication setup");
    return 0;
}

