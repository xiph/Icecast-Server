/* Icecast
 *
 * This program is distributed under the GNU General Public License,
 * version 2 or later. A copy of this license is included with this source.
 *
 * Copyright 2012,      David Richards, Mozilla Foundation,
 *                      and others (see AUTHORS for details).
 */


#ifndef __FORMAT_OPUS_H
#define __FORMAT_OPUS_H

#include "format_ogg.h"

ogg_codec_t *initial_opus_page (format_plugin_t *plugin, ogg_page *page);

#endif /* __FORMAT_OPUS_H */
