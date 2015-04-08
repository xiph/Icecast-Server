/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2015,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __CURL_H__
#define __CURL_H__

#include <curl/curl.h>

/* initialize/shutdown libcurl.
 * Those two functions are fully thread unsafe
 * and must only be run by the main thread and
 * only when no other thread is running.
 */
int   icecast_curl_initialize(void);
int   icecast_curl_shutdown(void);

/* creator and destructor function for CURL* object. */
CURL *icecast_curl_new(const char *url, char * errors);
int   icecast_curl_free(CURL *curl);

#endif
