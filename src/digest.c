/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "digest.h"
#include "md5.h"

#include "logging.h"
#define CATMODULE "digest"

struct digest_tag {
    /* base object */
    refobject_base_t __base;

    /* metadata */
    digest_algo_t algo;

    /* state */
    int done;
    union {
        struct MD5Context md5;
    } state;
};

REFOBJECT_DEFINE_TYPE(digest_t);

digest_t * digest_new(digest_algo_t algo)
{
    digest_t *digest = refobject_new__new(digest_t, NULL, NULL, NULL);

    if (!digest)
        return NULL;

    digest->algo = algo;
    switch (algo) {
        case DIGEST_ALGO_MD5:
            MD5Init(&(digest->state.md5));
        break;
        default:
            refobject_unref(digest);
            return NULL;
        break;
    }

    return digest;
}

ssize_t digest_write(digest_t *digest, const void *data, size_t len)
{
    if (!digest || !data)
        return -1;

    if (digest->done)
        return -1;

    switch (digest->algo) {
        case DIGEST_ALGO_MD5:
            MD5Update(&(digest->state.md5), (const unsigned char *)data, len);
            return len;
        break;
        default:
            return -1;
        break;
    }
}

ssize_t digest_read(digest_t *digest, void *buf, size_t len)
{
    if (!digest || !buf)
        return -1;

    if (digest->done)
        return -1;

    digest->done = 1;

    switch (digest->algo) {
        case DIGEST_ALGO_MD5:
            if (len < HASH_LEN) {
                unsigned char buffer[HASH_LEN];
                MD5Final(buffer, &(digest->state.md5));
                memcpy(buf, buffer, len);
                return len;
            } else {
                MD5Final((unsigned char*)buf, &(digest->state.md5));
                return HASH_LEN;
            }
        break;
        default:
            return -1;
        break;
    }
}

ssize_t digest_length_bytes(digest_t *digest)
{
    if (!digest)
        return -1;

    switch (digest->algo) {
        case DIGEST_ALGO_MD5:
            return 16;
        break;
        default:
            return -1;
        break;
    }
}
