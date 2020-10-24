/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "common/avl/avl.h"

#include "navigation.h"

#include "logging.h"
#define CATMODULE "navigation"

struct mount_identifier_tag {
    /* base object */
    refobject_base_t __base;
};

REFOBJECT_DEFINE_TYPE(mount_identifier_t);

static int mount_identifier_compare(void *compare_arg, void *a, void *b)
{
    const char *id_a, *id_b;

    id_a = refobject_get_name(a);
    id_b = refobject_get_name(b);

    if (!id_a || !id_b || id_a == id_b) {
        return 0;
    } else {
        return strcmp(id_a, id_b);
    }
}

void navigation_initialize(void)
{
}

void navigation_shutdown(void)
{
}


mount_identifier_t * mount_identifier_new(const char *mount)
{
    mount_identifier_t *n;

    if (!mount)
        return NULL;

    n = refobject_new_ext(mount_identifier_t, NULL, mount, NULL);
    if (!n)
        return NULL;

    return n;
}
