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

#include "common/thread/thread.h"

#include "refobject.h"

#define TO_BASE(x) REFOBJECT_TO_TYPE((x), refobject_base_t *)

refobject_t     refobject_new(size_t len, refobject_free_t freecb, void *userdata, const char *name, refobject_t parent)
{
    refobject_base_t *ret = NULL;

    if (len < sizeof(refobject_base_t))
        return (refobject_t)ret;

    ret = calloc(1, len);
    if (ret == NULL)
        return (refobject_t)ret;

    ret->refc = 1;
    ret->freecb = freecb;
    ret->userdata = userdata;

    thread_mutex_create(&(ret->lock));

    if (name) {
        ret->name = strdup(name);
        if (!ret->name) {
            refobject_unref(ret);
            return REFOBJECT_NULL;
        }
    }

    if (!REFOBJECT_IS_NULL(parent)) {
        if (refobject_ref(parent) != 0) {
            refobject_unref(ret);
            return REFOBJECT_NULL;
        }

        ret->parent = parent;
    }

    return (refobject_t)ret;
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

    if (base->freecb)
        base->freecb(self, &(base->userdata));

    if (base->userdata)
        free(base->userdata);

    if (base->name)
        free(base->name);

    thread_mutex_unlock(&(base->lock));
    thread_mutex_destroy(&(base->lock));

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

refobject_t     refobject_get_parent(refobject_t self)
{
    refobject_t ret;

    if (REFOBJECT_IS_NULL(self))
        return REFOBJECT_NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->parent;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}
