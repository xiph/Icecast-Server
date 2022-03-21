/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "icecasttypes.h"
#include <igloo/ro.h>
#include <igloo/error.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"

#include "module.h"
#include "global.h"  /* for igloo_instance */
#include "cfgfile.h" /* for XMLSTR() */

struct module_tag {
    igloo_ro_full_t __parent;
    mutex_t lock;
    char *name;
    const module_client_handler_t *client_handlers;
    size_t client_handlers_len;
    module_setup_handler_t freecb;
    void *userdata;
    char *management_link_url;
    char *management_link_title;
};


struct module_container_tag {
    igloo_ro_full_t __parent;
    mutex_t lock;
    avl_tree *module;
};

static int compare_refobject_t_name(void *arg, void *a, void *b)
{
    return strcmp(((module_t*)a)->name, ((module_t*)b)->name);
}

static int free_ro(void * key)
{
    igloo_ro_stub_t *stub = igloo_ro_to_type(key, igloo_ro_stub_t);
    igloo_ro_unref(&stub);
    return 0;
}

static void __module_container_free(igloo_ro_t self)
{
    module_container_t *cont = igloo_ro_to_type(self, module_container_t);
    thread_mutex_destroy(&(cont->lock));
    avl_tree_free(cont->module, free_ro);
}

static igloo_error_t __module_container_new(igloo_ro_t self, const igloo_ro_type_t *type, va_list ap)
{
    module_container_t *ret = igloo_ro_to_type(self, module_container_t);

    thread_mutex_create(&(ret->lock));

    ret->module = avl_tree_new(compare_refobject_t_name, NULL);

    return igloo_ERROR_NONE;
}

igloo_RO_PUBLIC_TYPE(module_container_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(__module_container_free),
        igloo_RO_TYPEDECL_NEW(__module_container_new)
        );

int                     module_container_add_module(module_container_t *self, module_t *module)
{
    module_t *ref;

    if (!self)
        return -1;

    if (igloo_ro_ref(module, &ref, module_t) != igloo_ERROR_NONE)
        return -1;

    thread_mutex_lock(&(self->lock));
    avl_insert(self->module, ref);
    thread_mutex_unlock(&(self->lock));

    return 0;
}

int                     module_container_delete_module(module_container_t *self, const char *name)
{
    module_t *module;

    if (!self || !name)
        return -1;

    module = module_container_get_module(self, name);
    if (!module)
        return -1;

    thread_mutex_lock(&(self->lock));
    avl_delete(self->module, module, free_ro);
    thread_mutex_unlock(&(self->lock));

    igloo_ro_unref(&module);

    return 0;
}

module_t *              module_container_get_module(module_container_t *self, const char *name)
{
    module_t *search;
    module_t *result;
    module_t *ret = NULL;

    if (!self || !name)
        return NULL;

    search = module_new(name, NULL, NULL, NULL);

    thread_mutex_lock(&(self->lock));
    if (avl_get_by_key(self->module, (void*)search, (void**)&result) != 0) {
        ret = NULL;
    }
    thread_mutex_unlock(&(self->lock));

    igloo_ro_unref(&search);
    igloo_ro_ref(result, &ret, module_t);

    return ret;
}

xmlNodePtr                      module_container_get_modulelist_as_xml(module_container_t *self)
{
    xmlNodePtr root;
    avl_node *avlnode;

    if (!self)
        return NULL;

    root = xmlNewNode(NULL, XMLSTR("modules"));
    if (!root)
        return NULL;

    thread_mutex_lock(&(self->lock));
    avlnode = avl_get_first(self->module);
    while (avlnode) {
        module_t *module = avlnode->key;
        xmlNodePtr node = xmlNewChild(root, NULL, XMLSTR("module"), NULL);

        xmlSetProp(node, XMLSTR("name"), XMLSTR(module->name));
        if (module->management_link_url)
            xmlSetProp(node, XMLSTR("management-url"), XMLSTR(module->management_link_url));
        if (module->management_link_title)
            xmlSetProp(node, XMLSTR("management-title"), XMLSTR(module->management_link_title));

        avlnode = avl_get_next(avlnode);
    }
    thread_mutex_unlock(&(self->lock));

    return root;
}

static void __module_free(igloo_ro_t self)
{
    module_t *mod = igloo_ro_to_type(self, module_t);

    if (mod->freecb)
        mod->freecb(mod, &(mod->userdata));

    if (mod->userdata)
        free(mod->userdata);

    free(mod->management_link_url);
    free(mod->management_link_title);
    free(mod->name);

    thread_mutex_destroy(&(mod->lock));
}

igloo_RO_PUBLIC_TYPE(module_t, igloo_ro_full_t,
        igloo_RO_TYPEDECL_FREE(__module_free)
        );

module_t *              module_new(const char *name, module_setup_handler_t newcb, module_setup_handler_t freecb, void *userdata)
{
    module_t *ret;

    if (igloo_ro_new_raw(&ret, module_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    ret->name = strdup(name);

    if (!ret->name) {
        igloo_ro_unref(&ret);
        return NULL;
    }

    thread_mutex_create(&(ret->lock));

    ret->userdata = userdata;
    ret->freecb = freecb;

    if (newcb) {
        if (newcb(ret, &(ret->userdata)) != 0) {
            igloo_ro_unref(&ret);
            return NULL;
        }
    }

    return ret;
}

int                             module_add_link(module_t *self, const char *type, const char *url, const char *title)
{
    char *n_url = NULL;
    char *n_title = NULL;

    if (!self || !type)
        return -1;

    if (strcmp(type, "management-url") != 0)
        return -1;

    if (url) {
        n_url = strdup(url);
        if (!n_url)
            return -1;
    }

    if (title) {
        n_title = strdup(title);
        if (!n_title) {
            free(n_url);
            return -1;
        }
    }

    thread_mutex_lock(&(self->lock));
    free(self->management_link_url);
    free(self->management_link_title);

    self->management_link_url = n_url;
    self->management_link_title = n_title;
    thread_mutex_unlock(&(self->lock));

    return 0;
}

const module_client_handler_t * module_get_client_handler(module_t *self, const char *name)
{
    size_t i;

    if (!self || !name)
        return NULL;

    thread_mutex_lock(&(self->lock));
    for (i = 0; i < self->client_handlers_len; i++) {
        if (self->client_handlers[i].name && strcmp(self->client_handlers[i].name, name) == 0) {
            thread_mutex_unlock(&(self->lock));
            return &(self->client_handlers[i]);
        }
    }
    thread_mutex_unlock(&(self->lock));

    return NULL;
}

int                             module_add_client_handler(module_t *self, const module_client_handler_t *handlers, size_t len)
{
    if (!self)
        return -1;

    thread_mutex_lock(&(self->lock));
    self->client_handlers = handlers;
    self->client_handlers_len = len;
    thread_mutex_unlock(&(self->lock));

    return 0;
}
