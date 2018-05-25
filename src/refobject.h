/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __REFOBJECT_H__
#define __REFOBJECT_H__

#include "common/thread/thread.h"

#include "icecasttypes.h"
#include "compat.h"

typedef void (*refobject_free)(refobject_t self, void **userdata);

struct refobject_base_tag {
    size_t refc;
    mutex_t lock;
    void *userdata;
    refobject_free freecb;
};

refobject_t     refobject_new(size_t len, refobject_free freecb, void *userdata);
int             refobject_ref(refobject_t self);
int             refobject_unref(refobject_t self);
void *          refobject_get_userdata(refobject_t self);
int             refobject_set_userdata(refobject_t self, void *userdata);

#endif
