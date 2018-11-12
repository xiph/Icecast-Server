/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include "acl.h"
#include "admin.h"
#include "util.h"

#include <stdio.h>

#define MAX_ADMIN_COMMANDS 32

/* define internal structure */
struct acl_tag {
    /* reference counter */
    size_t refcount;

    /* allowed methods */
    acl_policy_t method[httpp_req_unknown+1];

    /* admin/ interface */
    struct {
        admin_command_id_t command;
        acl_policy_t policy;
    } admin_commands[MAX_ADMIN_COMMANDS];
    size_t admin_commands_len;
    acl_policy_t admin_command_policy;

    /* web/ interface */
    acl_policy_t web_policy;

    /* mount specific functons */
    time_t max_connection_duration;
    size_t max_connections_per_user;

    /* HTTP headers to send to clients using this role */
    ice_config_http_header_t *http_headers;
};

/* some string util functions */
static inline void __skip_spaces(const char **str)
{
 register const char * p;

 for (p = *str; *p == ' '; p++);
 *str = p;
}

int acl_set_ANY_str(acl_t           *acl,
                    acl_policy_t    policy,
                    const char      *str,
                    int (*callback)(acl_t *, acl_policy_t, const char *))
{
    const char *end;
    size_t len;
    char buf[64];
    int ret;

    if ( !acl || !str || !callback || (policy != ACL_POLICY_ALLOW && policy != ACL_POLICY_DENY) )
        return -1;

    do {
        __skip_spaces(&str);
        end = strstr(str, ",");
        if (end) {
            len = end - str;
        } else {
            len = strlen(str);
        }

        if (len > (sizeof(buf) - 1))
            return -1;
        memcpy(buf, str, len);
        buf[len] = 0;

        ret = callback(acl, policy, buf);
        if (ret)
            return ret;

        str += len + 1;
    } while (end);

    return 0;
}

/* basic functions to work with ACLs */
acl_t *acl_new(void)
{
    acl_t * ret = calloc(1, sizeof(*ret));
    if (!ret)
        return NULL;

    ret->refcount = 1;

    acl_set_method_str(ret, ACL_POLICY_DENY, "*");
    acl_set_method_str(ret, ACL_POLICY_ALLOW, "get,options");

    acl_set_admin_str(ret, ACL_POLICY_DENY, "*");
    acl_set_admin_str(ret, ACL_POLICY_ALLOW, "buildm3u");

    acl_set_web_policy(ret, ACL_POLICY_ALLOW);

    acl_set_max_connection_duration(ret, -1);
    acl_set_max_connections_per_user(ret, 0);

    return ret;
}

acl_t *acl_new_from_xml_node(xmlNodePtr node)
{
    acl_t * ret;
    char * tmp;
    xmlAttrPtr prop;

    if (!node)
        return NULL;

    ret = acl_new();
    if (!ret)
        return NULL;

    prop = node->properties;
    while (prop) {
        tmp = (char*)xmlGetProp(node, prop->name);
        if (tmp) {
            /* basic {allow|deny}-* options */
            if (strcmp((const char*)prop->name, "allow-method") == 0) {
                acl_set_method_str(ret, ACL_POLICY_ALLOW, tmp);
            } else if (strcmp((const char*)prop->name, "deny-method") == 0) {
                acl_set_method_str(ret, ACL_POLICY_DENY, tmp);
            } else if (strcmp((const char*)prop->name, "allow-admin") == 0) {
                acl_set_admin_str(ret, ACL_POLICY_ALLOW, tmp);
            } else if (strcmp((const char*)prop->name, "deny-admin") == 0) {
                acl_set_admin_str(ret, ACL_POLICY_DENY, tmp);
            } else if (strcmp((const char*)prop->name, "allow-web") == 0) {
                if (strstr(tmp, "*") || util_str_to_bool(tmp)) {
                    acl_set_web_policy(ret, ACL_POLICY_ALLOW);
                } else {
                    acl_set_web_policy(ret, ACL_POLICY_DENY);
                }
            } else if (strcmp((const char*)prop->name, "deny-web") == 0) {
                if (strstr(tmp, "*") || util_str_to_bool(tmp)) {
                    acl_set_web_policy(ret, ACL_POLICY_DENY);
                } else {
                    acl_set_web_policy(ret, ACL_POLICY_ALLOW);
                }

            /* wildcard {allow,deny} option */
            } else if (strcmp((const char*)prop->name, "allow-all") == 0) {
                if (strstr(tmp, "*") || util_str_to_bool(tmp)) {
                    acl_set_method_str(ret, ACL_POLICY_ALLOW, "*");
                    acl_set_admin_str(ret,  ACL_POLICY_ALLOW, "*");
                    acl_set_web_policy(ret, ACL_POLICY_ALLOW);
                } else {
                    acl_set_method_str(ret, ACL_POLICY_DENY, "*");
                    acl_set_admin_str(ret,  ACL_POLICY_DENY, "*");
                    acl_set_web_policy(ret, ACL_POLICY_DENY);
                }
            } else if (strcmp((const char*)prop->name, "deny-all") == 0) {
                if (strstr(tmp, "*") || util_str_to_bool(tmp)) {
                    acl_set_method_str(ret, ACL_POLICY_DENY, "*");
                    acl_set_admin_str(ret,  ACL_POLICY_DENY, "*");
                    acl_set_web_policy(ret, ACL_POLICY_DENY);
                } else {
                    acl_set_method_str(ret, ACL_POLICY_ALLOW, "*");
                    acl_set_admin_str(ret,  ACL_POLICY_ALLOW, "*");
                    acl_set_web_policy(ret, ACL_POLICY_ALLOW);
                }

            /* other options */
            } else if (strcmp((const char*)prop->name, "connections-per-user") == 0) {
                if (strcmp(tmp, "*") == 0 || strcmp(tmp, "unlimited") == 0) {
                    acl_set_max_connections_per_user(ret, 0);
                } else {
                    acl_set_max_connections_per_user(ret, atoi(tmp));
                }
            } else if (strcmp((const char*)prop->name, "connection-duration") == 0) {
                if (strcmp(tmp, "*") == 0 || strcmp(tmp, "unlimited") == 0) {
                    acl_set_max_connection_duration(ret, 0);
                } else {
                    acl_set_max_connection_duration(ret, atoi(tmp));
                }
            }
            xmlFree(tmp);
        }
        prop = prop->next;
    }

    /* if we're new style configured try to read child nodes */
    if (xmlStrcmp(node->name, XMLSTR("acl")) == 0) {
        xmlNodePtr child = node->xmlChildrenNode;
        do {
            if (child == NULL)
                break;
            if (xmlIsBlankNode(child))
                continue;
            if (xmlStrcmp(child->name, XMLSTR("http-headers")) == 0) {
                config_parse_http_headers(child->xmlChildrenNode, &(ret->http_headers));
            }
        } while ((child = child->next));
    }

    return ret;
}

void acl_addref(acl_t * acl)
{
    if (!acl)
        return;

    acl->refcount++;
}

void acl_release(acl_t * acl)
{
    if (!acl)
        return;

    acl->refcount--;
    if (acl->refcount)
        return;

    config_clear_http_header(acl->http_headers);

    free(acl);
}

/* HTTP Method specific functions */
int acl_set_method_str__callback(acl_t          *acl,
                                 acl_policy_t   policy,
                                 const char     *str)
{
    httpp_request_type_e method;
    size_t i;

    if (strcmp(str, "*") == 0) {
        for (i = 0; i < (sizeof(acl->method)/sizeof(*acl->method)); i++)
            acl->method[i] = policy;
    } else {
        method = httpp_str_to_method(str);
        if (method == httpp_req_unknown)
            return -1;

        acl->method[method] = policy;
    }

    return 0;
}

acl_policy_t acl_test_method(acl_t * acl, httpp_request_type_e method)
{
    if (!acl || method < httpp_req_none || method > httpp_req_unknown)
        return ACL_POLICY_ERROR;

    return acl->method[method];
}

/* admin/ interface specific functions */
int acl_set_admin_str__callbck(acl_t        *acl,
                               acl_policy_t policy,
                               const char   *str)
{
    size_t read_i, write_i;
    admin_command_id_t command = admin_get_command(str);

   if (command == ADMIN_COMMAND_ERROR)
       return -1;

   if (command == ADMIN_COMMAND_ANY) {
       acl->admin_command_policy = policy;
       for (read_i = write_i = 0; read_i < acl->admin_commands_len; read_i++) {
        if (acl->admin_commands[read_i].policy == policy)
            continue;
        acl->admin_commands[write_i] = acl->admin_commands[read_i];
        write_i++; /* no need to check bounds here as this loop can only compress the array */
       }
       acl->admin_commands_len = write_i;
       return 0;
   }

   if (acl->admin_commands_len == MAX_ADMIN_COMMANDS)
       return -1;

   acl->admin_commands[acl->admin_commands_len].command = command;
   acl->admin_commands[acl->admin_commands_len].policy  = policy;
   acl->admin_commands_len++;
   return 0;
}

acl_policy_t acl_test_admin(acl_t *acl, admin_command_id_t command)
{
    size_t i;

    if (!acl)
        return ACL_POLICY_ERROR;

    for (i = 0; i < acl->admin_commands_len; i++)
        if (acl->admin_commands[i].command == command)
            return acl->admin_commands[i].policy;

    return acl->admin_command_policy;
}

/* web/ interface specific functions */
int acl_set_web_policy(acl_t *acl, acl_policy_t policy)
{
    if (!acl || (policy != ACL_POLICY_ALLOW && policy != ACL_POLICY_DENY))
        return -1;

    acl->web_policy = policy;

    return 0;
}

acl_policy_t acl_test_web(acl_t *acl)
{
    if (!acl)
        return ACL_POLICY_ERROR;

    return acl->web_policy;
}

/* mount specific functons */
int acl_set_max_connection_duration(acl_t *acl, time_t duration)
{
    if (!acl)
        return -1;

    acl->max_connection_duration = duration;

    return 0;
}

time_t acl_get_max_connection_duration(acl_t *acl)
{
    if (!acl)
        return -1;

    return acl->max_connection_duration;
}

int acl_set_max_connections_per_user(acl_t *acl, size_t limit)
{
    if (!acl)
        return -1;

    acl->max_connections_per_user = limit;

    return 0;
}

ssize_t acl_get_max_connections_per_user(acl_t *acl)
{
    if (!acl)
        return -1;

    return acl->max_connections_per_user;
}

const ice_config_http_header_t *acl_get_http_headers(acl_t * acl)
{
    if (!acl)
        return NULL;

    return acl->http_headers;
}
