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

#if (defined(HAVE_CRYPT_R) || defined(HAVE_CRYPT)) && defined(HAVE_PTHREAD)
#include <pthread.h>
#endif

#include <igloo/prng.h>

#include "global.h"
#include "util_crypt.h"
#include "util_string.h"

#define HASH_LEN     16

#if !defined(HAVE_CRYPT_R) && defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
static pthread_mutex_t crypt_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#if (defined(HAVE_CRYPT_R) || defined(HAVE_CRYPT)) && HAVE_PTHREAD
struct algo {
    const char prefix[4];
    const size_t saltlen;
    const bool secure;
};

static pthread_once_t crypt_detect = PTHREAD_ONCE_INIT;
static const struct algo *new_algo;
#define HAVE_new_algo

void crypt_detect_run(void)
{
    static const struct algo list[] = {{"$6$", 12, true}, {"$5$", 12, true}, {"$1$", 6, false}};

    for (size_t i = 0; i < (sizeof(list)/sizeof(*list)); i++) {
        if (util_crypt_is_supported(list[i].prefix)) {
            new_algo = &(list[i]);
            return;
        }
    }
}
#endif

char * util_crypt_hash_oldstyle(const char *pw)
{
    unsigned char digest[HASH_LEN];

    if (rhash_msg(RHASH_MD5, pw, strlen(pw), digest) != 0)
        return NULL;

    return util_bin_to_hex(digest, HASH_LEN);
}

char * util_crypt_hash(const char *pw)
{
#if (defined(HAVE_CRYPT_R) || defined(HAVE_CRYPT)) && HAVE_PTHREAD
    if (pthread_once(&crypt_detect, crypt_detect_run) != 0)
        return NULL;

    if (new_algo) {
        char input[128];
        char salt[64];
        char *salt_base64;
        ssize_t len;
#ifdef HAVE_CRYPT_R
        struct crypt_data data;
#elif defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
        char *data;
#endif

        /* if this is true, we have a bug */
        if (new_algo->saltlen > sizeof(salt))
            return NULL;

        len = igloo_prng_read(igloo_instance, salt, new_algo->saltlen, igloo_PRNG_FLAG_NONE);
        if (len != (ssize_t)new_algo->saltlen)
            return NULL;

        salt_base64 = util_base64_encode(salt, new_algo->saltlen);
        if (!salt_base64)
            return NULL;

        snprintf(input, sizeof(input), "%s%s", new_algo->prefix, salt_base64);

        free(salt_base64);

#ifdef HAVE_CRYPT_R
        memset(&data, 0, sizeof(data));

        return strdup(crypt_r(pw, input, &data));
#elif defined(HAVE_CRYPT) && defined(HAVE_PTHREAD)
        if (pthread_mutex_lock(&crypt_mutex) != 0)
            return NULL;

        data = strdup(crypt(pw, input));
        pthread_mutex_unlock(&crypt_mutex);
        return data;
#else
#error "BUG"
#endif
    } else {
#endif
        return util_crypt_hash_oldstyle(pw);
#if (defined(HAVE_CRYPT_R) || defined(HAVE_CRYPT)) && HAVE_PTHREAD
    }
#endif
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
        char *digest = util_crypt_hash_oldstyle(plain);
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

bool   util_crypt_is_supported(const char *prefix)
{
    static const struct {
        const char *plain;
        const char *crypted;
        const bool expected;
    } vectors[] = {
        {"abc", "$1$xxxxxxxx$3GbMJKRcRFz50R9Q96xFb.", true},
        {"abX", "$1$xxxxxxxx$3GbMJKRcRFz50R9Q96xFb.", false},
        {"abc", "$1$xxxxxxxx$3GbMJKRcRFz50R9Q96xFbY", false},
        {"abX", "$1$xxxxxxxx$3GbMJKRcRFz50R9Q96xFbY", false},
        {"abc", "$3$$e0fba38268d0ec66ef1cb452d5885e53", true},
        {"abX", "$3$$e0fba38268d0ec66ef1cb452d5885e53", false},
        {"abc", "$3$$e0fba38268d0ec66ef1cb452d5885e5Y", false},
        {"abX", "$3$$e0fba38268d0ec66ef1cb452d5885e5Y", false},
        {"abc", "$5$xxxxxxxxxxxxxxxx$zNpAueQbvBleD3aSz0KwnySLaHSedk8ULXPvT1m7DUC", true},
        {"abX", "$5$xxxxxxxxxxxxxxxx$zNpAueQbvBleD3aSz0KwnySLaHSedk8ULXPvT1m7DUC", false},
        {"abc", "$5$xxxxxxxxxxxxxxxx$zNpAueQbvBleD3aSz0KwnySLaHSedk8ULXPvT1m7DUY", false},
        {"abX", "$5$xxxxxxxxxxxxxxxx$zNpAueQbvBleD3aSz0KwnySLaHSedk8ULXPvT1m7DUY", false},
        {"abc", "$6$xxxxxxxxxxxxxxxx$yNfBmH1zabagyi9HZwRuCgebrSjfr1zXUE6pFhnTG1BcvINxhgU53sjSUJDnQ5s6FPq8NSIntrpmc5ox87wX5.", true},
        {"abX", "$6$xxxxxxxxxxxxxxxx$yNfBmH1zabagyi9HZwRuCgebrSjfr1zXUE6pFhnTG1BcvINxhgU53sjSUJDnQ5s6FPq8NSIntrpmc5ox87wX5.", false},
        {"abc", "$6$xxxxxxxxxxxxxxxx$yNfBmH1zabagyi9HZwRuCgebrSjfr1zXUE6pFhnTG1BcvINxhgU53sjSUJDnQ5s6FPq8NSIntrpmc5ox87wX5Y", false},
        {"abX", "$6$xxxxxxxxxxxxxxxx$yNfBmH1zabagyi9HZwRuCgebrSjfr1zXUE6pFhnTG1BcvINxhgU53sjSUJDnQ5s6FPq8NSIntrpmc5ox87wX5Y", false},
        {"abc", "$7$DU..../....2Q9obwLhin8qvQl6sisAO/$n4xOT1fpjmazI6Ekeq3slWypZS0PKKV/QVpUE1X0MH6", true},
        {"abX", "$7$DU..../....2Q9obwLhin8qvQl6sisAO/$n4xOT1fpjmazI6Ekeq3slWypZS0PKKV/QVpUE1X0MH6", false},
        {"abc", "$7$DU..../....2Q9obwLhin8qvQl6sisAO/$n4xOT1fpjmazI6Ekeq3slWypZS0PKKV/QVpUE1X0MHY", false},
        {"abX", "$7$DU..../....2Q9obwLhin8qvQl6sisAO/$n4xOT1fpjmazI6Ekeq3slWypZS0PKKV/QVpUE1X0MHY", false},
        {"abc", "$md5$GUBv0xjJ$$59nlXSorBz79sJsp1gfwk1", true},
        {"abX", "$md5$GUBv0xjJ$$59nlXSorBz79sJsp1gfwk1", false},
        {"abc", "$md5$GUBv0xjJ$$59nlXSorBz79sJsp1gfwkY", false},
        {"abX", "$md5$GUBv0xjJ$$59nlXSorBz79sJsp1gfwkY", false},
        {"abc", "$sha1$40000$jtNX3nZ2$Cw.7bEep2dEG6qIx3.0HkiF/YoLW", true},
        {"abX", "$sha1$40000$jtNX3nZ2$Cw.7bEep2dEG6qIx3.0HkiF/YoLW", false},
        {"abc", "$sha1$40000$jtNX3nZ2$Cw.7bEep2dEG6qIx3.0HkiF/YoLY", false},
        {"abX", "$sha1$40000$jtNX3nZ2$Cw.7bEep2dEG6qIx3.0HkiF/YoLY", false},
        {"abc", "$y$j9T$F5Jx5fExrKuPp53xLKQ..1$aC5fZPrKSlHTuOtuJjdRm7BCdVfOnO9UIkyfXQcyB83", true},
        {"abX", "$y$j9T$F5Jx5fExrKuPp53xLKQ..1$aC5fZPrKSlHTuOtuJjdRm7BCdVfOnO9UIkyfXQcyB83", false},
        {"abc", "$y$j9T$F5Jx5fExrKuPp53xLKQ..1$aC5fZPrKSlHTuOtuJjdRm7BCdVfOnO9UIkyfXQcyB8Y", false},
        {"abX", "$y$j9T$F5Jx5fExrKuPp53xLKQ..1$aC5fZPrKSlHTuOtuJjdRm7BCdVfOnO9UIkyfXQcyB8Y", false},
        {"abc", "$gy$jCT$HM87v.7RwpQLba8fDjNSk1$3jEy/aqqTrXmZVCK3RqOQJJS8ve8hM5pSUTTkaTO.l5", true},
        {"abX", "$gy$jCT$HM87v.7RwpQLba8fDjNSk1$3jEy/aqqTrXmZVCK3RqOQJJS8ve8hM5pSUTTkaTO.l5", false},
        {"abc", "$gy$jCT$HM87v.7RwpQLba8fDjNSk1$3jEy/aqqTrXmZVCK3RqOQJJS8ve8hM5pSUTTkaTO.lY", false},
        {"abX", "$gy$jCT$HM87v.7RwpQLba8fDjNSk1$3jEy/aqqTrXmZVCK3RqOQJJS8ve8hM5pSUTTkaTO.lY", false}
    };
    size_t prefixlen = strlen(prefix);
    bool supported = false;

    for (size_t i = 0; i < (sizeof(vectors)/sizeof(*vectors)); i++) {
        if (strncmp(vectors[i].crypted, prefix, prefixlen) == 0) {
            bool res = util_crypt_check(vectors[i].plain, vectors[i].crypted);
            if (res != vectors[i].expected)
                return false;
            supported = true;
        }
    }

    return supported;
}

bool   util_crypt_is_new_secure(void)
{
#ifdef HAVE_new_algo
    if (pthread_once(&crypt_detect, crypt_detect_run) != 0)
        return NULL;

    return new_algo->secure;
#else
    return false;
#endif
}
