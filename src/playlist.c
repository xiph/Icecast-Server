/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "playlist.h"

/* for XMLSTR() */
#include "cfgfile.h"

#include "logging.h"
#define CATMODULE "playlist"

typedef struct playlist_track_tag playlist_track_t;

struct playlist_tag {
    size_t refc;
    ssize_t max_tracks;
    playlist_track_t *first;
};

struct playlist_track_tag {
    char *title;
    char *creator;
    char *album;
    char *trackNum;
    playlist_track_t *next;
};

static void __free_track(playlist_track_t *track)
{
    if (track->title)
        free(track->title);
    if (track->creator)
        free(track->creator);
    if (track->album)
        free(track->album);
    if (track->trackNum)
        free(track->trackNum);
    free(track);
}

static char * __query_vc(vorbis_comment *vc, const char *key)
{
    char *value = vorbis_comment_query(vc, key, 0);
    if (!value)
        return NULL;
    return strdup(value);
}

playlist_t * playlist_new(ssize_t max_tracks)
{
    playlist_t *playlist = calloc(1, sizeof(playlist_t));

    if (!playlist)
       return NULL;

    playlist->refc = 1;
    playlist->max_tracks = max_tracks;

    return playlist;
}

int          playlist_ref(playlist_t *playlist)
{
    if (!playlist)
        return -1;
    playlist->refc++;
    return 0;
}

int          playlist_release(playlist_t *playlist)
{
    playlist_track_t *track;

    if (!playlist)
        return -1;
    playlist->refc--;
    if (playlist->refc)
        return 0;

    while ((track = playlist->first)) {
        playlist->first = track->next;
        __free_track(track);
    }

    free(playlist);
    return 0;
}

int          playlist_set_max_tracks(playlist_t *playlist, ssize_t max_tracks)
{
    if (!playlist)
        return -1;
    playlist->max_tracks = max_tracks;
    return 0;
}


int          playlist_push_track(playlist_t *playlist, vorbis_comment *vc)
{
    playlist_track_t *track, **cur;
    ssize_t num = 0;

    if (!playlist)
        return -1;

    track = calloc(1, sizeof(playlist_track_t));
    if (!track)
        return -1;

    cur = &playlist->first;
    while (*cur) {
        cur = &(*cur)->next;
        num++;
    }
    *cur = track;

    while (playlist->max_tracks > 0 && num > playlist->max_tracks) {
        playlist_track_t *to_free = playlist->first;
        playlist->first = to_free->next;
        __free_track(to_free);
        num--;
    }

    if (vc) {
        track->title = __query_vc(vc, "TITLE");
        track->creator = __query_vc(vc, "ARTIST");
        track->album = __query_vc(vc, "ALBUM");
        track->trackNum = __query_vc(vc, "TRACKNUMBER");
    }

    return 0;
}

xmlNodePtr   playlist_render_xspf(playlist_t *playlist)
{
    xmlNodePtr rootnode, tracklist, tracknode;
    playlist_track_t *track;

    if (!playlist)
        return NULL;

    rootnode = xmlNewNode(NULL, XMLSTR("playlist"));
    xmlSetProp(rootnode, XMLSTR("version"), XMLSTR("1"));
    xmlSetProp(rootnode, XMLSTR("xmlns"), XMLSTR("http://xspf.org/ns/0/"));

    tracklist = xmlNewNode(NULL, XMLSTR("trackList"));
    xmlAddChild(rootnode, tracklist);

    track = playlist->first;
    while (track) {
        tracknode = xmlNewNode(NULL, XMLSTR("track"));
        xmlAddChild(tracklist, tracknode);
        /* TODO: Handle meta data */
        if (track->title)
            xmlNewTextChild(tracknode, NULL, XMLSTR("title"), XMLSTR(track->title));
        if (track->creator)
            xmlNewTextChild(tracknode, NULL, XMLSTR("creator"), XMLSTR(track->creator));
        if (track->album)
            xmlNewTextChild(tracknode, NULL, XMLSTR("album"), XMLSTR(track->album));
        if (track->trackNum)
            xmlNewTextChild(tracknode, NULL, XMLSTR("trackNum"), XMLSTR(track->trackNum));
        track = track->next;
    }

    return rootnode;
}
