/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */


#ifndef __FORMAT_MIDI_H
#define __FORMAT_MIDI_H

#include "format_ogg.h"

ogg_codec_t *initial_midi_page (format_plugin_t *plugin, ogg_page *page);

#endif /* __FORMAT_MIDI_H */
