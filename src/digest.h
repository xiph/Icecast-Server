/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 */

#ifndef __DIGEST_H__
#define __DIGEST_H__

#include "refobject.h"

REFOBJECT_FORWARD_TYPE(digest_t);
REFOBJECT_FORWARD_TYPE(hmac_t);

typedef enum {
    DIGEST_ALGO_MD5,
    DIGEST_ALGO_SHA3_224,
    DIGEST_ALGO_SHA3_256,
    DIGEST_ALGO_SHA3_384,
    DIGEST_ALGO_SHA3_512
} digest_algo_t;

const char *digest_algo_id2str(digest_algo_t algo);
ssize_t     digest_algo_length_bytes(digest_algo_t algo);

digest_t *  digest_new(digest_algo_t algo);
digest_t *  digest_copy(digest_t *digest);
ssize_t     digest_write(digest_t *digest, const void *data, size_t len);
ssize_t     digest_read(digest_t *digest, void *buf, size_t len);

/* Returns the digest size in bytes */
ssize_t     digest_length_bytes(digest_t *digest);


hmac_t *    hmac_new(digest_algo_t algo, const void *key, size_t keylen);
hmac_t *    hmac_copy(hmac_t *hmac);
ssize_t     hmac_write(hmac_t *hmac, const void *data, size_t len);
ssize_t     hmac_read(hmac_t *hmac, void *buf, size_t len);

#endif
