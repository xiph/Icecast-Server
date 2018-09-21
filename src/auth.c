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
 * Copyright 2013-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "auth.h"
#include "source.h"
#include "client.h"
#include "errors.h"
#include "cfgfile.h"
#include "stats.h"
#include "common/httpp/httpp.h"
#include "fserve.h"
#include "admin.h"
#include "acl.h"

#include "logging.h"
#define CATMODULE "auth"

/* data structures */
struct auth_stack_tag {
    size_t refcount;
    auth_t *auth;
    mutex_t lock;
    auth_stack_t *next;
};

/* code */
static void __handle_auth_client(auth_t *auth, auth_client *auth_user);

static mutex_t _auth_lock; /* protects _current_id */
static volatile unsigned long _current_id = 0;

static unsigned long _next_auth_id(void) {
    unsigned long id;

    thread_mutex_lock(&_auth_lock);
    id = _current_id++;
    thread_mutex_unlock(&_auth_lock);

    return id;
}

static const struct {
    auth_result result;
    const char *string;
} __auth_results[] = {
    {.result = AUTH_UNDEFINED,      .string = "undefined"},
    {.result = AUTH_OK,             .string = "ok"},
    {.result = AUTH_FAILED,         .string = "failed"},
    {.result = AUTH_RELEASED,       .string = "released"},
    {.result = AUTH_FORBIDDEN,      .string = "forbidden"},
    {.result = AUTH_NOMATCH,        .string = "no match"},
    {.result = AUTH_USERADDED,      .string = "user added"},
    {.result = AUTH_USEREXISTS,     .string = "user exists"},
    {.result = AUTH_USERDELETED,    .string = "user deleted"}
};

static const char *auth_result2str(auth_result res)
{
    size_t i;

    for (i = 0; i < (sizeof(__auth_results)/sizeof(*__auth_results)); i++) {
        if (__auth_results[i].result == res)
            return __auth_results[i].string;
    }

    return "(unknown)";
}

auth_result auth_str2result(const char *str)
{
    size_t i;

    for (i = 0; i < (sizeof(__auth_results)/sizeof(*__auth_results)); i++) {
        if (strcasecmp(__auth_results[i].string, str) == 0)
            return __auth_results[i].result;
    }

    return AUTH_FAILED;
}

static auth_client *auth_client_setup (client_t *client)
{
    /* This will look something like "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==" */
    auth_client *auth_user;

    do {
        const char *header;
        char *userpass, *tmp;
        char *username, *password;

        /* check if we already have auth infos */
        if (client->username || client->password)
            break;

        header = httpp_getvar(client->parser, "authorization");

        if (header == NULL)
            break;

        if (strncmp(header, "Basic ", 6) == 0) {
            userpass = util_base64_decode (header+6);
            if (userpass == NULL) {
                ICECAST_LOG_WARN("Base64 decode of Authorization header \"%s\" failed",
                        header+6);
                break;
            }

            tmp = strchr(userpass, ':');
            if (tmp == NULL) {
                free(userpass);
                break;
            }

            *tmp = 0;
            username = userpass;
            password = tmp+1;
            client->username = strdup(username);
            client->password = strdup(password);
            free (userpass);
            break;
        }
        ICECAST_LOG_INFO("unhandled authorization header: %s", header);
    } while (0);

    auth_user = calloc(1, sizeof(auth_client));
    auth_user->client = client;
    return auth_user;
}


static void queue_auth_client (auth_client *auth_user)
{
    auth_t *auth;

    if (auth_user == NULL)
        return;
    auth_user->next = NULL;
    if (auth_user->client == NULL || auth_user->client->auth == NULL)
    {
        ICECAST_LOG_WARN("internal state is incorrect for %p", auth_user->client);
        return;
    }
    auth = auth_user->client->auth;
    ICECAST_LOG_DEBUG("...refcount on auth_t %s is now %d", auth->mount, (int)auth->refcount);
    if (auth->immediate) {
        __handle_auth_client(auth, auth_user);
    } else {
        thread_mutex_lock (&auth->lock);
        *auth->tailp = auth_user;
        auth->tailp = &auth_user->next;
        auth->pending_count++;
        ICECAST_LOG_INFO("auth on %s has %d pending", auth->mount, auth->pending_count);
        thread_mutex_unlock (&auth->lock);
    }
}


/* release the auth. It is referred to by multiple structures so this is
 * refcounted and only actual freed after the last use
 */
void auth_release (auth_t *authenticator) {
    if (authenticator == NULL)
        return;

    thread_mutex_lock(&authenticator->lock);
    authenticator->refcount--;
    ICECAST_LOG_DEBUG("...refcount on auth_t %s is now %d", authenticator->mount, (int)authenticator->refcount);
    if (authenticator->refcount)
    {
        thread_mutex_unlock(&authenticator->lock);
        return;
    }

    /* cleanup auth thread attached to this auth */
    if (authenticator->running) {
        authenticator->running = 0;
        thread_mutex_unlock(&authenticator->lock);
        thread_join(authenticator->thread);
        thread_mutex_lock(&authenticator->lock);
    }

    if (authenticator->free)
        authenticator->free(authenticator);
    if (authenticator->type)
        xmlFree (authenticator->type);
    if (authenticator->role)
        xmlFree (authenticator->role);
    if (authenticator->management_url)
        xmlFree (authenticator->management_url);
    thread_mutex_unlock(&authenticator->lock);
    thread_mutex_destroy(&authenticator->lock);
    if (authenticator->mount)
        free(authenticator->mount);
    acl_release(authenticator->acl);
    free(authenticator);
}

/* increment refcount on the auth.
 */
void    auth_addref (auth_t *authenticator) {
    if (authenticator == NULL)
        return;

    thread_mutex_lock (&authenticator->lock);
    authenticator->refcount++;
    ICECAST_LOG_DEBUG("...refcount on auth_t %s is now %d", authenticator->mount, (int)authenticator->refcount);
    thread_mutex_unlock (&authenticator->lock);
}

static void auth_client_free (auth_client *auth_user)
{
    if (!auth_user)
        return;

    free(auth_user->alter_client_arg);
    free(auth_user);
}


/* verify that the client is still connected. */
static int is_client_connected (client_t *client) {
/* As long as sock_active() is broken we need to disable this:

    int ret = 1;
    if (client)
        if (sock_active(client->con->sock) == 0)
            ret = 0;
    return ret;
*/
    return 1;
}

static auth_result auth_new_client (auth_t *auth, auth_client *auth_user) {
    client_t *client = auth_user->client;
    auth_result ret = AUTH_FAILED;

    /* make sure there is still a client at this point, a slow backend request
     * can be avoided if client has disconnected */
    if (is_client_connected(client) == 0) {
        ICECAST_LOG_DEBUG("client is no longer connected");
        client->respcode = 400;
        auth_release (client->auth);
        client->auth = NULL;
        return AUTH_FAILED;
    }

    if (auth->authenticate_client) {
        ret = auth->authenticate_client(auth_user);
        if (ret != AUTH_OK)
        {
            auth_release (client->auth);
            client->auth = NULL;
            return ret;
        }
    }
    return ret;
}


/* wrapper function for auth thread to drop client connections
 */
static auth_result auth_remove_client(auth_t *auth, auth_client *auth_user)
{
    client_t *client = auth_user->client;
    auth_result ret = AUTH_RELEASED;

    (void)auth;

    if (client->auth->release_client)
        ret = client->auth->release_client(auth_user);

    auth_release(client->auth);
    client->auth = NULL;

    /* client is going, so auth is not an issue at this point */
    acl_release(client->acl);
    client->acl = NULL;

    return ret;
}

static inline int __handle_auth_client_alter(auth_t *auth, auth_client *auth_user)
{
    client_t *client = auth_user->client;
    const char *uuid = NULL;
    const char *location = NULL;
    int http_status = 0;

    void client_send_redirect(client_t *client, const char *uuid, int status, const char *location);

    switch (auth_user->alter_client_action) {
        case AUTH_ALTER_NOOP:
            return 0;
        break;
        case AUTH_ALTER_REWRITE:
            free(client->uri);
            client->uri = auth_user->alter_client_arg;
            auth_user->alter_client_arg = NULL;
            return 0;
        break;
        case AUTH_ALTER_REDIRECT:
        /* fall through */
        case AUTH_ALTER_REDIRECT_SEE_OTHER:
            uuid = "be7fac90-54fb-4673-9e0d-d15d6a4963a2";
            http_status = 303;
            location = auth_user->alter_client_arg;
        break;
        case AUTH_ALTER_REDIRECT_TEMPORARY:
            uuid = "4b08a03a-ecce-4981-badf-26b0bb6c9d9c";
            http_status = 307;
            location = auth_user->alter_client_arg;
        break;
        case AUTH_ALTER_REDIRECT_PERMANENT:
            uuid = "36bf6815-95cb-4cc8-a7b0-6b4b0c82ac5d";
            http_status = 308;
            location = auth_user->alter_client_arg;
        break;
        case AUTH_ALTER_SEND_ERROR:
            client_send_error_by_uuid(client, auth_user->alter_client_arg);
            return 1;
        break;
    }

    if (uuid && location && http_status) {
        client_send_redirect(client, uuid, http_status, location);
        return 1;
    }

    return -1;
}
static void __handle_auth_client (auth_t *auth, auth_client *auth_user) {
    auth_result result;

    if (auth_user->process) {
        result = auth_user->process(auth, auth_user);
    } else {
        ICECAST_LOG_ERROR("client auth process not set");
        result = AUTH_FAILED;
    }

    ICECAST_LOG_DEBUG("client %p on auth %p role %s processed: %s", auth_user->client, auth, auth->role, auth_result2str(result));

    if (result == AUTH_OK) {
        if (auth_user->client->acl)
            acl_release(auth_user->client->acl);
        acl_addref(auth_user->client->acl = auth->acl);
        if (auth->role && !auth_user->client->role) /* TODO: Handle errors here */
            auth_user->client->role = strdup(auth->role);
    }

    if (result != AUTH_NOMATCH) {
        if (__handle_auth_client_alter(auth, auth_user) == 1)
            return;
    }

    if (result == AUTH_NOMATCH && auth_user->on_no_match) {
        auth_user->on_no_match(auth_user->client, auth_user->on_result, auth_user->userdata);
    } else if (auth_user->on_result) {
        auth_user->on_result(auth_user->client, auth_user->userdata, result);
    }

    auth_client_free (auth_user);
}

/* The auth thread main loop. */
static void *auth_run_thread (void *arg)
{
    auth_t *auth = arg;

    ICECAST_LOG_INFO("Authentication thread started");
    while (1) {
        thread_mutex_lock(&auth->lock);

        if (!auth->running) {
            thread_mutex_unlock(&auth->lock);
            break;
        }

        if (auth->head) {
            auth_client *auth_user;

            /* may become NULL before lock taken */
            auth_user = (auth_client*)auth->head;
            if (auth_user == NULL)
            {
                thread_mutex_unlock (&auth->lock);
                continue;
            }
            ICECAST_LOG_DEBUG("%d client(s) pending on %s (role %s)", auth->pending_count, auth->mount, auth->role);
            auth->head = auth_user->next;
            if (auth->head == NULL)
                auth->tailp = &auth->head;
            auth->pending_count--;
            thread_mutex_unlock(&auth->lock);
            auth_user->next = NULL;

            __handle_auth_client(auth, auth_user);

            continue;
        } else {
            thread_mutex_unlock(&auth->lock);
        }
        thread_sleep (150000);
    }
    ICECAST_LOG_INFO("Authentication thread shutting down");
    return NULL;
}


/* Add a client.
 */
static void auth_add_client(auth_t *auth, client_t *client, void (*on_no_match)(client_t *client, void (*on_result)(client_t *client, void *userdata, auth_result result), void *userdata), void (*on_result)(client_t *client, void *userdata, auth_result result), void *userdata) {
    auth_client *auth_user;
    auth_matchtype_t matchtype;

    ICECAST_LOG_DEBUG("Trying to add client %p to auth %p's (role %s) queue.", client, auth, auth->role);

    /* TODO: replace that magic number */
    if (auth->pending_count > 100) {
        ICECAST_LOG_WARN("too many clients awaiting authentication on auth %p", auth);
        client_send_error_by_id(client, ICECAST_ERROR_AUTH_BUSY);
        return;
    }

    if (auth->filter_method[client->parser->req_type] == AUTH_MATCHTYPE_NOMATCH) {
        if (on_no_match) {
           on_no_match(client, on_result, userdata);
        } else if (on_result) {
           on_result(client, userdata, AUTH_NOMATCH);
        }
        return;
    }

    if (client->admin_command == ADMIN_COMMAND_ERROR) {
        /* this is a web/ client */
        matchtype = auth->filter_web_policy;
    } else {
        /* this is a admin/ client */
        size_t i;

        matchtype = AUTH_MATCHTYPE_UNUSED;

        for (i = 0; i < (sizeof(auth->filter_admin)/sizeof(*(auth->filter_admin))); i++) {
            if (auth->filter_admin[i].type != AUTH_MATCHTYPE_UNUSED && auth->filter_admin[i].command == client->admin_command) {
                matchtype = auth->filter_admin[i].type;
                break;
            }
        }

        if (matchtype == AUTH_MATCHTYPE_UNUSED)
            matchtype = auth->filter_admin_policy;
    }
    if (matchtype == AUTH_MATCHTYPE_NOMATCH) {
        if (on_no_match) {
           on_no_match(client, on_result, userdata);
        } else if (on_result) {
           on_result(client, userdata, AUTH_NOMATCH);
        }
        return;
    }

    auth_release(client->auth);
    auth_addref(client->auth = auth);
    auth_user = auth_client_setup(client);
    auth_user->process = auth_new_client;
    auth_user->on_no_match = on_no_match;
    auth_user->on_result = on_result;
    auth_user->userdata = userdata;
    ICECAST_LOG_DEBUG("adding client %p for authentication on %p", client, auth);
    queue_auth_client(auth_user);
}

static void __auth_on_result_destroy_client(client_t *client, void *userdata, auth_result result)
{
    (void)userdata, (void)result;

    client_destroy(client);
}

/* determine whether we need to process this client further. This
 * involves any auth exit, typically for external auth servers.
 */
int auth_release_client (client_t *client) {
    if (!client->acl)
        return 0;

    if (client->auth && client->auth->release_client) {
        auth_client *auth_user = auth_client_setup(client);
        auth_user->process = auth_remove_client;
        auth_user->on_result = __auth_on_result_destroy_client;
        queue_auth_client(auth_user);
        return 1;
    } else if (client->auth) {
        auth_release(client->auth);
        client->auth = NULL;
    }

    acl_release(client->acl);
    client->acl = NULL;
    return 0;
}


static int get_authenticator (auth_t *auth, config_options_t *options)
{
    if (auth->type == NULL)
    {
        ICECAST_LOG_WARN("no authentication type defined");
        return -1;
    }
    do
    {
        ICECAST_LOG_DEBUG("type is %s", auth->type);

        if (strcmp(auth->type, AUTH_TYPE_URL) == 0) {
#ifdef HAVE_CURL
            if (auth_get_url_auth(auth, options) < 0)
                return -1;
            break;
#else
            ICECAST_LOG_ERROR("Auth URL disabled");
            return -1;
#endif
        } else if (strcmp(auth->type, AUTH_TYPE_HTPASSWD) == 0) {
            if (auth_get_htpasswd_auth(auth, options) < 0)
                return -1;
            break;
        } else if (strcmp(auth->type, AUTH_TYPE_ANONYMOUS) == 0) {
            if (auth_get_anonymous_auth(auth, options) < 0)
                return -1;
            break;
        } else if (strcmp(auth->type, AUTH_TYPE_STATIC) == 0) {
            if (auth_get_static_auth(auth, options) < 0)
                return -1;
            break;
        } else if (strcmp(auth->type, AUTH_TYPE_LEGACY_PASSWORD) == 0) {
            if (auth_get_static_auth(auth, options) < 0)
                return -1;
            break;
        }

        ICECAST_LOG_ERROR("Unrecognised authenticator type: \"%s\"", auth->type);
        return -1;
    } while (0);

    return 0;
}


static inline void auth_get_authenticator__filter_admin(auth_t *auth, xmlNodePtr node, size_t *filter_admin_index, const char *name, auth_matchtype_t matchtype)
{
    char * tmp = (char*)xmlGetProp(node, XMLSTR(name));

    if (tmp) {
        char *cur = tmp;
        char *next;

        while (cur) {
            next = strstr(cur, ",");
            if (next) {
                *next = 0;
                next++;
                for (; *next == ' '; next++);
            }

            if (*filter_admin_index < (sizeof(auth->filter_admin)/sizeof(*(auth->filter_admin)))) {
                auth->filter_admin[*filter_admin_index].command = admin_get_command(cur);
                switch (auth->filter_admin[*filter_admin_index].command) {
                    case ADMIN_COMMAND_ERROR:
                        ICECAST_LOG_ERROR("Can not add unknown %s command to role.", name);
                    break;
                    case ADMIN_COMMAND_ANY:
                        auth->filter_admin_policy = matchtype;
                    break;
                    default:
                        auth->filter_admin[*filter_admin_index].type = matchtype;
                        (*filter_admin_index)++;
                    break;
                }
            } else {
                ICECAST_LOG_ERROR("Can not add more %s commands to role.", name);
            }

            cur = next;
        }

        free(tmp);
    }
}

static inline int auth_get_authenticator__filter_method(auth_t *auth, xmlNodePtr node, const char *name, auth_matchtype_t matchtype)
{
    char * tmp = (char*)xmlGetProp(node, XMLSTR(name));

    if (tmp) {
        char *cur = tmp;

        while (cur) {
            char *next = strstr(cur, ",");
            httpp_request_type_e idx;

            if (next) {
                *next = 0;
                next++;
                for (; *next == ' '; next++);
            }

            if (strcmp(cur, "*") == 0) {
                size_t i;

                for (i = 0; i < (sizeof(auth->filter_method)/sizeof(*(auth->filter_method))); i++)
                    auth->filter_method[i] = matchtype;
                break;
            }

            idx = httpp_str_to_method(cur);
            if (idx == httpp_req_unknown) {
                ICECAST_LOG_ERROR("Can not add known method \"%H\" to role's %s", cur, name);
                return -1;
            }
            auth->filter_method[idx] = matchtype;
            cur = next;
        }

        free(tmp);
    }

    return 0;
}

static inline int auth_get_authenticator__permission_alter(auth_t *auth, xmlNodePtr node, const char *name, auth_matchtype_t matchtype)
{
    char * tmp = (char*)xmlGetProp(node, XMLSTR(name));

    if (tmp) {
        char *cur = tmp;

        while (cur) {
            char *next = strstr(cur, ",");
            auth_alter_t idx;

            if (next) {
                *next = 0;
                next++;
                for (; *next == ' '; next++);
            }

            if (strcmp(cur, "*") == 0) {
                size_t i;

                for (i = 0; i < (sizeof(auth->permission_alter)/sizeof(*(auth->permission_alter))); i++)
                    auth->permission_alter[i] = matchtype;
                break;
            }

            idx = auth_str2alter(cur);
            if (idx == AUTH_ALTER_NOOP) {
                ICECAST_LOG_ERROR("Can not add unknown alter action \"%H\" to role's %s", cur, name);
                return -1;
            } else if (idx == AUTH_ALTER_REDIRECT) {
                auth->permission_alter[AUTH_ALTER_REDIRECT] = matchtype;
                auth->permission_alter[AUTH_ALTER_REDIRECT_SEE_OTHER] = matchtype;
                auth->permission_alter[AUTH_ALTER_REDIRECT_TEMPORARY] = matchtype;
                auth->permission_alter[AUTH_ALTER_REDIRECT_PERMANENT] = matchtype;
            } else {
                auth->permission_alter[idx] = matchtype;
            }

            cur = next;
        }

        free(tmp);
    }

    return 0;
}
auth_t *auth_get_authenticator(xmlNodePtr node)
{
    auth_t *auth = calloc(1, sizeof(auth_t));
    config_options_t *options = NULL, **next_option = &options;
    xmlNodePtr option;
    char *method;
    char *tmp;
    size_t i;
    size_t filter_admin_index = 0;

    if (auth == NULL)
        return NULL;

    thread_mutex_create(&auth->lock);
    auth->refcount = 1;
    auth->id = _next_auth_id();
    auth->type = (char*)xmlGetProp(node, XMLSTR("type"));
    auth->role = (char*)xmlGetProp(node, XMLSTR("name"));
    auth->management_url = (char*)xmlGetProp(node, XMLSTR("management-url"));
    auth->filter_web_policy = AUTH_MATCHTYPE_MATCH;
    auth->filter_admin_policy = AUTH_MATCHTYPE_MATCH;

    for (i = 0; i < (sizeof(auth->filter_admin)/sizeof(*(auth->filter_admin))); i++) {
        auth->filter_admin[i].type = AUTH_MATCHTYPE_UNUSED;
        auth->filter_admin[i].command = ADMIN_COMMAND_ERROR;
    }

    for (i = 0; i < (sizeof(auth->permission_alter)/sizeof(*(auth->permission_alter))); i++)
        auth->permission_alter[i] = AUTH_MATCHTYPE_NOMATCH;

    if (!auth->type) {
        auth_release(auth);
        return NULL;
    }

    method = (char*)xmlGetProp(node, XMLSTR("method"));
    if (method) {
        char *cur = method;
        char *next;

        for (i = 0; i < (sizeof(auth->filter_method)/sizeof(*auth->filter_method)); i++)
            auth->filter_method[i] = AUTH_MATCHTYPE_NOMATCH;

        while (cur) {
            httpp_request_type_e idx;

            next = strstr(cur, ",");
            if (next) {
                *next = 0;
                next++;
                for (; *next == ' '; next++);
            }

            if (strcmp(cur, "*") == 0) {
                for (i = 0; i < (sizeof(auth->filter_method)/sizeof(*auth->filter_method)); i++)
                    auth->filter_method[i] = AUTH_MATCHTYPE_MATCH;
                break;
            }

            idx = httpp_str_to_method(cur);
            if (idx == httpp_req_unknown) {
                auth_release(auth);
                return NULL;
            }
            auth->filter_method[idx] = AUTH_MATCHTYPE_MATCH;

            cur = next;
        }

        xmlFree(method);
    } else {
        for (i = 0; i < (sizeof(auth->filter_method)/sizeof(*auth->filter_method)); i++)
            auth->filter_method[i] = AUTH_MATCHTYPE_MATCH;
    }

    auth_get_authenticator__filter_method(auth, node, "match-method", AUTH_MATCHTYPE_MATCH);
    auth_get_authenticator__filter_method(auth, node, "nomatch-method", AUTH_MATCHTYPE_NOMATCH);

    tmp = (char*)xmlGetProp(node, XMLSTR("match-web"));
    if (tmp) {
        if (strcmp(tmp, "*") == 0) {
            auth->filter_web_policy = AUTH_MATCHTYPE_MATCH;
        } else {
            auth->filter_web_policy = AUTH_MATCHTYPE_NOMATCH;
        }
        free(tmp);
    }

    tmp = (char*)xmlGetProp(node, XMLSTR("nomatch-web"));
    if (tmp) {
        if (strcmp(tmp, "*") == 0) {
            auth->filter_web_policy = AUTH_MATCHTYPE_NOMATCH;
        } else {
            auth->filter_web_policy = AUTH_MATCHTYPE_MATCH;
        }
        free(tmp);
    }

    auth_get_authenticator__filter_admin(auth, node, &filter_admin_index, "match-admin", AUTH_MATCHTYPE_MATCH);
    auth_get_authenticator__filter_admin(auth, node, &filter_admin_index, "nomatch-admin", AUTH_MATCHTYPE_NOMATCH);

    auth_get_authenticator__permission_alter(auth, node, "may-alter", AUTH_MATCHTYPE_MATCH);
    auth_get_authenticator__permission_alter(auth, node, "may-not-alter", AUTH_MATCHTYPE_NOMATCH);

    /* BEFORE RELEASE 2.5.0 TODO: Migrate this to config_parse_options(). */
    option = node->xmlChildrenNode;
    while (option)
    {
        xmlNodePtr current = option;
        option = option->next;
        if (xmlStrcmp (current->name, XMLSTR("option")) == 0)
        {
            config_options_t *opt = calloc(1, sizeof (config_options_t));
            opt->name = (char *)xmlGetProp(current, XMLSTR("name"));
            if (opt->name == NULL)
            {
                free(opt);
                continue;
            }
            opt->value = (char *)xmlGetProp(current, XMLSTR("value"));
            if (opt->value == NULL)
            {
                xmlFree(opt->name);
                free(opt);
                continue;
            }
            *next_option = opt;
            next_option = &opt->next;
        }
        else
            if (xmlStrcmp (current->name, XMLSTR("text")) != 0)
                ICECAST_LOG_WARN("unknown auth setting (%s)", current->name);
    }
    auth->acl  = acl_new_from_xml_node(node);
    if (!auth->acl) {
        auth_release(auth);
        auth = NULL;
    } else {
        if (get_authenticator (auth, options) < 0) {
            auth_release(auth);
            auth = NULL;
        } else {
            auth->tailp = &auth->head;
            if (!auth->immediate) {
                auth->running = 1;
                auth->thread = thread_create("auth thread", auth_run_thread, auth, THREAD_ATTACHED);
            }
        }
    }

    while (options) {
        config_options_t *opt = options;
        options = opt->next;
        xmlFree(opt->name);
        xmlFree(opt->value);
        free (opt);
    }

    if (auth && !auth->management_url && (auth->adduser || auth->deleteuser || auth->listuser)) {
        char url[128];
        snprintf(url, sizeof(url), "/admin/manageauth.xsl?id=%lu", auth->id);
        auth->management_url = (char*)xmlCharStrdup(url);
    }

    return auth;
}

int auth_alter_client(auth_t *auth, auth_client *auth_user, auth_alter_t action, const char *arg)
{
    if (!auth || !auth_user || !arg)
        return -1;

    if (action < 0 || action >= (sizeof(auth->permission_alter)/sizeof(*(auth->permission_alter))))
        return -1;

    if (auth->permission_alter[action] != AUTH_MATCHTYPE_MATCH)
        return -1;

    if (replace_string(&(auth_user->alter_client_arg), arg) != 0)
        return -1;

    auth_user->alter_client_action = action;

    return 0;
}

auth_alter_t auth_str2alter(const char *str)
{
    if (!str)
        return AUTH_ALTER_NOOP;

    if (strcasecmp(str, "noop") == 0) {
        return AUTH_ALTER_NOOP;
    } else if (strcasecmp(str, "rewrite") == 0) {
        return AUTH_ALTER_REWRITE;
    } else if (strcasecmp(str, "redirect") == 0) {
        return AUTH_ALTER_REDIRECT;
    } else if (strcasecmp(str, "redirect_see_other") == 0) {
        return AUTH_ALTER_REDIRECT_SEE_OTHER;
    } else if (strcasecmp(str, "redirect_temporary") == 0) {
        return AUTH_ALTER_REDIRECT_TEMPORARY;
    } else if (strcasecmp(str, "redirect_permanent") == 0) {
        return AUTH_ALTER_REDIRECT_PERMANENT;
    } else if (strcasecmp(str, "send_error") == 0) {
        return AUTH_ALTER_SEND_ERROR;
    } else {
        return AUTH_ALTER_NOOP;
    }
}

/* these are called at server start and termination */

void auth_initialise (void)
{
    thread_mutex_create(&_auth_lock);
}

void auth_shutdown (void)
{
    ICECAST_LOG_INFO("Auth shutdown");
    thread_mutex_destroy(&_auth_lock);
}

/* authstack functions */

static void __move_client_forward_in_auth_stack(client_t *client, void (*on_result)(client_t *client, void *userdata, auth_result result), void *userdata) {
    auth_stack_next(&client->authstack);
    if (client->authstack) {
        auth_stack_add_client(client->authstack, client, on_result, userdata);
    } else {
        if (on_result)
            on_result(client, userdata, AUTH_NOMATCH);
    }
}

void          auth_stack_add_client(auth_stack_t *stack, client_t *client, void (*on_result)(client_t *client, void *userdata, auth_result result), void *userdata) {
    auth_t *auth;

    if (!stack || !client || (client->authstack && client->authstack != stack))
        return;

    if (!client->authstack)
        auth_stack_addref(stack);
    client->authstack = stack;
    auth = auth_stack_get(stack);
    auth_add_client(auth, client, __move_client_forward_in_auth_stack, on_result, userdata);
    auth_release(auth);
}

void          auth_stack_release(auth_stack_t *stack) {
    if (!stack)
        return;

    thread_mutex_lock(&stack->lock);
    stack->refcount--;
    thread_mutex_unlock(&stack->lock);

    if (stack->refcount)
        return;

    auth_release(stack->auth);
    auth_stack_release(stack->next);
    thread_mutex_destroy(&stack->lock);
    free(stack);
}

void          auth_stack_addref(auth_stack_t *stack) {
    if (!stack)
        return;
    thread_mutex_lock(&stack->lock);
    stack->refcount++;
    thread_mutex_unlock(&stack->lock);
}

int           auth_stack_next(auth_stack_t **stack) {
    auth_stack_t *next;
    if (!stack || !*stack)
        return -1;
    thread_mutex_lock(&(*stack)->lock);
    next = (*stack)->next;
    auth_stack_addref(next);
    thread_mutex_unlock(&(*stack)->lock);
    auth_stack_release(*stack);
    *stack = next;
    if (!next)
        return 1;
    return 0;
}

int           auth_stack_push(auth_stack_t **stack, auth_t *auth) {
    auth_stack_t *next;

    if (!stack || !auth)
        return -1;

    next = calloc(1, sizeof(*next));
    if (!next) {
        return -1;
    }
    thread_mutex_create(&next->lock);
    next->refcount = 1;
    next->auth = auth;
    auth_addref(auth);

    if (*stack) {
        auth_stack_append(*stack, next);
        auth_stack_release(next);
        return 0;
    } else {
        *stack = next;
        return 0;
    }
}

int           auth_stack_append(auth_stack_t *stack, auth_stack_t *tail) {
    auth_stack_t *next, *cur;

    if (!stack)
        return -1;

    auth_stack_addref(cur = stack);
    thread_mutex_lock(&cur->lock);
    while (1) {
        next = cur->next;
        if (!cur->next)
            break;

        auth_stack_addref(next);
        thread_mutex_unlock(&cur->lock);
        auth_stack_release(cur);
        cur = next;
        thread_mutex_lock(&cur->lock);
    }

    auth_stack_addref(cur->next = tail);
    thread_mutex_unlock(&cur->lock);
    auth_stack_release(cur);

    return 0;
}

auth_t       *auth_stack_get(auth_stack_t *stack) {
    auth_t *auth;

    if (!stack)
        return NULL;

    thread_mutex_lock(&stack->lock);
    auth_addref(auth = stack->auth);
    thread_mutex_unlock(&stack->lock);
    return auth;
}

auth_t       *auth_stack_getbyid(auth_stack_t *stack, unsigned long id) {
    auth_t *ret = NULL;

    if (!stack)
        return NULL;

    auth_stack_addref(stack);

    while (!ret && stack) {
        auth_t *auth = auth_stack_get(stack);
        if (auth->id == id) {
            ret = auth;
            break;
        }
        auth_release(auth);
        auth_stack_next(&stack);
    }

    if (stack)
        auth_stack_release(stack);

    return ret;

}

acl_t        *auth_stack_get_anonymous_acl(auth_stack_t *stack, httpp_request_type_e method) {
    acl_t *ret = NULL;

    if (!stack || method < 0 || method > httpp_req_unknown)
        return NULL;

    auth_stack_addref(stack);

    while (!ret && stack) {
        auth_t *auth = auth_stack_get(stack);
        if (auth->filter_method[method] != AUTH_MATCHTYPE_NOMATCH && strcmp(auth->type, AUTH_TYPE_ANONYMOUS) == 0) {
            acl_addref(ret = auth->acl);
        }
        auth_release(auth);

        if (!ret)
            auth_stack_next(&stack);
    }

    if (stack)
        auth_stack_release(stack);

    return ret;
}
