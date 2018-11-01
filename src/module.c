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

#include <igloo/thread.h>
#include <igloo/avl.h>

#include "refobject.h"
#include "module.h"
#include "cfgfile.h" /* for XMLSTR() */

struct module_tag {
    refobject_base_t __base;
    igloo_mutex_t lock;
    const module_client_handler_t *client_handlers;
    size_t client_handlers_len;
    module_setup_handler_t freecb;
    void *userdata;
    char *management_link_url;
    char *management_link_title;
};


struct module_container_tag {
    refobject_base_t __base;
    igloo_mutex_t lock;
    igloo_avl_tree *module;
};

static int compare_refobject_t_name(void *arg, void *a, void *b)
{
    return strcmp(refobject_get_name(a), refobject_get_name(b));
}

static void __module_container_free(refobject_t self, void **userdata)
{
    module_container_t *cont = REFOBJECT_TO_TYPE(self, module_container_t *);
    igloo_thread_mutex_destroy(&(cont->lock));
    igloo_avl_tree_free(cont->module, (igloo_avl_free_key_fun_type)refobject_unref);
}

int __module_container_new(refobject_t self, const refobject_type_t *type, va_list ap)
{
    module_container_t *ret = REFOBJECT_TO_TYPE(self, module_container_t*);

    igloo_thread_mutex_create(&(ret->lock));

    ret->module = igloo_avl_tree_new(compare_refobject_t_name, NULL);

    return 0;
}

REFOBJECT_DEFINE_TYPE(module_container_t,
        REFOBJECT_DEFINE_TYPE_FREE(__module_container_free),
        REFOBJECT_DEFINE_TYPE_NEW(__module_container_new)
        );

int                     module_container_add_module(module_container_t *self, module_t *module)
{
    if (!self)
        return -1;

    if (refobject_ref(module) != 0)
        return -1;

    igloo_thread_mutex_lock(&(self->lock));
    igloo_avl_insert(self->module, module);
    igloo_thread_mutex_unlock(&(self->lock));

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

    igloo_thread_mutex_lock(&(self->lock));
    igloo_avl_delete(self->module, module, (igloo_avl_free_key_fun_type)refobject_unref);
    igloo_thread_mutex_unlock(&(self->lock));

    refobject_unref(module);

    return 0;
}

module_t *              module_container_get_module(module_container_t *self, const char *name)
{
    refobject_base_t *search;
    module_t *ret;

    if (!self || !name)
        return NULL;

    search = refobject_new__new(refobject_base_t, NULL, name, NULL);

    igloo_thread_mutex_lock(&(self->lock));
    if (igloo_avl_get_by_key(self->module, REFOBJECT_TO_TYPE(search, void *), (void**)&ret) != 0) {
        ret = NULL;
    }
    igloo_thread_mutex_unlock(&(self->lock));

    refobject_unref(search);
    refobject_ref(ret);

    return ret;
}

xmlNodePtr                      module_container_get_modulelist_as_xml(module_container_t *self)
{
    xmlNodePtr root;
    igloo_avl_node *avlnode;

    if (!self)
        return NULL;

    root = xmlNewNode(NULL, XMLSTR("modules"));
    if (!root)
        return NULL;

    igloo_thread_mutex_lock(&(self->lock));
    avlnode = igloo_avl_get_first(self->module);
    while (avlnode) {
        module_t *module = avlnode->key;
        xmlNodePtr node = xmlNewChild(root, NULL, XMLSTR("module"), NULL);

        xmlSetProp(node, XMLSTR("name"), XMLSTR(refobject_get_name(module)));
        if (module->management_link_url)
            xmlSetProp(node, XMLSTR("management-url"), XMLSTR(module->management_link_url));
        if (module->management_link_title)
            xmlSetProp(node, XMLSTR("management-title"), XMLSTR(module->management_link_title));

        avlnode = igloo_avl_get_next(avlnode);
    }
    igloo_thread_mutex_unlock(&(self->lock));

    return root;
}

static void __module_free(refobject_t self, void **userdata)
{
    module_t *mod = REFOBJECT_TO_TYPE(self, module_t *);

    if (mod->freecb)
        mod->freecb(mod, &(mod->userdata));

    if (mod->userdata)
        free(mod->userdata);

    free(mod->management_link_url);
    free(mod->management_link_title);

    igloo_thread_mutex_destroy(&(mod->lock));
}

REFOBJECT_DEFINE_TYPE(module_t,
        REFOBJECT_DEFINE_TYPE_FREE(__module_free)
        );

module_t *              module_new(const char *name, module_setup_handler_t newcb, module_setup_handler_t freecb, void *userdata)
{
    module_t *ret = refobject_new__new(module_t, NULL, name, NULL);

    if (!ret)
        return NULL;

    igloo_thread_mutex_create(&(ret->lock));

    ret->userdata = userdata;
    ret->freecb = freecb;

    if (newcb) {
        if (newcb(ret, &(ret->userdata)) != 0) {
            refobject_unref(ret);
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

    igloo_thread_mutex_lock(&(self->lock));
    free(self->management_link_url);
    free(self->management_link_title);

    self->management_link_url = n_url;
    self->management_link_title = n_title;
    igloo_thread_mutex_unlock(&(self->lock));

    return 0;
}

const module_client_handler_t * module_get_client_handler(module_t *self, const char *name)
{
    size_t i;

    if (!self || !name)
        return NULL;

    igloo_thread_mutex_lock(&(self->lock));
    for (i = 0; i < self->client_handlers_len; i++) {
        if (self->client_handlers[i].name && strcmp(self->client_handlers[i].name, name) == 0) {
            igloo_thread_mutex_unlock(&(self->lock));
            return &(self->client_handlers[i]);
        }
    }
    igloo_thread_mutex_unlock(&(self->lock));

    return NULL;
}

int                             module_add_client_handler(module_t *self, const module_client_handler_t *handlers, size_t len)
{
    if (!self)
        return -1;

    igloo_thread_mutex_lock(&(self->lock));
    self->client_handlers = handlers;
    self->client_handlers_len = len;
    igloo_thread_mutex_unlock(&(self->lock));

    return 0;
}
