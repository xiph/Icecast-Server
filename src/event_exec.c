/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef _WIN32
#include <sys/wait.h>
/* for __setup_empty_script_environment() */
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "event.h"
#include "logging.h"
#define CATMODULE "event_exec"


typedef struct event_exec {
/* REVIEW: Some ideas for future work:
   <option type="environ" name="TEST" value="Blubb" />
   <option name="argument" value="--test" />
   <option type="argument" value="--test" />
   <option name="default_arguments" value="false" />
*/
    /* name and path of executable */
    char *executable;
} event_exec_t;

#ifdef _WIN32
/* TODO #2101: Implement script executing on win* */
#else
/* this sets up the new environment for script execution.
 * We ignore most failtures as we can not handle them anyway.
 */
#ifdef HAVE_SETENV
static inline void __update_environ(const char *name, const char *value) {
    if (!name || !value) return;
    setenv(name, value, 1);
}
#else
#define __update_environ(x,y)
#endif
static inline void __setup_empty_script_environment(event_exec_t *self, event_t *event) {
    ice_config_t * config = config_get_config();
    mount_proxy *mountinfo;
    source_t *source;
    char buf[80];
    int i;

    /* close at least the first 1024 handles */
    for (i = 0; i < 1024; i++)
        close(i);

    /* open null device */
    i = open(config->null_device, O_RDWR);
    if (i != -1) {
        /* attach null device to stdin, stdout and stderr */
        if (i != 0)
            dup2(i, 0);
        if (i != 1)
            dup2(i, 1);
        if (i != 2)
            dup2(i, 2);

        /* close null device */
        if (i > 2)
            close(i);
    }

    __update_environ("ICECAST_VERSION",   ICECAST_VERSION_STRING);
    __update_environ("ICECAST_HOSTNAME",  config->hostname);
    __update_environ("ICECAST_ADMIN",     config->admin);
    __update_environ("ICECAST_LOGDIR",    config->log_dir);
    __update_environ("EVENT_URI",         event->uri);
    __update_environ("EVENT_TRIGGER",     event->trigger); /* new name */
    __update_environ("SOURCE_ACTION",     event->trigger); /* old name */
    __update_environ("CLIENT_IP",         event->connection_ip);
    __update_environ("CLIENT_ROLE",       event->client_role);
    __update_environ("CLIENT_USERNAME",   event->client_username);
    __update_environ("CLIENT_USERAGENT",  event->client_useragent);

    snprintf(buf, sizeof(buf), "%lu", event->connection_id);
    __update_environ("CLIENT_ID",         buf);
    snprintf(buf, sizeof(buf), "%lli", (long long int)event->connection_time);
    __update_environ("CLIENT_CONNECTION_TIME", buf);
    snprintf(buf, sizeof(buf), "%i", event->client_admin_command);
    __update_environ("CLIENT_ADMIN_COMMAND", buf);

    mountinfo = config_find_mount(config, event->uri, MOUNT_TYPE_NORMAL);
    if (mountinfo) {
        __update_environ("MOUNT_NAME",        mountinfo->stream_name);
        __update_environ("MOUNT_DESCRIPTION", mountinfo->stream_description);
        __update_environ("MOUNT_URL",         mountinfo->stream_url);
        __update_environ("MOUNT_GENRE",       mountinfo->stream_genre);
    }

    avl_tree_rlock(global.source_tree);
    source = source_find_mount(event->uri);
    if (source) {
        __update_environ("SOURCE_MOUNTPOINT", source->mount);
        __update_environ("SOURCE_PUBLIC",     source->yp_public ? "true" : "false");
        __update_environ("SROUCE_HIDDEN",     source->hidden    ? "true" : "false");
    }
    avl_tree_unlock(global.source_tree);

    config_release_config();
}

static void _run_script (event_exec_t *self, event_t *event) {
    pid_t pid, external_pid;

    /* do a fork twice so that the command has init as parent */
    external_pid = fork();
    switch (external_pid)
    {
        case 0:
            switch (pid = fork ())
            {
                case -1:
                    ICECAST_LOG_ERROR("Unable to fork %s (%s)", self->executable, strerror (errno));
                    break;
                case 0:  /* child */
                    if (access(self->executable, R_OK|X_OK) != 0) {
                        ICECAST_LOG_ERROR("Unable to run command %s (%s)", self->executable, strerror(errno));
                        exit(1);
                    }
                    ICECAST_LOG_DEBUG("Starting command %s", self->executable);
                    __setup_empty_script_environment(self, event);
                    /* consider to add action here as well */
                    if (event->uri) {
                        execl(self->executable, self->executable, event->uri, (char *)NULL);
                    } else {
                        execl(self->executable, self->executable, (char *)NULL);
                    }
                    exit(1);
                default: /* parent */
                    break;
            }
            exit (0);
        case -1:
            ICECAST_LOG_ERROR("Unable to fork %s", strerror (errno));
            break;
        default: /* parent */
            waitpid (external_pid, NULL, 0);
            break;
    }
}
#endif

static int event_exec_emit(void *state, event_t *event) {
    event_exec_t *self = state;
#ifdef _WIN32
    ICECAST_LOG_ERROR("<event type=\"exec\" ...> not supported on win*");
#else
    _run_script(self, event);
#endif
    return 0;
}

static void event_exec_free(void *state) {
    event_exec_t *self = state;
    free(self);
}

int event_get_exec(event_registration_t *er, config_options_t *options) {
    event_exec_t *self = calloc(1, sizeof(event_exec_t));

    if (!self)
        return -1;

    if (options) {
        do {
            if (options->type)
                continue;
            if (!options->name)
                continue;
            /* BEFORE RELEASE 2.4.2 DOCUMENT: Document supported options:
             * <option name="executable" value="..." />
             */
            if (strcmp(options->name, "executable") == 0) {
                free(self->executable);
                self->executable = NULL;
                if (options->value)
                    self->executable = strdup(options->value);
            } else {
                ICECAST_LOG_ERROR("Unknown <option> tag with name %s.", options->name);
            }
        } while ((options = options->next));
    }

    if (!self->executable) {
        ICECAST_LOG_ERROR("No executable given.");
        event_exec_free(self);
        return -1;
    }

    er->state = self;
    er->emit = event_exec_emit;
    er->free = event_exec_free;
    return 0;
}
