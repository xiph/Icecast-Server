/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2022,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __METADATA_XIPH_H__
#define __METADATA_XIPH_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> /* for size_t */

#include <vorbis/codec.h>

uint32_t metadata_xiph_read_u32be_unaligned(const unsigned char *in);
uint32_t metadata_xiph_read_u32le_unaligned(const unsigned char *in);

/* returns true if parsing was successful, *vc must be in inited state before and will be in inited state after (even when false is returned) */
bool     metadata_xiph_read_vorbis_comments(vorbis_comment *vc, const void *buffer, size_t len);

#endif
