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
 * Copyright 2011,      Dave 'justdave' Miller <justdave@mozilla.com>,
 * Copyright 2011-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "icecasttypes.h"
#include <igloo/ro.h>

#include "connection_handle.h"
#include "auth.h"
#include "acl.h"
#include "admin.h"
#include "global.h"
#include "fastevent.h"
#include "listensocket.h"
#include "source.h"
#include "errors.h"
#include "stats.h"
#include "fserve.h"

#include "logging.h"
#define CATMODULE "connection-handle"

/* Handle <resource> lookups here.
 */

static bool _handle_resources(client_t *client, char **uri)
{
    const char *http_host = httpp_getvar(client->parser, "host");
    char *serverhost = NULL;
    int   serverport = 0;
    char *vhost = NULL;
    char *vhost_colon;
    char *new_uri = NULL;
    ice_config_t *config;
    const listener_t *listen_sock;
    resource_t *resource;

    if (http_host) {
        vhost = strdup(http_host);
        if (vhost) {
            /* TODO: This may not work correctly for IPv6 addresses. */
            vhost_colon = strstr(vhost, ":");
            if (vhost_colon)
                *vhost_colon = 0;
        }
    }

    config = config_get_config();
    listen_sock = listensocket_get_listener(client->con->listensocket_effective);
    if (listen_sock) {
        serverhost = listen_sock->bind_address;
        serverport = listen_sock->port;
    }

    resource = config->resources;

    /* We now go thru all resources and see if any matches. */
    for (; resource; resource = resource->next) {
        /* We check for several aspects, if they DO NOT match, we continue with our search. */

        /* Check for the URI to match. */
        if (resource->flags & ALIAS_FLAG_PREFIXMATCH) {
            size_t len = strlen(resource->source);
            if (strncmp(*uri, resource->source, len) != 0)
                continue;
            ICECAST_LOG_DEBUG("Match: *uri='%s', resource->source='%s', len=%zu", *uri, resource->source, len);
        } else {
            if (strcmp(*uri, resource->source) != 0)
                continue;
        }

        /* Check for the server's port to match. */
        if (resource->port != -1 && resource->port != serverport)
            continue;

        /* Check for the server's bind address to match. */
        if (resource->bind_address != NULL && serverhost != NULL && strcmp(resource->bind_address, serverhost) != 0)
            continue;

        if (resource->listen_socket != NULL && (listen_sock->id == NULL || strcmp(resource->listen_socket, listen_sock->id) != 0))
            continue;

        /* Check for the vhost to match. */
        if (resource->vhost != NULL && vhost != NULL && strcmp(resource->vhost, vhost) != 0)
            continue;

        /* Ok, we found a matching entry. */

        if (resource->destination) {
            if (resource->flags & ALIAS_FLAG_PREFIXMATCH) {
                size_t len = strlen(resource->source);
                asprintf(&new_uri, "%s%s", resource->destination, (*uri) + len);
            } else {
                new_uri = strdup(resource->destination);
            }
        }
        if (resource->omode != OMODE_DEFAULT)
            client->mode = resource->omode;

        if (resource->module) {
            module_t *module = module_container_get_module(global.modulecontainer, resource->module);

            if (module != NULL) {
                igloo_ro_unref(&(client->handler_module));
                client->handler_module = module;
            } else {
                ICECAST_LOG_ERROR("Module used in alias not found: %s", resource->module);
            }
        }

        if (resource->handler) {
            char *func = strdup(resource->handler);
            if (func) {
                free(client->handler_function);
                client->handler_function = func;
            } else {
                ICECAST_LOG_ERROR("Can not allocate memory.");
            }
        }

        ICECAST_LOG_DEBUG("resource has made %s into %s", *uri, new_uri);
        break;
    }

    listensocket_release_listener(client->con->listensocket_effective);
    config_release_config();

    if (new_uri) {
        free(*uri);
        *uri = new_uri;
    }

    if (vhost)
        free(vhost);

    return true;
}


static void __prepare_shoutcast_admin_cgi_request(client_t *client)
{
    ice_config_t *config;
    const char *sc_mount;
    const char *pass = httpp_get_query_param(client->parser, "pass");
    const listener_t *listener;

    if (pass == NULL) {
        ICECAST_LOG_ERROR("missing pass parameter");
        return;
    }

    if (client->password) {
        ICECAST_LOG_INFO("Client already has password set");
        return;
    }

    /* Why do we acquire a global lock here? -- ph3-der-loewe, 2018-05-11 */
    global_lock();
    config = config_get_config();
    sc_mount = config->shoutcast_mount;

    listener = listensocket_get_listener(client->con->listensocket_effective);
    if (listener && listener->shoutcast_mount)
        sc_mount = listener->shoutcast_mount;

    httpp_set_query_param(client->parser, "mount", sc_mount);
    listensocket_release_listener(client->con->listensocket_effective);

    httpp_setvar(client->parser, HTTPP_VAR_PROTOCOL, "ICY");
    client->password = strdup(pass);
    config_release_config();
    global_unlock();
}

/* Updates client's admin_command */
static bool _update_admin_command(client_t *client)
{
    if (strcmp(client->uri, "/admin.cgi") == 0) {
        client->admin_command = admin_get_command(client->uri + 1);
        __prepare_shoutcast_admin_cgi_request(client);
        if (!client->password) {
            client_send_error_by_id(client, ICECAST_ERROR_CON_MISSING_PASS_PARAMETER);
            return false;
        }
    } else if (strncmp(client->uri, "/admin/", 7) == 0) {
        client->admin_command = admin_get_command(client->uri + 7);
    }

    return true;
}

static inline void source_startup(client_t *client)
{
    source_t *source;
    source = source_reserve(client->uri);

    if (source) {
        source->client = client;
        source->parser = client->parser;
        source->con = client->con;
        if (connection_complete_source(source, 1) < 0) {
            source_clear_source(source);
            source_free_source(source);
            return;
        }
        client->respcode = 200;
        if (client->protocol == ICECAST_PROTOCOL_SHOUTCAST) {
            client->respcode = 200;
            /* send this non-blocking but if there is only a partial write
             * then leave to header timeout */
            client_send_bytes(client, "OK2\r\nicy-caps:11\r\n\r\n", 20); /* TODO: Replace Magic Number! */
            source->shoutcast_compat = 1;
            source_client_callback(client, source);
        } else {
            refbuf_t *ok = refbuf_new(PER_CLIENT_REFBUF_SIZE);
            const char *expectcontinue;
            const char *transfer_encoding;
            int status_to_send = 0;
            ssize_t ret;

            transfer_encoding = httpp_getvar(source->parser, "transfer-encoding");
            if (transfer_encoding && strcasecmp(transfer_encoding, HTTPP_ENCODING_IDENTITY) != 0) {
                client->encoding = httpp_encoding_new(transfer_encoding);
                if (!client->encoding) {
                    client_send_error_by_id(client, ICECAST_ERROR_CON_UNIMPLEMENTED);
                    return;
                }
            }

            if (source->parser && source->parser->req_type == httpp_req_source) {
                status_to_send = 200;
            } else {
                /* For PUT support we check for 100-continue and send back a 100 to stay in spec */
                expectcontinue = httpp_getvar (source->parser, "expect");

                if (expectcontinue != NULL) {
                    if (util_strcasestr(expectcontinue, "100-continue") != NULL) {
                        status_to_send = 100;
                    }
                }
            }

            client->respcode = 200;
            if (status_to_send) {
                ret = util_http_build_header(ok->data, PER_CLIENT_REFBUF_SIZE, 0, 0, status_to_send, NULL, NULL, NULL, NULL, NULL, client);
                snprintf(ok->data + ret, PER_CLIENT_REFBUF_SIZE - ret, "Content-Length: 0\r\n\r\n");
                ok->len = strlen(ok->data);
            } else {
                ok->len = 0;
            }
            refbuf_release(client->refbuf);
            client->refbuf = ok;
            fserve_add_client_callback(client, source_client_callback, source);
        }
    } else {
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNT_IN_USE);
        ICECAST_LOG_WARN("Mountpoint %#H in use", client->uri);
    }
}

/* only called for native icecast source clients */
static void _handle_source_request(client_t *client)
{
    const char *method = httpp_getvar(client->parser, HTTPP_VAR_REQ_TYPE);

    ICECAST_LOG_INFO("Source logging in at mountpoint \"%s\" using %s%H%s from %s as role %s with acl %s",
        client->uri,
        ((method) ? "\"" : "<"), ((method) ? method : "unknown"), ((method) ? "\"" : ">"),
        client->con->ip, client->role, acl_get_name(client->acl));

    if (client->parser && client->parser->req_type == httpp_req_source) {
        ICECAST_LOG_DEBUG("Source at mountpoint \"%s\" connected using deprecated SOURCE method.", client->uri);
    }

    if (client->uri[0] != '/') {
        ICECAST_LOG_WARN("source mountpoint not starting with /");
        client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNTPOINT_NOT_STARTING_WITH_SLASH);
        return;
    }

    source_startup(client);
}


static void _handle_stats_request(client_t *client)
{
    stats_event_inc(NULL, "stats_connections");

    client->respcode = 200;
    snprintf (client->refbuf->data, PER_CLIENT_REFBUF_SIZE,
        "HTTP/1.0 200 OK\r\n\r\n");
    client->refbuf->len = strlen(client->refbuf->data);
    fserve_add_client_callback(client, stats_callback, NULL);
}

/* if 0 is returned then the client should not be touched, however if -1
 * is returned then the caller is responsible for handling the client
 */
static void __add_listener_to_source(source_t *source, client_t *client)
{
    size_t loop = 10;

    do {
        ICECAST_LOG_DEBUG("max on %s is %ld (cur %lu)", source->mount,
            source->max_listeners, source->listeners);
        if (source->max_listeners == -1)
            break;
        if (source->listeners < (unsigned long)source->max_listeners)
            break;

        if (loop && source->fallback_when_full && source->fallback_mount) {
            source_t *next = source_find_mount (source->fallback_mount);
            if (!next) {
                ICECAST_LOG_ERROR("Fallback '%s' for full source '%s' not found",
                    source->mount, source->fallback_mount);
                client_send_error_by_id(client, ICECAST_ERROR_SOURCE_MAX_LISTENERS);
                return;
            }
            ICECAST_LOG_INFO("stream full, trying %s", next->mount);
            source = next;
            navigation_history_navigate_to(&(client->history), source->identifier, NAVIGATION_DIRECTION_DOWN);
            loop--;
            continue;
        }
        /* now we fail the client */
        client_send_error_by_id(client, ICECAST_ERROR_SOURCE_MAX_LISTENERS);
        return;
    } while (1);

    client->write_to_client = format_generic_write_to_client;
    client->check_buffer = format_check_http_buffer;
    client->refbuf->len = PER_CLIENT_REFBUF_SIZE;
    memset(client->refbuf->data, 0, PER_CLIENT_REFBUF_SIZE);

    /* lets add the client to the active list */
    avl_tree_wlock(source->pending_tree);
    avl_insert(source->pending_tree, client);
    avl_tree_unlock(source->pending_tree);

    if (source->running == 0 && source->on_demand) {
        /* enable on-demand relay to start, wake up the slave thread */
        ICECAST_LOG_DEBUG("kicking off on-demand relay");
        source->on_demand_req = 1;
    }
    ICECAST_LOG_DEBUG("Added client to %s", source->mount);
}

/* count the number of clients on a mount with same username and same role as the given one */
static inline ssize_t __count_user_role_on_mount (source_t *source, client_t *client) {
    ssize_t ret = 0;
    avl_node *node;

    avl_tree_rlock(source->client_tree);
    node = avl_get_first(source->client_tree);
    while (node) {
        client_t *existing_client = (client_t *)node->key;
        if (existing_client->username && client->username &&
            strcmp(existing_client->username, client->username) == 0 &&
            existing_client->role && client->role &&
            strcmp(existing_client->role, client->role) == 0) {
            ret++;
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->client_tree);

    avl_tree_rlock(source->pending_tree);
    node = avl_get_first(source->pending_tree);
    while (node) {
        client_t *existing_client = (client_t *)node->key;
        if (existing_client->username && client->username &&
            strcmp(existing_client->username, client->username) == 0 &&
            existing_client->role && client->role &&
            strcmp(existing_client->role, client->role) == 0){
            ret++;
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->pending_tree);
    return ret;
}

static void _handle_get_request(client_t *client) {
    source_t *source = NULL;

    ICECAST_LOG_DEBUG("Got client %p with URI %H", client, client->uri);

    /* there are several types of HTTP GET clients
     * media clients, which are looking for a source (eg, URI = /stream.ogg),
     * stats clients, which are looking for /admin/stats.xml and
     * fserve clients, which are looking for static files.
     */

    stats_event_inc(NULL, "client_connections");

    /* this is a web/ request. let's check if we are allowed to do that. */
    if (acl_test_web(client->acl) != ACL_POLICY_ALLOW) {
        /* doesn't seem so, sad client :( */
        auth_reject_client_on_deny(client);
        return;
    }

    if (client->parser->req_type == httpp_req_options) {
        client_send_204(client);
        return;
    }

    if (util_check_valid_extension(client->uri) == XSLT_CONTENT) {
        /* If the file exists, then transform it, otherwise, write a 404 */
        ICECAST_LOG_DEBUG("Stats request, sending XSL transformed stats");
        stats_transform_xslt(client);
        return;
    }

    avl_tree_rlock(global.source_tree);
    /* let's see if this is a source or just a random fserve file */
    source = source_find_mount_with_history(client->uri, &(client->history));
    if (source) {
        /* true mount */
        do {
            ssize_t max_connections_per_user = acl_get_max_connections_per_user(client->acl);
            /* check for duplicate_logins */
            if (max_connections_per_user > 0) { /* -1 = not set (-> default=unlimited), 0 = unlimited */
                if (max_connections_per_user <= __count_user_role_on_mount(source, client)) {
                    client_send_error_by_id(client, ICECAST_ERROR_CON_PER_CRED_CLIENT_LIMIT);
                    break;
                }
            }

            if (!source->allow_direct_access) {
                client_send_error_by_id(client, ICECAST_ERROR_CON_MOUNT_NO_FOR_DIRECT_ACCESS);
                break;
            }

            /* Set max listening duration in case not already set. */
            if (client->con->discon_time == 0) {
                time_t connection_duration = acl_get_max_connection_duration(client->acl);
                if (connection_duration == -1) {
                    ice_config_t *config = config_get_config();
                    mount_proxy *mount = config_find_mount(config, source->mount, MOUNT_TYPE_NORMAL);
                    if (mount && mount->max_listener_duration)
                        connection_duration = mount->max_listener_duration;
                    config_release_config();
                }

                if (connection_duration > 0) /* -1 = not set (-> default=unlimited), 0 = unlimited */
                    client->con->discon_time = connection_duration + time(NULL);
            }

            __add_listener_to_source(source, client);
        } while (0);
        avl_tree_unlock(global.source_tree);
    } else {
        /* file */
        avl_tree_unlock(global.source_tree);
        fserve_client_create(client);
    }
}

static void _handle_delete_request(client_t *client) {
    source_t *source;

    avl_tree_wlock(global.source_tree);
    source = source_find_mount_raw(client->uri);
    if (source) {
        source->running = 0;
        avl_tree_unlock(global.source_tree);
        client_send_204(client);
    } else {
        avl_tree_unlock(global.source_tree);
        client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
    }
}

static void _handle_admin_request(client_t *client, char *adminuri)
{
    ICECAST_LOG_DEBUG("Client %p requesting admin interface.", client);

    stats_event_inc(NULL, "client_connections");

    admin_handle_request(client, adminuri);
}

/* Handle any client that passed the authing process.
 */
static void _handle_authed_client(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    /* Update admin parameters just in case auth changed our URI */
    if (!_update_admin_command(client))
        return;

    fastevent_emit(FASTEVENT_TYPE_CLIENT_AUTHED, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);

    if (result != AUTH_OK) {
        auth_reject_client_on_fail(client);
        return;
    }

    if (acl_test_method(client->acl, client->parser->req_type) != ACL_POLICY_ALLOW) {
        ICECAST_LOG_ERROR("Client (role=%s, acl=%s, username=%s) not allowed to use this request method on %H", client->role, acl_get_name(client->acl), client->username, client->uri);
        auth_reject_client_on_deny(client);
        return;
    }

    /* Dispatch legacy admin.cgi requests */
    if (strcmp(client->uri, "/admin.cgi") == 0) {
        _handle_admin_request(client, client->uri + 1);
        return;
    } /* Dispatch all admin requests */
    else if (strncmp(client->uri, "/admin/", 7) == 0) {
        _handle_admin_request(client, client->uri + 7);
        return;
    }

    if (client->handler_module && client->handler_function) {
        const module_client_handler_t *handler = module_get_client_handler(client->handler_module, client->handler_function);
        if (handler) {
            handler->cb(client->handler_module, client);
            return;
        } else {
            ICECAST_LOG_ERROR("No such handler function in module: %s", client->handler_function);
        }
    }

    switch (client->parser->req_type) {
        case httpp_req_source:
        case httpp_req_put:
            _handle_source_request(client);
        break;
        case httpp_req_stats:
            _handle_stats_request(client);
        break;
        case httpp_req_get:
        case httpp_req_post:
        case httpp_req_options:
            _handle_get_request(client);
        break;
        case httpp_req_delete:
            _handle_delete_request(client);
        break;
        default:
            ICECAST_LOG_ERROR("Wrong request type from client");
            client_send_error_by_id(client, ICECAST_ERROR_CON_UNKNOWN_REQUEST);
        break;
    }
}

/* Handle clients that still need to authenticate.
 */

static void _handle_authentication_global(client_t *client, void *userdata, auth_result result)
{
    ice_config_t *config;
    auth_stack_t *authstack;

    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        /* Allow global admins access to all mount points */
        !(result == AUTH_OK && client->admin_command != ADMIN_COMMAND_ERROR && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying global authenticators for client %p.", client);
    config = config_get_config();
    authstack = config->authstack;
    auth_stack_addref(authstack);
    config_release_config();
    auth_stack_add_client(authstack, client, _handle_authed_client, userdata);
    auth_stack_release(authstack);
}

static inline mount_proxy * __find_non_admin_mount(ice_config_t *config, const char *name, mount_type type)
{
    if (strcmp(name, "/admin.cgi") == 0 || strncmp(name, "/admin/", 7) == 0)
        return NULL;

    return config_find_mount(config, name, type);
}

static void _handle_authentication_mount_generic(client_t *client, void *userdata, mount_type type, void (*callback)(client_t*, void*, auth_result))
{
    ice_config_t *config;
    mount_proxy *mountproxy;
    auth_stack_t *stack = NULL;

    config = config_get_config();
    mountproxy = __find_non_admin_mount(config, client->uri, type);
    if (!mountproxy) {
        int command_type = admin_get_command_type(client->admin_command);
        if (command_type == ADMINTYPE_MOUNT || command_type == ADMINTYPE_HYBRID) {
            const char *mount = httpp_get_param(client->parser, "mount");
            if (mount)
                mountproxy = __find_non_admin_mount(config, mount, type);
        }
    }
    if (mountproxy && mountproxy->mounttype == type)
        stack = mountproxy->authstack;
    auth_stack_addref(stack);
    config_release_config();

    if (stack) {
        auth_stack_add_client(stack, client, callback, userdata);
        auth_stack_release(stack);
    } else {
        callback(client, userdata, AUTH_NOMATCH);
    }
}

static void _handle_authentication_mount_default(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH &&
        /* Allow global admins access to all mount points */
        !(result == AUTH_OK && client->admin_command != ADMIN_COMMAND_ERROR && acl_test_admin(client->acl, client->admin_command) == ACL_POLICY_DENY)) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying <mount type=\"default\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, userdata, MOUNT_TYPE_DEFAULT, _handle_authentication_global);
}

static void _handle_authentication_mount_normal(client_t *client, void *userdata, auth_result result)
{
    auth_stack_release(client->authstack);
    client->authstack = NULL;

    if (result != AUTH_NOMATCH) {
        _handle_authed_client(client, userdata, result);
        return;
    }

    ICECAST_LOG_DEBUG("Trying <mount type=\"normal\"> specific authenticators for client %p.", client);
    _handle_authentication_mount_generic(client, userdata, MOUNT_TYPE_NORMAL, _handle_authentication_mount_default);
}

static void _handle_authentication_listen_socket(client_t *client)
{
    auth_stack_t *stack = NULL;
    const listener_t *listener;

    listener = listensocket_get_listener(client->con->listensocket_effective);
    if (listener) {
        if (listener->authstack) {
            auth_stack_addref(stack = listener->authstack);
        }
        listensocket_release_listener(client->con->listensocket_effective);
    }

    if (stack) {
        auth_stack_add_client(stack, client, _handle_authentication_mount_normal, NULL);
        auth_stack_release(stack);
    } else {
        _handle_authentication_mount_normal(client, NULL, AUTH_NOMATCH);
    }
}

static void _handle_authentication(client_t *client)
{
    fastevent_emit(FASTEVENT_TYPE_CLIENT_READY_FOR_AUTH, FASTEVENT_FLAG_MODIFICATION_ALLOWED, FASTEVENT_DATATYPE_CLIENT, client);
    _handle_authentication_listen_socket(client);
}

void connection_handle_client(client_t *client)
{
    http_parser_t *parser = client->parser;
    const char *rawuri = httpp_getvar(parser, HTTPP_VAR_URI);
    const char *upgrade, *connection;
    char *uri;

    if (strcmp("ICE",  httpp_getvar(parser, HTTPP_VAR_PROTOCOL)) &&
        strcmp("HTTP", httpp_getvar(parser, HTTPP_VAR_PROTOCOL))) {
        ICECAST_LOG_ERROR("Bad HTTP protocol detected");
        client_destroy(client);
        return;
    }

    upgrade = httpp_getvar(parser, "upgrade");
    connection = httpp_getvar(parser, "connection");
    if (upgrade && connection && strcasecmp(connection, "upgrade") == 0) {
        if (client->con->tlsmode == ICECAST_TLSMODE_DISABLED || client->con->tls || strstr(upgrade, "TLS/1.0") == NULL) {
            client_send_error_by_id(client, ICECAST_ERROR_CON_UPGRADE_ERROR);
            return;
        } else {
            client_send_101(client, ICECAST_REUSE_UPGRADETLS);
            return;
        }
    } else if (client->con->tlsmode != ICECAST_TLSMODE_DISABLED && client->con->tlsmode != ICECAST_TLSMODE_AUTO && !client->con->tls) {
        client_send_426(client, ICECAST_REUSE_UPGRADETLS);
        return;
    }

    if (parser->req_type == httpp_req_options && strcmp(rawuri, "*") == 0) {
        client->uri = strdup("*");
        client_send_204(client);
        return;
    }

    uri = util_normalise_uri(rawuri);

    if (!uri) {
        client_destroy(client);
        return;
    }

    client->mode = config_str_to_omode(NULL, NULL, httpp_get_param(client->parser, "omode"));

    if (!_handle_resources(client, &uri)) {
        client_destroy(client);
        return;
    }

    client->uri = uri;

    if (!_update_admin_command(client))
        return;

    _handle_authentication(client);
}
