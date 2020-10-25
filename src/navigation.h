/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __NAVIGATION_H__
#define __NAVIGATION_H__

#include <string.h>

#include "refobject.h"

#define MAX_NAVIGATION_HISTORY_SIZE  8

typedef struct {
    mount_identifier_t *history[MAX_NAVIGATION_HISTORY_SIZE];
    size_t fill;
} navigation_history_t;

typedef enum {
    NAVIGATION_DIRECTION_UP,
    NAVIGATION_DIRECTION_DOWN,
    NAVIGATION_DIRECTION_REPLACE_CURRENT,
    NAVIGATION_DIRECTION_REPLACE_ALL
} navigation_direction_t;

REFOBJECT_FORWARD_TYPE(mount_identifier_t);

const char * navigation_direction_to_str(navigation_direction_t dir);

void navigation_initialize(void);
void navigation_shutdown(void);

mount_identifier_t *    mount_identifier_new(const char *mount);
#define mount_identifier_get_mount(identifier)  refobject_get_name((identifier))
int                     mount_identifier_compare(mount_identifier_t *a, mount_identifier_t *b);

#define navigation_history_init(history) memset((history), 0, sizeof(navigation_history_t))
void                    navigation_history_clear(navigation_history_t *history);
mount_identifier_t *    navigation_history_get_up(navigation_history_t *history);
int                     navigation_history_navigate_to(navigation_history_t *history, mount_identifier_t *identifier, navigation_direction_t direction);

#endif
