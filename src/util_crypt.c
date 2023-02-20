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

#include "util_crypt.h"
#include "util_string.h"

#define HASH_LEN     16

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
    if (len == (HASH_LEN*2) && crypted[0] != '$') {
        char *digest = util_crypt_hash(plain);
        bool res;

        if (!digest)
            return false;

        res = strcmp(digest, crypted) == 0;
        free(digest);
        return res;
    }

    return false;
}
