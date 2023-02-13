/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 * Copyright 2014-2015, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/*
 * md5.c
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

/* Modified for icecast by Mike Smith <msmith@xiph.org>, mostly changing header
 * and type definitions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "compat.h"
#include "md5.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* The following are proxy functions. This will be removed once more updates have been done */

void MD5Init(struct MD5Context *ctx)
{
    ctx->rhash = rhash_init(RHASH_MD5);
}

void MD5Update(struct MD5Context *ctx, unsigned char const *buf,
        unsigned len)
{
    rhash_update(ctx->rhash, buf, len);
}

void MD5Final(unsigned char digest[HASH_LEN], struct MD5Context *ctx)
{
    rhash_final(ctx->rhash, digest);
    rhash_free(ctx->rhash);
    memset(ctx, 0, sizeof(*ctx));
}
