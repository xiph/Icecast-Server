/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>
 *
 * The SHA3 implementation is based on rhash:
 * 2013 by Aleksey Kravchenko <rhash.admin@gmail.com>
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
        struct {
            /* 1600 bits algorithm hashing state */
            uint64_t hash[25];
            /* 1536-bit buffer for leftovers */
            char message[24*8];
            /* count of bytes in the message[] buffer */
            size_t rest;
            /* size of a message block processed at once */
            size_t block_size;
        } sha3;
    } state;
};

REFOBJECT_DEFINE_TYPE(digest_t);

#define ROTL64(qword, n) ((qword) << (n) ^ ((qword) >> (64 - (n))))

#ifdef WORDS_BIGENDIAN
// TODO: Improve this.
static inline uint64_t digest_letoh64(uint64_t x)
{
    union {
        uint64_t num;
        char b[8];
    } in, out;
    int i;

    in.num = x;
    for (i = 0; i < 8; i++)
        out.b[i] = in.b[7 - i];
    return out.num;
}
#else
#define digest_letoh64(x) (x)
#endif

/* SHA3 (Keccak) constants for 24 rounds */
static uint64_t keccak_round_constants[] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL, 0x8000000080008000ULL,
        0x000000000000808BULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008AULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
        0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800AULL, 0x800000008000000AULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static inline void sha3_init(digest_t *digest, unsigned int bits)
{
    /* The Keccak capacity parameter = bits * 2 */
    unsigned int rate = 1600 - bits * 2;

    digest->state.sha3.block_size = rate / 8;
}

#define XORED_A(i) A[(i)] ^ A[(i) + 5] ^ A[(i) + 10] ^ A[(i) + 15] ^ A[(i) + 20]
#define THETA_STEP(i) \
        A[(i)]      ^= D[(i)]; \
        A[(i) + 5]  ^= D[(i)]; \
        A[(i) + 10] ^= D[(i)]; \
        A[(i) + 15] ^= D[(i)]; \
        A[(i) + 20] ^= D[(i)] 

/* Keccak theta() transformation */
static inline void keccak_theta(uint64_t *A)
{
    uint64_t D[5];
    D[0] = ROTL64(XORED_A(1), 1) ^ XORED_A(4);
    D[1] = ROTL64(XORED_A(2), 1) ^ XORED_A(0);
    D[2] = ROTL64(XORED_A(3), 1) ^ XORED_A(1);
    D[3] = ROTL64(XORED_A(4), 1) ^ XORED_A(2);
    D[4] = ROTL64(XORED_A(0), 1) ^ XORED_A(3);
    THETA_STEP(0);
    THETA_STEP(1);
    THETA_STEP(2);
    THETA_STEP(3);
    THETA_STEP(4);
}

/* Keccak pi() transformation */
static inline void keccak_pi(uint64_t *A)
{
    uint64_t A1;
    A1 = A[1];
    A[ 1] = A[ 6];
    A[ 6] = A[ 9];
    A[ 9] = A[22];
    A[22] = A[14];
    A[14] = A[20];
    A[20] = A[ 2];
    A[ 2] = A[12];
    A[12] = A[13];
    A[13] = A[19];
    A[19] = A[23];
    A[23] = A[15];
    A[15] = A[ 4];
    A[ 4] = A[24];
    A[24] = A[21];
    A[21] = A[ 8];
    A[ 8] = A[16];
    A[16] = A[ 5];
    A[ 5] = A[ 3];
    A[ 3] = A[18];
    A[18] = A[17];
    A[17] = A[11];
    A[11] = A[ 7];
    A[ 7] = A[10];
    A[10] = A1;
    /* note: A[ 0] is left as is */
}

#define CHI_STEP(i) \
        A0 = A[0 + (i)]; \
        A1 = A[1 + (i)]; \
        A[0 + (i)] ^= ~A1 & A[2 + (i)]; \
        A[1 + (i)] ^= ~A[2 + (i)] & A[3 + (i)]; \
        A[2 + (i)] ^= ~A[3 + (i)] & A[4 + (i)]; \
        A[3 + (i)] ^= ~A[4 + (i)] & A0; \
        A[4 + (i)] ^= ~A0 & A1

/* Keccak chi() transformation */
static inline void keccak_chi(uint64_t *A)
{
    uint64_t A0, A1;
    CHI_STEP(0);
    CHI_STEP(5);
    CHI_STEP(10);
    CHI_STEP(15);
    CHI_STEP(20);
}

/**
 * The core transformation. Process the specified block of data.
 *
 * @param hash the algorithm state
 * @param block the message block to process
 * @param block_size the size of the processed block in bytes
 */
static inline void sha3_process_block(uint64_t hash[25], const uint64_t *block, size_t block_size)
{
    size_t round;

    /* expanded loop */
    hash[ 0] ^= digest_letoh64(block[ 0]);
    hash[ 1] ^= digest_letoh64(block[ 1]);
    hash[ 2] ^= digest_letoh64(block[ 2]);
    hash[ 3] ^= digest_letoh64(block[ 3]);
    hash[ 4] ^= digest_letoh64(block[ 4]);
    hash[ 5] ^= digest_letoh64(block[ 5]);
    hash[ 6] ^= digest_letoh64(block[ 6]);
    hash[ 7] ^= digest_letoh64(block[ 7]);
    hash[ 8] ^= digest_letoh64(block[ 8]);
    /* if not sha3-512 */
    if (block_size > 72) {
        hash[ 9] ^= digest_letoh64(block[ 9]);
        hash[10] ^= digest_letoh64(block[10]);
        hash[11] ^= digest_letoh64(block[11]);
        hash[12] ^= digest_letoh64(block[12]);
        /* if not sha3-384 */
        if (block_size > 104) {
            hash[13] ^= digest_letoh64(block[13]);
            hash[14] ^= digest_letoh64(block[14]);
            hash[15] ^= digest_letoh64(block[15]);
            hash[16] ^= digest_letoh64(block[16]);
            /* if not sha3-256 */
            if (block_size > 136) {
                hash[17] ^= digest_letoh64(block[17]);
            }
        }
    }

    /* make a permutation of the hash */
    for (round = 0; round < (sizeof(keccak_round_constants)/sizeof(*keccak_round_constants)); round++) {
        keccak_theta(hash);

        /* apply Keccak rho() transformation */
        hash[ 1] = ROTL64(hash[ 1],  1);
        hash[ 2] = ROTL64(hash[ 2], 62);
        hash[ 3] = ROTL64(hash[ 3], 28);
        hash[ 4] = ROTL64(hash[ 4], 27);
        hash[ 5] = ROTL64(hash[ 5], 36);
        hash[ 6] = ROTL64(hash[ 6], 44);
        hash[ 7] = ROTL64(hash[ 7],  6);
        hash[ 8] = ROTL64(hash[ 8], 55);
        hash[ 9] = ROTL64(hash[ 9], 20);
        hash[10] = ROTL64(hash[10],  3);
        hash[11] = ROTL64(hash[11], 10);
        hash[12] = ROTL64(hash[12], 43);
        hash[13] = ROTL64(hash[13], 25);
        hash[14] = ROTL64(hash[14], 39);
        hash[15] = ROTL64(hash[15], 41);
        hash[16] = ROTL64(hash[16], 45);
        hash[17] = ROTL64(hash[17], 15);
        hash[18] = ROTL64(hash[18], 21);
        hash[19] = ROTL64(hash[19],  8);
        hash[20] = ROTL64(hash[20], 18);
        hash[21] = ROTL64(hash[21],  2);
        hash[22] = ROTL64(hash[22], 61);
        hash[23] = ROTL64(hash[23], 56);
        hash[24] = ROTL64(hash[24], 14);

        keccak_pi(hash);
        keccak_chi(hash);

        /* apply iota(hash, round) */
        *hash ^= keccak_round_constants[round];
    }
}

void sha3_write(digest_t *digest, const char *msg, size_t size)
{
    size_t rest = digest->state.sha3.rest;
    size_t block_size = digest->state.sha3.block_size;

    digest->state.sha3.rest = (rest + size) % block_size;

    /* fill partial block */
    if (rest) {
        size_t left = block_size - rest;
        memcpy(digest->state.sha3.message + rest, msg, (size < left ? size : left));
        if (size < left) return;

        /* process partial block */
        sha3_process_block(digest->state.sha3.hash, (uint64_t*)digest->state.sha3.message, block_size);
        msg  += left;
        size -= left;
    }
    while (size >= block_size) {
        const char *aligned_message_block;

        if (((intptr_t)(void*)msg) & 7) {
            memcpy(digest->state.sha3.message, msg, block_size);
            aligned_message_block = digest->state.sha3.message;
        } else {
            aligned_message_block = msg;
        }

        sha3_process_block(digest->state.sha3.hash, (uint64_t*)aligned_message_block, block_size);
        msg  += block_size;
        size -= block_size;
    }

    if (size)
        memcpy(digest->state.sha3.message, msg, size); /* save leftovers */
}

static inline size_t sha3_read(digest_t *digest, void *buf, size_t len)
{
    const size_t block_size = digest->state.sha3.block_size;

    memset(digest->state.sha3.message + digest->state.sha3.rest, 0, block_size - digest->state.sha3.rest);
    digest->state.sha3.message[digest->state.sha3.rest] |= 0x06;
    digest->state.sha3.message[block_size - 1] |= 0x80;

    sha3_process_block(digest->state.sha3.hash, (uint64_t*)digest->state.sha3.message, block_size);

#ifdef WORDS_BIGENDIAN
    do {
        size_t i;
        for (i = 0; i < (sizeof(digest->state.sha3.hash)/sizeof(*digest->state.sha3.hash)); i++)
            digest->state.sha3.hash[i] = digest_htole64(digest->state.sha3.hash[i]);
    } while (0);
#endif

    if (len > (100 - digest->state.sha3.block_size / 2))
        len = 100 - digest->state.sha3.block_size / 2;
    memcpy(buf, digest->state.sha3.hash, len);
    return len;
}


const char *digest_algo_id2str(digest_algo_t algo)
{
    switch (algo) {

        case DIGEST_ALGO_MD5:
            return "MD5";
        break;
        case DIGEST_ALGO_SHA3_224:
            return "SHA3-224";
        break;
        case DIGEST_ALGO_SHA3_256:
            return "SHA3-256";
        break;
        case DIGEST_ALGO_SHA3_384:
            return "SHA3-384";
        break;
        case DIGEST_ALGO_SHA3_512:
            return "SHA3-512";
        break;
    }

    return NULL;
}
ssize_t     digest_algo_length_bytes(digest_algo_t algo)
{
    switch (algo) {
        case DIGEST_ALGO_MD5:
            return 16;
        break;
        case DIGEST_ALGO_SHA3_224:
            return 224/8;
        break;
        case DIGEST_ALGO_SHA3_256:
            return 256/8;
        break;
        case DIGEST_ALGO_SHA3_384:
            return 384/8;
        break;
        case DIGEST_ALGO_SHA3_512:
            return 512/8;
        break;
    }

    return -1;
}

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
        case DIGEST_ALGO_SHA3_224:
            sha3_init(digest, 224);
        break;
        case DIGEST_ALGO_SHA3_256:
            sha3_init(digest, 256);
        break;
        case DIGEST_ALGO_SHA3_384:
            sha3_init(digest, 384);
        break;
        case DIGEST_ALGO_SHA3_512:
            sha3_init(digest, 512);
        break;
        default:
            refobject_unref(digest);
            return NULL;
        break;
    }

    return digest;
}

digest_t *  digest_copy(digest_t *digest)
{
    digest_t *n;

    if (!digest)
        return NULL;

    n = refobject_new__new(digest_t, NULL, NULL, NULL);
    n->algo = digest->algo;
    n->done = digest->done;
    n->state = digest->state;

    return n;
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
        case DIGEST_ALGO_SHA3_224:
        case DIGEST_ALGO_SHA3_256:
        case DIGEST_ALGO_SHA3_384:
        case DIGEST_ALGO_SHA3_512:
            sha3_write(digest, data, len);
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
        case DIGEST_ALGO_SHA3_224:
        case DIGEST_ALGO_SHA3_256:
        case DIGEST_ALGO_SHA3_384:
        case DIGEST_ALGO_SHA3_512:
            return sha3_read(digest, buf, len);
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

    return digest_algo_length_bytes(digest->algo);
}
