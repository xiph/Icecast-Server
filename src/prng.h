/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __PRNG_H__
#define __PRNG_H__

#include "icecasttypes.h"

void prng_initialize(void);
void prng_shutdown(void);
void prng_configure(ice_config_t *config);

void prng_write(const void *buffer, size_t len);
ssize_t prng_read(void *buffer, size_t len);

int prng_write_file(const char *filename, ssize_t len);
int prng_read_file(const char *filename, ssize_t len);

#endif
