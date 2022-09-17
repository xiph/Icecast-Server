/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>

#ifndef _WIN32
#include <sys/wait.h>
/* for __setup_empty_script_environment() */
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "event.h"
#include "global.h"
#include "source.h"
#include "logging.h"
#define CATMODULE "event_exec"

typedef enum event_exec_argvtype_tag {
    ARGVTYPE_NO_DEFAULTS = 0,
    ARGVTYPE_ONLY_URI,
    ARGVTYPE_URI_AND_TRIGGER,
    ARGVTYPE_LEGACY,

    ARGVTYPE_DFAULT = ARGVTYPE_LEGACY
} event_exec_argvtype_t;

typedef struct event_exec {
    /* name and path of executable */
    char *executable;

    /* what to add to argv[] */
    event_exec_argvtype_t argvtype;

    /* actual argv[] */
    char **argv;
} event_exec_t;

static char *_null_aware_strdup(const char *s)
{
    if (!s)
        return NULL;
    return strdup(s);
}

/* OS independed code: */
static inline size_t __argvtype2offset(event_exec_argvtype_t argvtype) {
    switch (argvtype) {
        case ARGVTYPE_NO_DEFAULTS:     return 1; break;
        case ARGVTYPE_ONLY_URI:        return 2; break;
        case ARGVTYPE_URI_AND_TRIGGER: return 3; break;
        case ARGVTYPE_LEGACY:          return 2; break;
        default: return 0; break; /* This should never happen. */
    }
}

/* BEFORE RELEASE 2.5.0 DOCUMENT: Document names of possible values. */
static inline event_exec_argvtype_t __str2argvtype(const char *str) {
    if (!str)
        str = "(BAD VALUE)";

    if (strcmp(str, "default") == 0) {
        return ARGVTYPE_DFAULT;
    } else if (strcmp(str, "no_defaults") == 0) {
        return ARGVTYPE_NO_DEFAULTS;
    } else if (strcmp(str, "uri") == 0) {
        return ARGVTYPE_ONLY_URI;
    } else if (strcmp(str, "uri_and_trigger") == 0) {
        return ARGVTYPE_URI_AND_TRIGGER;
    } else if (strcmp(str, "legacy") == 0) {
        return ARGVTYPE_LEGACY;
    } else {
        ICECAST_LOG_ERROR("Unknown argument type %s, using \"default\"", str);
        return ARGVTYPE_DFAULT;
    }
}

static inline char **__setup_argv(event_exec_t *self, event_t *event) {
    char *uri;

    self->argv[0] = self->executable;

    switch (self->argvtype) {
        case ARGVTYPE_NO_DEFAULTS:
            /* nothing to do */
        break;
        case ARGVTYPE_URI_AND_TRIGGER:
            self->argv[2] = event->trigger ? event->trigger : "";
        /* fall through */
        case ARGVTYPE_ONLY_URI:
            uri = _null_aware_strdup(event_extra_get(event, EVENT_EXTRA_KEY_URI));
            self->argv[1] = uri ? uri : "";
        break;
        case ARGVTYPE_LEGACY:
            /* This mode is similar to ARGVTYPE_ONLY_URI
             * but if URI is unknown the parameter is skipped!
             */
            uri = _null_aware_strdup(event_extra_get(event, EVENT_EXTRA_KEY_URI));
            if (uri) {
                self->argv[1] = uri;
            } else {
                self->argv[1] = self->executable;
                return &self->argv[1];
            }
        break;
    }

    return self->argv;
}

/* OS depended code: */
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

static inline void __update_environ_with_key(const event_t *event, const char *name, event_extra_key_t key)
{
    __update_environ(name, event_extra_get(event, key));
}

static inline void __setup_environ(ice_config_t *config, event_exec_t *self, event_t *event) {
    mount_proxy *mountinfo;
    source_t *source;
    char buf[80];

    /* BEFORE RELEASE 2.5.0 DOCUMENT: Document all those env vars. */
    __update_environ("ICECAST_VERSION",   ICECAST_VERSION_STRING);
    __update_environ("ICECAST_HOSTNAME",  config->hostname);
    __update_environ("ICECAST_ADMIN",     config->admin);
    __update_environ("ICECAST_LOGDIR",    config->log_dir);
    __update_environ("EVENT_TRIGGER",     event->trigger); /* new name */
    __update_environ("SOURCE_ACTION",     event->trigger); /* old name (deprecated) */
    __update_environ_with_key(event, "EVENT_URI", EVENT_EXTRA_KEY_URI);
    __update_environ_with_key(event, "SOURCE_MEDIA_TYPE", EVENT_EXTRA_KEY_SOURCE_MEDIA_TYPE);
    __update_environ_with_key(event, "CLIENT_IP", EVENT_EXTRA_KEY_CONNECTION_IP);
    __update_environ_with_key(event, "CLIENT_ROLE", EVENT_EXTRA_KEY_CLIENT_ROLE);
    __update_environ_with_key(event, "CLIENT_USERNAME", EVENT_EXTRA_KEY_CLIENT_USERNAME);
    __update_environ_with_key(event, "CLIENT_USERAGENT", EVENT_EXTRA_KEY_CLIENT_USERAGENT);

    snprintf(buf, sizeof(buf), "%lu", event->connection_id);
    __update_environ("CLIENT_ID",         buf);
    snprintf(buf, sizeof(buf), "%lli", (long long int)event->connection_time);
    __update_environ("CLIENT_CONNECTION_TIME", buf);
    snprintf(buf, sizeof(buf), "%i", event->client_admin_command);
    __update_environ("CLIENT_ADMIN_COMMAND", buf);

    mountinfo = config_find_mount(config, event_extra_get(event, EVENT_EXTRA_KEY_URI), MOUNT_TYPE_NORMAL);
    if (mountinfo) {
        __update_environ("MOUNT_NAME",        mountinfo->stream_name);
        __update_environ("MOUNT_DESCRIPTION", mountinfo->stream_description);
        __update_environ("MOUNT_URL",         mountinfo->stream_url);
        __update_environ("MOUNT_GENRE",       mountinfo->stream_genre);
    }

    avl_tree_rlock(global.source_tree);
    source = source_find_mount(event_extra_get(event, EVENT_EXTRA_KEY_URI));
    if (source) {
        __update_environ("SOURCE_MOUNTPOINT", source->mount);
        __update_environ("SOURCE_PUBLIC",     source->yp_public ? "true" : "false");
        __update_environ("SROUCE_HIDDEN",     source->hidden    ? "true" : "false");
    }
    avl_tree_unlock(global.source_tree);
}

static inline void __setup_file_descriptors(ice_config_t *config) {
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
}

static inline void __setup_empty_script_environment(event_exec_t *self, event_t *event) {
    ice_config_t *config = config_get_config();

    __setup_file_descriptors(config);
    __setup_environ(config, self, event);

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
                    execv(self->executable, __setup_argv(self, event));
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
    /* BEFORE RELEASE 2.5.0 DOCUMENT: Document this not working on win*. */
    ICECAST_LOG_ERROR("<event type=\"exec\" ...> not supported on Windows");
#else
    _run_script(self, event);
#endif
    return 0;
}

static void event_exec_free(void *state) {
    event_exec_t *self = state;
    size_t i;

    for (i = __argvtype2offset(self->argvtype); self->argv[i]; i++)
        free(self->argv[i]);

    free(self->argv);
    free(self->executable);
    free(self);
}

int event_get_exec(event_registration_t *er, config_options_t *options) {
    event_exec_t *self = calloc(1, sizeof(event_exec_t));
    config_options_t *cur;
    size_t extra_argc = 0;

    if (!self)
        return -1;

    self->argvtype = ARGVTYPE_DFAULT;

    if ((cur = options)) {
        do {
            if (cur->name) {
                /* BEFORE RELEASE 2.5.0 DOCUMENT: Document supported options:
                 * <option name="executable" value="..." />
                 * <option name="default_arguments" value="..." /> (for values see near top of documment)
                 */
                if (strcmp(cur->name, "executable") == 0) {
                    util_replace_string(&(self->executable), cur->value);
                } else if (strcmp(cur->name, "default_arguments") == 0) {
                    self->argvtype = __str2argvtype(cur->value);
                } else {
                    ICECAST_LOG_ERROR("Unknown <option> tag with name %s.", cur->name);
                }
            } else if (cur->type) {
                if (strcmp(cur->type, "argument") == 0) {
                    extra_argc++;
                } else {
                    ICECAST_LOG_ERROR("Unknown <option> tag with type %s.", cur->type);
                }
            }
        } while ((cur = cur->next));
    }

    if (!self->executable) {
        ICECAST_LOG_ERROR("No executable given.");
        event_exec_free(self);
        return -1;
    }

    self->argv = calloc(__argvtype2offset(self->argvtype) + extra_argc + 1, sizeof(char*));
    if (!self->argv) {
        ICECAST_LOG_ERROR("Can not allocate argv[]");
        event_exec_free(self);
        return -1;
    }

    extra_argc = __argvtype2offset(self->argvtype);

    if ((cur = options)) {
        do {
            if (cur->type) {
                /* BEFORE RELEASE 2.5.0 DOCUMENT: Document supported options:
                 * <option type="argument" value="..." />
                 */
                if (strcmp(cur->type, "argument") == 0) {
                    if (cur->value) {
                        self->argv[extra_argc] = strdup(cur->value);
                        if (!self->argv[extra_argc]) {
                            ICECAST_LOG_ERROR("Can not allocate argv[x]");
                            event_exec_free(self);
                            return -1;
                        }
                        extra_argc++;
                    }
                } else {
                    ICECAST_LOG_ERROR("Unknown <option> tag with type %s.", cur->type);
                }
            }
        } while ((cur = cur->next));
    }

    er->state = self;
    er->emit = event_exec_emit;
    er->free = event_exec_free;
    return 0;
}
