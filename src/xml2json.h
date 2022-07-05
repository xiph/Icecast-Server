/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/* This file contains functions for rendering XML as JSON. */

#ifndef __XML2JSON_H__
#define __XML2JSON_H__

#include <libxml/tree.h>

char * xml2json_render_doc_simple(xmlDocPtr doc, const char *default_namespace);

#endif
