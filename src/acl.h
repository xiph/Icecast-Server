/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp Schafft <lion@lion.leolix.org>
 */

#ifndef __ACL_H__
#define __ACL_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "httpp/httpp.h"

typedef enum acl_policy_tag {
 /* Error on function call */
 ACL_POLICY_ERROR  = -1,
 /* Client is allowed to do operation, go ahead! */
 ACL_POLICY_ALLOW = 0,
 /* Client is not allowed to do so, send error! */
 ACL_POLICY_DENY  = 1
} acl_policy_t;

struct acl_tag;
typedef struct acl_tag acl_t;

/* basic functions to work with ACLs */
acl_t * acl_new(void);
acl_t * acl_new_from_xml_node(xmlNodePtr node);

void acl_addref(acl_t * acl);
void acl_release(acl_t * acl);

/* special functions */
int acl_set_ANY_str(acl_t * acl, acl_policy_t policy, const char * str, int (*callback)(acl_t *, acl_policy_t, const char *));

/* HTTP Method specific functions */
int acl_set_method_str__callback(acl_t * acl, acl_policy_t policy, const char * str);
#define acl_set_method_str(acl,policy,str) acl_set_ANY_str((acl), (policy), (str), acl_set_method_str__callback)
acl_policy_t acl_test_method(acl_t * acl, httpp_request_type_e method);

/* admin/ interface specific functions */
int acl_set_admin_str__callbck(acl_t * acl, acl_policy_t policy, const char * str);
#define acl_set_admin_str(acl,policy,str) acl_set_ANY_str((acl), (policy), (str), acl_set_admin_str__callbck)
acl_policy_t acl_test_admin(acl_t * acl, int command);

/* web/ interface specific functions */
int acl_set_web_policy(acl_t * acl, acl_policy_t policy);
acl_policy_t acl_test_web(acl_t * acl);

/* mount specific functons */
int acl_set_max_connection_duration(acl_t * acl, time_t duration);
time_t acl_get_max_connection_duration(acl_t * acl);
int acl_set_max_connections_per_user(acl_t * acl, size_t limit);
ssize_t acl_get_max_connections_per_user(acl_t * acl);

#endif
