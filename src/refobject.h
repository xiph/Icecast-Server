/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __REFOBJECT_H__
#define __REFOBJECT_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "common/thread/thread.h"

#include "icecasttypes.h"
#include "compat.h"

#ifdef HAVE_TYPE_ATTRIBUTE_TRANSPARENT_UNION
#define REFOBJECT_NULL          ((refobject_t)(refobject_base_t*)NULL)
#define REFOBJECT_IS_NULL(x)    (((refobject_t)(x)).refobject_base == NULL)
#define REFOBJECT_TO_TYPE(x,y)  ((y)(((refobject_t)(x)).refobject_base))
#else
#define REFOBJECT_NULL          NULL
#define REFOBJECT_IS_NULL(x)    ((x) == NULL)
#define REFOBJECT_TO_TYPE(x,y)  ((y)(x))
#endif

typedef void (*refobject_free_t)(refobject_t self, void **userdata);

struct refobject_base_tag {
    size_t refc;
    mutex_t lock;
    void *userdata;
    refobject_free_t freecb;
    char *name;
    refobject_t associated;
};

refobject_t     refobject_new(size_t len, refobject_free_t freecb, void *userdata, const char *name, refobject_t associated);
int             refobject_ref(refobject_t self);
int             refobject_unref(refobject_t self);
void *          refobject_get_userdata(refobject_t self);
int             refobject_set_userdata(refobject_t self, void *userdata);
const char *    refobject_get_name(refobject_t self);
refobject_t     refobject_get_associated(refobject_t self);

#endif
