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

#include "refobject.h"

#define TO_BASE(x) REFOBJECT_TO_TYPE((x), refobject_base_t *)

int refobject_new__return_zero(refobject_t self, const refobject_type_t *type, va_list ap)
{
    (void)self, (void)type, (void)ap;
    return 0;
}

REFOBJECT_DEFINE_TYPE(refobject_base_t,
        REFOBJECT_DEFINE_TYPE_NEW_NOOP()
        );

static inline int check_type(const refobject_type_t *type)
{
    return type->control_length == sizeof(refobject_type_t) && type->control_version == REFOBJECT_CONTROL_VERSION &&
        type->type_length >= sizeof(refobject_base_t);
}

refobject_t     refobject_new__real(const refobject_type_t *type, void *userdata, const char *name, refobject_t associated)
{
    refobject_base_t *ret = NULL;

    if (!check_type(type))
        return (refobject_t)ret;

    ret = calloc(1, type->type_length);
    if (ret == NULL)
        return (refobject_t)ret;

    ret->type = type;
    ret->refc = 1;
    ret->userdata = userdata;

    thread_mutex_create(&(ret->lock));

    if (name) {
        ret->name = strdup(name);
        if (!ret->name) {
            refobject_unref(ret);
            return REFOBJECT_NULL;
        }
    }

    if (!REFOBJECT_IS_NULL(associated)) {
        if (refobject_ref(associated) != 0) {
            refobject_unref(ret);
            return REFOBJECT_NULL;
        }

        ret->associated = associated;
    }

    return (refobject_t)ret;
}

refobject_t     refobject_new__simple(const refobject_type_t *type, void *userdata, const char *name, refobject_t associated, ...)
{
    refobject_t ret;
    int res;
    va_list ap;

    if (!check_type(type))
        return REFOBJECT_NULL;

    if (!type->type_newcb)
        return REFOBJECT_NULL;

    ret = refobject_new__real(type, userdata, name, associated);
    if (REFOBJECT_IS_NULL(ret))
        return REFOBJECT_NULL;

    va_start(ap, associated);
    res = type->type_newcb(ret, type, ap);
    va_end(ap);

    if (res != 0) {
        refobject_unref(ret);
        return REFOBJECT_NULL;
    }

    return ret;
}

int             refobject_ref(refobject_t self)
{
    if (REFOBJECT_IS_NULL(self))
        return -1;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    TO_BASE(self)->refc++;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return 0;
}

int             refobject_unref(refobject_t self)
{
    register refobject_base_t *base = TO_BASE(self);

    if (REFOBJECT_IS_NULL(self))
        return -1;

    thread_mutex_lock(&(base->lock));
    base->refc--;
    if (base->refc) {
        thread_mutex_unlock(&(base->lock));
        return 0;
    }

    if (base->type->type_freecb)
        base->type->type_freecb(self, &(base->userdata));

    if (base->userdata)
        free(base->userdata);

    if (base->name)
        free(base->name);

    thread_mutex_unlock(&(base->lock));
    igloo_thread_mutex_destroy(&(base->lock));

    free(base);

    return 0;
}

void *          refobject_get_userdata(refobject_t self)
{
    void *ret;

    if (REFOBJECT_IS_NULL(self))
        return NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->userdata;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}

int             refobject_set_userdata(refobject_t self, void *userdata)
{
    if (REFOBJECT_IS_NULL(self))
        return -1;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    TO_BASE(self)->userdata = userdata;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return 0;
}

const char *    refobject_get_name(refobject_t self)
{
    const char *ret;

    if (REFOBJECT_IS_NULL(self))
        return NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->name;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}

refobject_t     refobject_get_associated(refobject_t self)
{
    refobject_t ret;

    if (REFOBJECT_IS_NULL(self))
        return REFOBJECT_NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->associated;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}
