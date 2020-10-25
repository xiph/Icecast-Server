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

#include "common/avl/avl.h"

#include "navigation.h"

#include "logging.h"
#define CATMODULE "navigation"

struct mount_identifier_tag {
    /* base object */
    refobject_base_t __base;
};

REFOBJECT_DEFINE_TYPE(mount_identifier_t);

static inline const char * navigation_direction_id2str(navigation_direction_t dir)
{
    switch (dir) {
        case NAVIGATION_DIRECTION_UP: return "up"; break;
        case NAVIGATION_DIRECTION_DOWN: return "down"; break;
        case NAVIGATION_DIRECTION_REPLACE_CURRENT: return "replace-current"; break;
        case NAVIGATION_DIRECTION_REPLACE_ALL: return "replace-all"; break;
    }

    return NULL;
}

static int mount_identifier_compare__for_tree(void *compare_arg, void *a, void *b)
{
    const char *id_a, *id_b;

    id_a = mount_identifier_get_mount(a);
    id_b = mount_identifier_get_mount(b);

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

    n = refobject_new__new(mount_identifier_t, NULL, mount, NULL);
    if (!n)
        return NULL;

    return n;
}

int                     mount_identifier_compare(mount_identifier_t *a, mount_identifier_t *b)
{
    return mount_identifier_compare__for_tree(NULL, a, b);
}

static inline int navigation_history_pop(navigation_history_t *history)
{
    if (history->fill == 0)
        return 0;
    history->fill--;
    refobject_unref(history->history[history->fill]);
    history->history[history->fill] = NULL;
    return 1;
}

static inline int navigation_history_push(navigation_history_t *history, mount_identifier_t *identifier)
{
    if (history->fill > 0 && mount_identifier_compare(history->history[history->fill - 1], identifier) == 0)
        return 0;

    if (refobject_ref(identifier) != 0)
        return -1;

    if (history->fill == (sizeof(history->history)/sizeof(*history->history))) {
        refobject_unref(history->history[0]);
        memmove(history->history, &(history->history[1]), sizeof(history->history) - sizeof(*history->history));
        history->fill--;
    }

    history->history[history->fill++] = identifier;

    return 0;
}

void                    navigation_history_clear(navigation_history_t *history)
{
    if (!history)
        return;
    while (navigation_history_pop(history));
}

mount_identifier_t *    navigation_history_get_up(navigation_history_t *history)
{
    if (!history)
        return NULL;

    if (history->fill < 2)
        return NULL;

    if (refobject_ref(history->history[history->fill - 2]) != 0)
        return NULL;

    return history->history[history->fill - 2];
}

int                     navigation_history_navigate_to(navigation_history_t *history, mount_identifier_t *identifier, navigation_direction_t direction)
{
    ICECAST_LOG_DDEBUG("Called with history=%p, identifier=%p (%#H), direction=%s", history, identifier, mount_identifier_get_mount(identifier), navigation_direction_id2str(direction));

    if (!history || !identifier)
        return -1;

    if (direction == NAVIGATION_DIRECTION_UP && history->fill < 2)
        direction = NAVIGATION_DIRECTION_REPLACE_ALL;

    switch (direction) {
        case NAVIGATION_DIRECTION_UP:
            if (history->fill < 2)
                return -1;
            if (mount_identifier_compare(history->history[history->fill - 2], identifier) != 0)
                return -1;
            return navigation_history_pop(history);
        break;
        case NAVIGATION_DIRECTION_DOWN:
            return navigation_history_push(history, identifier);
        break;
        case NAVIGATION_DIRECTION_REPLACE_CURRENT:
            if (history->fill == 0) {
                return navigation_history_push(history, identifier);
            } else {
                if (history->fill > 1 && mount_identifier_compare(history->history[history->fill - 2], identifier) == 0) {
                    return navigation_history_pop(history);
                }

                if (refobject_ref(identifier) != 0)
                    return -1;
                refobject_unref(history->history[history->fill - 1]);
                history->history[history->fill - 1] = identifier;
                return 0;
            }
        break;
        case NAVIGATION_DIRECTION_REPLACE_ALL:
            navigation_history_clear(history);
            if (history->fill != 0)
                return -1;
            return navigation_history_push(history, identifier);
        break;
    }

    return -1;
}
