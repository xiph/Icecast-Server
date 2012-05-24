/* Icecast
 *
 * This program is distributed under the GNU General Public License,
 * version 2. A copy of this license is included with this source.
 * At your option, this specific source file can also be distributed
 * under the GNU GPL version 3.
 *
 * Copyright 2012,      David Richards, Mozilla Foundation,
 *                      and others (see AUTHORS for details).
 */

/* format_ebml.h
**
** ebml format plugin header
**
*/
#ifndef __FORMAT_EBML_H__
#define __FORMAT_EBML_H__

#include "format.h"

typedef struct ebml_st ebml_t;
typedef struct ebml_source_state_st ebml_source_state_t;

struct ebml_source_state_st {

    ebml_t *ebml;
    refbuf_t *header;
    int file_headers_written;

};

int format_ebml_get_plugin (source_t *source);

#endif  /* __FORMAT_EBML_H__ */
