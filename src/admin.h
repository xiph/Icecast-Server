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
 * Copyright 2011-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __ADMIN_H__
#define __ADMIN_H__

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "icecasttypes.h"
#include "compat.h"
#include "resourcematch.h"

/* types */
#define ADMINTYPE_ERROR   (-1)
#define ADMINTYPE_GENERAL   1
#define ADMINTYPE_MOUNT     2
#define ADMINTYPE_HYBRID    (ADMINTYPE_GENERAL|ADMINTYPE_MOUNT)

/* special commands */
#define ADMIN_COMMAND_ERROR ((admin_command_id_t)(-1))
#define ADMIN_COMMAND_ANY   ((admin_command_id_t)0) /* for ACL framework */

typedef void (*admin_request_function_ptr)(client_t * client, source_t * source, admin_format_t format);
typedef void (*admin_request_function_with_parameters_ptr)(client_t * client, source_t * source, admin_format_t format, resourcematch_extract_t *parameters);

typedef struct admin_command_handler {
    const char                         *route;
    const int                           type;
    const int                           format;
    const admin_request_function_ptr    function;
    const admin_request_function_with_parameters_ptr function_with_parameters;
} admin_command_handler_t;

void admin_handle_request(client_t *client, const char *uri);

void admin_send_response(xmlDocPtr       doc,
                         client_t       *client,
                         admin_format_t  response,
                         const char     *xslt_template);

void admin_add_listeners_to_mount(source_t       *source,
                                  xmlNodePtr      parent,
                                  operation_mode  mode);

xmlNodePtr admin_add_role_to_authentication(auth_t *auth, xmlNodePtr parent);

admin_command_id_t admin_get_command(const char *command);
int admin_get_command_type(admin_command_id_t command);

/* Register and unregister admin commands below /admin/$prefix/.
 * All parameters must be kept in memory as long as the registration is valid as there will be no copy made.
 */
int admin_command_table_register(const char *prefix, size_t handlers_length, const admin_command_handler_t *handlers);
int admin_command_table_unregister(const char *prefix);

#endif  /* __ADMIN_H__ */
