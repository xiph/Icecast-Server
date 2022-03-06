/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018-2020, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __LISTENSOCKET_H__
#define __LISTENSOCKET_H__

#include "common/net/sock.h"

#include "icecasttypes.h"
#include "refobject.h"
#include "cfgfile.h"

REFOBJECT_FORWARD_TYPE(listensocket_container_t);

listensocket_container_t *  listensocket_container_new(void);
int                         listensocket_container_configure(listensocket_container_t *self, const ice_config_t *config);
int                         listensocket_container_configure_and_setup(listensocket_container_t *self, const ice_config_t *config);
int                         listensocket_container_setup(listensocket_container_t *self);
connection_t *              listensocket_container_accept(listensocket_container_t *self, int timeout);
int                         listensocket_container_set_sockcount_cb(listensocket_container_t *self, void (*cb)(size_t count, void *userdata), void *userdata);
ssize_t                     listensocket_container_sockcount(listensocket_container_t *self);
listensocket_t *            listensocket_container_get_by_id(listensocket_container_t *self, const char *id);
listensocket_t **           listensocket_container_list_sockets(listensocket_container_t *self);

REFOBJECT_FORWARD_TYPE(listensocket_t);

int                         listensocket_refsock(listensocket_t *self);
int                         listensocket_unrefsock(listensocket_t *self);
connection_t *              listensocket_accept(listensocket_t *self, listensocket_container_t *container);
const listener_t *          listensocket_get_listener(listensocket_t *self);
int                         listensocket_release_listener(listensocket_t *self);
listener_type_t             listensocket_get_type(listensocket_t *self);
sock_family_t               listensocket_get_family(listensocket_t *self);

const char *                listensocket_type_to_string(listener_type_t type);
const char *                listensocket_tlsmode_to_string(tlsmode_t mode);

#endif
