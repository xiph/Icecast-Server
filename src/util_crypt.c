/* Icecast
 *
 *  Copyright 2023      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 *  This program is distributed under the GNU General Public License, version 2.
 *  A copy of this license is included with this source.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rhash.h>

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#if !defined(HAVE_CRYPT_R) && defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
#include <pthread.h>
#endif

#include "util_crypt.h"
#include "util_string.h"

#define HASH_LEN     16

#if !defined(HAVE_CRYPT_R) && defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
static pthread_mutex_t crypt_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

char * util_crypt_hash(const char *pw)
{
    unsigned char digest[HASH_LEN];

    if (rhash_msg(RHASH_MD5, pw, strlen(pw), digest) != 0)
        return NULL;

    return util_bin_to_hex(digest, HASH_LEN);
}

bool   util_crypt_check(const char *plain, const char *crypted)
{
    size_t len;

    if (!plain || !crypted)
        return false;

    len = strlen(crypted);
    if (!len)
        return false;

    /* below here we know that plain and crypted are non-null and that crypted is at least one byte long */

    if (len == (HASH_LEN*2) && crypted[0] != '$') {
        char *digest = util_crypt_hash(plain);
        bool res;

        if (!digest)
            return false;

        res = strcmp(digest, crypted) == 0;
        free(digest);
        return res;
    }

    if (crypted[0] == '$') {
        const char *cres;
#ifdef HAVE_CRYPT_R
        struct crypt_data data;

        memset(&data, 0, sizeof(data));

        cres = crypt_r(plain, crypted, &data);
        if (cres && strcmp(crypted, cres) == 0)
            return true;
#elif defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
        bool res = false;

        if (pthread_mutex_lock(&crypt_mutex) != 0)
            return false;

        cres = crypt(plain, crypted);
        if (cres && strcmp(crypted, cres) == 0)
            res = true;
        pthread_mutex_unlock(&crypt_mutex);
        return res;
#endif
    }

    return false;
}
