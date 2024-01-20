/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2023-2023, Philipp Schafft <lion@lion.leolix.org>,
 */

#ifndef __GEOIP_H__
#define __GEOIP_H__

#include "icecasttypes.h"
#include "client.h"

igloo_RO_FORWARD_TYPE(geoip_db_t);

#ifdef HAVE_MAXMINDDB
geoip_db_t * geoip_db_new(const char *filename);
void geoip_lookup_client(geoip_db_t *self, client_t * client);
#else
#define geoip_db_new(filename) NULL
#define geoip_lookup_client(self,client)
#endif

#endif  /* __GEOIP_H__ */
