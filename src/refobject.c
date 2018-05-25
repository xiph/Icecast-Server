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

#include "common/thread/thread.h"

#include "refobject.h"

#ifdef HAVE_TYPE_ATTRIBUTE_TRANSPARENT_UNION
#define TO_BASE(x) ((x).refobject_base)
#else
#define TO_BASE(x) ((refobject_base_t*)(x))
#endif

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
            return (refobject_t)(refobject_base_t*)NULL;
        }
    }

    if (TO_BASE(parent) != NULL) {
        if (refobject_ref(parent) != 0) {
            refobject_unref(ret);
            return (refobject_t)(refobject_base_t*)NULL;
        }

        ret->parent = parent;
    }

    return (refobject_t)ret;
}

int             refobject_ref(refobject_t self)
{
    if (TO_BASE(self) == NULL)
        return -1;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    TO_BASE(self)->refc++;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return 0;
}

int             refobject_unref(refobject_t self)
{
    register refobject_base_t *base = TO_BASE(self);

    if (base == NULL)
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

    return 0;
}

void *          refobject_get_userdata(refobject_t self)
{
    void *ret;

    if (TO_BASE(self) == NULL)
        return NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->userdata;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}

int             refobject_set_userdata(refobject_t self, void *userdata)
{
    if (TO_BASE(self) == NULL)
        return -1;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    TO_BASE(self)->userdata = userdata;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return 0;
}

const char *    refobject_get_name(refobject_t self)
{
    const char *ret;

    if (TO_BASE(self) == NULL)
        return NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->name;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}

refobject_t     refobject_get_parent(refobject_t self)
{
    refobject_t ret;

    if (TO_BASE(self) == NULL)
        return (refobject_t)(refobject_base_t*)NULL;

    thread_mutex_lock(&(TO_BASE(self)->lock));
    ret = TO_BASE(self)->parent;
    thread_mutex_unlock(&(TO_BASE(self)->lock));

    return ret;
}
