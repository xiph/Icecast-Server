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

#include "icecasttypes.h"

#include <igloo/ro.h>
#include <igloo/sp.h>
#include <igloo/error.h>

#include "navigation.h"
#include "global.h"

#include "logging.h"
#define CATMODULE "navigation"

struct mount_identifier_tag {
    /* base object */
    igloo_ro_tiny_t __parent;
    const char *mount;
};

static void mount_identifier_free(igloo_ro_t self)
{
    mount_identifier_t *identifier = igloo_ro_to_type(self, mount_identifier_t);
    if (igloo_sp_unref(&(identifier->mount), igloo_instance) != igloo_ERROR_NONE) {
        ICECAST_LOG_ERROR("igloo_sp_unref() for identifier=%p's mount failed. BUG.");
    }
}

igloo_RO_PUBLIC_TYPE(mount_identifier_t, igloo_ro_tiny_t,
        igloo_RO_TYPEDECL_FREE(mount_identifier_free)
        );

const char * navigation_direction_to_str(navigation_direction_t dir)
{
    switch (dir) {
        case NAVIGATION_DIRECTION_UP: return "up"; break;
        case NAVIGATION_DIRECTION_DOWN: return "down"; break;
        case NAVIGATION_DIRECTION_REPLACE_CURRENT: return "replace-current"; break;
        case NAVIGATION_DIRECTION_REPLACE_ALL: return "replace-all"; break;
    }

    return NULL;
}

navigation_direction_t navigation_str_to_direction(const char *str, navigation_direction_t def)
{
    if (!str || !*str)
        return def;

    if (strcasecmp(str, "up") == 0) {
        return NAVIGATION_DIRECTION_UP;
    } else if (strcasecmp(str, "down") == 0) {
        return NAVIGATION_DIRECTION_DOWN;
    } else if (strcasecmp(str, "replace_current") == 0 || strcasecmp(str, "replace-current") == 0) {
        return NAVIGATION_DIRECTION_REPLACE_CURRENT;
    } else if (strcasecmp(str, "replace_all") == 0 || strcasecmp(str, "replace-all") == 0) {
        return NAVIGATION_DIRECTION_REPLACE_ALL;
    } else {
        return def;
    }
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

    if (igloo_ro_new_raw(&n, mount_identifier_t, igloo_instance) != igloo_ERROR_NONE)
        return NULL;

    if (!n)
        return NULL;

    if (igloo_sp_replace(mount, &(n->mount), igloo_instance) != igloo_ERROR_NONE) {
        igloo_ro_unref(&n);
        return NULL;
    }

    return n;
}

const char *            mount_identifier_get_mount(mount_identifier_t *identifier)
{
    return identifier->mount;
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
    ICECAST_LOG_DEBUG("Clearing history->history[history->fill]=%p", history->history[history->fill]);
    igloo_ro_unref(&(history->history[history->fill]));
    return 1; // FIXME: should this be 1?
}

static inline int navigation_history_push(navigation_history_t *history, mount_identifier_t *identifier)
{
    igloo_error_t error;

    if (history->fill > 0 && mount_identifier_compare(history->history[history->fill - 1], identifier) == 0)
        return 0;

    if (history->fill == (sizeof(history->history)/sizeof(*history->history))) {
        igloo_ro_unref(&(history->history[0]));
        memmove(history->history, &(history->history[1]), sizeof(history->history) - sizeof(*history->history));
        history->fill--;
    }

    error = igloo_ro_ref(identifier, &(history->history[history->fill++]), mount_identifier_t);
    if (error != igloo_ERROR_NONE) {
        ICECAST_LOG_ERROR("Error calling igloo_ro_ref(%p, ..., mount_identifier_t): %i", identifier, (int)error);
    }

    char *x;
    if (igloo_ro_stringify(identifier, &x, igloo_RO_SY_OBJECT) != igloo_ERROR_NONE)
        x = NULL;
    ICECAST_LOG_DEBUG("pushing identifier=%p into history=%p -> OK (%s)", identifier, history, x);
    free(x);

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
    mount_identifier_t *id;

    if (!history)
        return NULL;

    if (history->fill < 2)
        return NULL;

    if (igloo_ro_ref(history->history[history->fill - 2], &id, mount_identifier_t) != 0)
        return NULL;

    return id;
}

int                     navigation_history_navigate_to(navigation_history_t *history, mount_identifier_t *identifier, navigation_direction_t direction)
{
    ICECAST_LOG_DDEBUG("Called with history=%p, identifier=%p (%#H), direction=%s", history, identifier, mount_identifier_get_mount(identifier), navigation_direction_to_str(direction));

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
                igloo_error_t error;

                if (history->fill > 1 && mount_identifier_compare(history->history[history->fill - 2], identifier) == 0) {
                    return navigation_history_pop(history);
                }

                error = igloo_ro_ref_replace(identifier, &(history->history[history->fill - 1]), mount_identifier_t);
                if (error != igloo_ERROR_NONE) {
                    ICECAST_LOG_ERROR("Error calling igloo_ro_ref_replace(%p, ..., mount_identifier_t): %i", identifier, (int)error);
                }

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
