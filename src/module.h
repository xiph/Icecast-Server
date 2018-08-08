/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __MODULE_H__
#define __MODULE_H__

#include <libxml/tree.h>

#include "icecasttypes.h"

typedef void (*module_client_handler_function_t)(module_t *self, client_t *client, const char *uri);
typedef int  (*module_setup_handler_t)(module_t *self, void **userdata);

typedef struct {
    const char *name;
    module_client_handler_function_t cb;
} module_client_handler_t;

module_container_t *            module_container_new(void);
int                             module_container_add_module(module_container_t *self, module_t *module);
int                             module_container_delete_module(module_container_t *self, const char *name);
module_t *                      module_container_get_module(module_container_t *self, const char *name);
xmlNodePtr                      module_container_get_modulelist_as_xml(module_container_t *self);

module_t *                      module_new(const char *name, module_setup_handler_t newcb, module_setup_handler_t freecb, void *userdata);

int                             module_add_link(module_t *self, const char *type, const char *url, const char *title);

/* Note: Those functions are not really thread safe as (module_client_handler_t) is not thread safe. This is by design. */
const module_client_handler_t * module_get_client_handler(module_t *self, const char *name);
int                             module_add_client_handler(module_t *self, const module_client_handler_t *handlers, size_t len);

#endif
