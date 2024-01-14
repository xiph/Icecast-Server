/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2023-2023, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * Client authentication functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <igloo/igloo.h>
#include <igloo/error.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#else
#error "No pthread support"
#endif

#include <libxml/xmlversion.h>

#ifdef HAVE_OPENSSL
#include <openssl/opensslv.h>
#endif

#include <vorbis/codec.h>

#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif

#ifdef HAVE_SPEEX
#include <speex/speex.h>
#endif

#ifdef HAVE_CURL
#include <curl/curlver.h>
#include <curl/curl.h>
#endif

#include "version.h"

#include "logging.h"
#define CATMODULE "version"

const char * const * version_get_compiletime_flags(void)
{
    static const char * const compiletime_flags[] = {
        /* ---[ Functions ]--- */
#ifdef HAVE_POLL
        "poll",
#endif
#ifdef HAVE_SYS_SELECT_H
        "select",
#endif
#ifdef HAVE_UNAME
        "uname",
#endif
#ifdef HAVE_GETHOSTNAME
        "gethostname",
#endif
#ifdef HAVE_GETADDRINFO
        "getaddrinfo",
#endif
#ifdef HAVE_CRYPT
        "crypt",
#endif
#ifdef HAVE_CRYPT_R
        "crypt_r",
#endif
#ifdef HAVE_PIPE
        "pipe",
#endif
#ifdef HAVE_PIPE2
        "pipe2",
#endif
#ifdef HAVE_SOCKETPAIR
        "socketpair",
#endif
#ifdef HAVE_POSIX_SPAWN
        "posix_spawn",
#endif
#ifdef HAVE_POSIX_SPAWNP
        "posix_spawnp",
#endif
#ifdef HAVE_POSIX_FADVISE
        "posix_fadvise",
#endif
#ifdef HAVE_POSIX_FALLOCATE
        "posix_fallocate",
#endif
#ifdef HAVE_POSIX_MADVISE
        "posix_madvise",
#endif
#ifdef HAVE_FALLOCATE
        "fallocate",
#endif
#ifdef HAVE_FTRUNCATE
        "ftruncate",
#endif
        /* ---[ OS ]--- */
#ifdef WIN32
        "win32",
#endif
        /* ---[ Options ]--- */
#ifdef DEVEL_LOGGING
        "developer-logging",
#endif
        NULL,
    };

    return compiletime_flags;
}

#ifdef HAVE_SPEEX
static inline const char *get_speex_version(void)
{
    const char *version;
    if (speex_lib_ctl(SPEEX_LIB_GET_VERSION_STRING, &version) != 0)
        return NULL;
    return version;
}
#endif

static inline const char *get_igloo_version(void)
{
    const char *version;
    if (igloo_version_get(&version, NULL, NULL, NULL) != igloo_ERROR_NONE)
        return NULL;
    return version;
}


#ifdef HAVE_PTHREAD
static pthread_once_t version_detect = PTHREAD_ONCE_INIT;
static icecast_dependency_t dependency_versions_real[32];

static inline void dependency_versions_add(size_t i, const char *name, const char *compiletime, const char *runtime)
{
    if (i >= ((sizeof(dependency_versions_real)/sizeof(*dependency_versions_real)) - 1)) /* substract 1 for final NULL-row */
        return;

    dependency_versions_real[i].name = name;
    dependency_versions_real[i].compiletime = compiletime;
    dependency_versions_real[i].runtime = runtime;
}

static void version_init(void)
{
#ifdef HAVE_CURL
    const curl_version_info_data * curl_runtime_version = curl_version_info(CURLVERSION_NOW);
#endif
    size_t i = 0;

    dependency_versions_add(i++, "libigloo", NULL, get_igloo_version());
    dependency_versions_add(i++, "libxml2", LIBXML_DOTTED_VERSION, NULL);
#if defined(HAVE_OPENSSL) && defined(OPENSSL_VERSION_TEXT)
    dependency_versions_add(i++, "OpenSSL", OPENSSL_VERSION_TEXT, NULL);
#endif
    dependency_versions_add(i++, "libvorbis", NULL, vorbis_version_string());
#ifdef HAVE_THEORA
    dependency_versions_add(i++, "libtheora", NULL, theora_version_string());
#endif
#ifdef HAVE_SPEEX
    dependency_versions_add(i++, "libspeex", NULL, get_speex_version());
#endif
#ifdef HAVE_CURL
    dependency_versions_add(i++, "libcurl", LIBCURL_VERSION, curl_runtime_version->version);
#endif
}
#endif


const icecast_dependency_t * version_get_dependencies(void)
{
#ifdef HAVE_PTHREAD
    if (pthread_once(&version_detect, version_init) != 0)
        return NULL;
#endif

    return dependency_versions_real;
}
