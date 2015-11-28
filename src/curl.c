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

#include "curl.h"

#include "logging.h"
#define CATMODULE "curl"

#ifdef CURLOPT_PASSWDFUNCTION
/* make sure that prompting at the console does not occur */
static int my_getpass(void *client, char *prompt, char *buffer, int buflen) {
    buffer[0] = '\0';
    return 0;
}
#endif

static size_t handle_returned (void *ptr, size_t size, size_t nmemb, void *stream) {
    (void)ptr, (void)stream;
    return size * nmemb;
}

int   icecast_curl_initialize(void)
{
#ifdef HAVE_CURL_GLOBAL_INIT
    CURLcode ret = curl_global_init(CURL_GLOBAL_ALL);
    if (ret != 0)
        return -1;
#endif

    return 0;
}

int   icecast_curl_shutdown(void)
{
    curl_global_cleanup();
    return 0;
}

CURL *icecast_curl_new(const char *url, char * errors)
{
    ice_config_t *config;
    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

#if XXXX
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
    curl_easy_setopt(url->handle, CURLOPT_USERPWD, url->userpwd);
    curl_easy_setopt(self->handle, CURLOPT_POSTFIELDS, post);
#endif

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handle_returned);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl);

#ifdef CURLOPT_PASSWDFUNCTION
    curl_easy_setopt(curl, CURLOPT_PASSWDFUNCTION, my_getpass);
#endif

    if (url)
        curl_easy_setopt(curl, CURLOPT_URL, url);
    if (errors)
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errors);

    config = config_get_config();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config->server_id);
    config_release_config();

    return curl;
}

int icecast_curl_free(CURL *curl)
{
    if (!curl)
        return -1;
    curl_easy_cleanup(curl);
    return 0;
}
