/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __PLAYLIST_H__
#define __PLAYLIST_H__

#include <vorbis/codec.h>

typedef struct playlist_tag playlist_t;

playlist_t * playlist_new(ssize_t max_tracks);
int          playlist_ref(playlist_t *playlist);
int          playlist_release(playlist_t *playlist);

/* set maximum size of playlist.
 * Will not reduce number of tracks if there are more in the list
 * than the new limit.
 */
int          playlist_set_max_tracks(playlist_t *playlist, ssize_t max_tracks);

/* push a new track at the end of the playlist.
 * If the playlist is already at maximum size the oldest track
 * is automatically removed.
 */
int          playlist_push_track(playlist_t *playlist, vorbis_comment *vc);

/* this function returns the root node of the playlist.
 * If you want to use this for file output you need to generate
 * your own xmlDocPtr and attach it as root node.
 */
xmlNodePtr   playlist_render_xspf(playlist_t *playlist);

#endif
