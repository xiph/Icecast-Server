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

#include "auth.h"
#include "source.h"
#include "client.h"
#include "cfgfile.h"
#include "httpp/httpp.h"

#include "logging.h"
#define CATMODULE "auth_cmd"

typedef struct {
    char *filename;
} auth_cmd;

typedef struct {
   char *mount;
   client_t *client;
   char *cmd;
} auth_client;


static void cmd_clear(auth_t *self)
{
    auth_cmd *cmd = self->state;
    free(cmd->filename);
    free(cmd);
}

static void *auth_cmd_thread (void *arg)
{
    auth_client *auth_user = arg;
    int fd[2];
    int ok = 0;
    int send_fail = 404;
    pid_t pid;
    client_t *client = auth_user->client;
    int status, len;
    char str[512];

    DEBUG0("starting auth thread");
    if (pipe (fd) == 0)
    {
        pid = fork();
        switch (pid)
        {
            case 0: /* child */
                dup2 (fd[0], fileno(stdin));
                close (fd[0]);
                close (fd[1]);
                execl (auth_user->cmd, auth_user->cmd, NULL);
                exit (EXIT_FAILURE);
            case -1:
                break;
            default: /* parent */
                close (fd[0]);
                len = snprintf (str, sizeof(str), "%s\n%s\n%s\n",
                        auth_user->mount, client->username, client->password);
                write (fd[1], str, len);
                close (fd[1]);
                DEBUG1 ("Waiting on pid %ld", (long)pid);
                if (waitpid (pid, &status, 0) < 0)
                {
                    DEBUG1("waitpid error %s", strerror(errno));
                    break;
                }
                if (WIFEXITED (status))
                {
                    DEBUG1("command exited normally with %d", WEXITSTATUS (status));
                    if (WEXITSTATUS(status) == 0)
                        ok = 1;
                    else
                        send_fail = 401;
                }
                break;
        }
        /* try to add authenticated client */
        if (ok)
        {
            auth_postprocess_client (auth_user->mount, auth_user->client);
            send_fail = 0;
        }
        else
            auth_failed_client (auth_user->mount);
    }
    if (send_fail == 404)
        client_send_404 (client, "Mount not available");
    if (send_fail == 401)
        client_send_401 (client);
    free (auth_user->mount);
    free (auth_user->cmd);
    free (auth_user);
    return NULL;
}


static auth_result auth_cmd_client (source_t *source, client_t *client)
{
    auth_client *auth_user = calloc (1, sizeof (auth_client));
    auth_cmd *cmd = source->authenticator->state;

    if (auth_user == NULL)
        return AUTH_FAILED;

    auth_user->cmd = strdup (cmd->filename);
    auth_user->mount = strdup (source->mount);
    auth_user->client = client;
    thread_create("Auth by command thread", auth_cmd_thread, auth_user, THREAD_DETACHED);
    return AUTH_OK;
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

auth_t *auth_get_cmd_auth (config_options_t *options)
{
    auth_t *authenticator = calloc(1, sizeof(auth_t));
    auth_cmd *state;

    authenticator->authenticate = auth_cmd_client;
    authenticator->free = cmd_clear;
    authenticator->adduser = auth_cmd_adduser;
    authenticator->deleteuser = auth_cmd_deleteuser;
    authenticator->listuser = auth_cmd_listuser;

    state = calloc(1, sizeof(auth_cmd));

    while(options) {
        if(!strcmp(options->name, "filename"))
            state->filename = strdup(options->value);
        options = options->next;
    }
    authenticator->state = state;
    INFO0("external command based authentication setup");
    return authenticator;
}

