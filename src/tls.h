/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2016,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __TLS_H__
#define __TLS_H__

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "common/net/sock.h"

/* Do we have TLS Support? */
#if defined(HAVE_OPENSSL)
#define ICECAST_CAP_TLS
#endif


typedef struct tls_ctx_tag tls_ctx_t;
typedef struct tls_tag tls_t;

void       tls_initialize(void);
void       tls_shutdown(void);

tls_ctx_t *tls_ctx_new(const char *cert_file, const char *key_file, const char *cipher_list);
void       tls_ctx_ref(tls_ctx_t *ctx);
void       tls_ctx_unref(tls_ctx_t *ctx);

tls_t     *tls_new(tls_ctx_t *ctx);
void       tls_ref(tls_t *tls);
void       tls_unref(tls_t *tls);

void       tls_set_incoming(tls_t *tls);
void       tls_set_socket(tls_t *tls, sock_t sock);

int        tls_want_io(tls_t *tls);

int        tls_got_shutdown(tls_t *tls);

ssize_t    tls_read(tls_t *tls, void *buffer, size_t len);
ssize_t    tls_write(tls_t *tls, const void *buffer, size_t len);

#endif
