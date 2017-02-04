/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2017, Julien CROUZET <contact@juliencrouzet.fr>
 */

#ifndef __CORS_H__
#define __CORS_H__

#include <sys/types.h>

#include "cfgfile.h"
#include "client.h"

void cors_set_headers(char                   **out,
                      size_t                  *len,
                      ice_config_cors_path_t  *options,
                      struct _client_tag      *client);

#endif  /* __CORS_H__ */
