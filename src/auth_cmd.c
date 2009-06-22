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

static void cmd_clear(auth_t *self)
{
    auth_cmd *cmd = self->state;
    free(cmd->filename);
    free(cmd);
}

static auth_result auth_cmd_client (auth_client *auth_user)
{
    int fd[2];
    pid_t pid;
    client_t *client = auth_user->client;
    auth_t *auth = auth_user->auth;
    auth_cmd *cmd = auth->state;
    int status, len;
    char str[512];

    if (client->username == NULL || client->password == NULL)
        return AUTH_FAILED;

    if (pipe (fd) == 0)
    {
        pid = fork();
        switch (pid)
        {
            case 0: /* child */
                dup2 (fd[0], fileno(stdin));
                close (fd[0]);
                close (fd[1]);
                execl (cmd->filename, cmd->filename, NULL);
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
                        return AUTH_OK;
                }
                break;
        }
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
        if(!strcmp(options->name, "filename"))
            state->filename = strdup(options->value);
        options = options->next;
    }
    if (state->filename == NULL)
    {
        ERROR0 ("No command specified for authentication");
        return -1;
    }
    authenticator->state = state;
    INFO0("external command based authentication setup");
    return 0;
}

