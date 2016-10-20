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

typedef struct tls_ctx_tag tls_ctx_t;

void       tls_initialize(void);
void       tls_shutdown(void);

tls_ctx_t *tls_ctx_new(const char *cert_file, const char *key_file, const char *cipher_list);
void       tls_ctx_ref(tls_ctx_t *ctx);
void       tls_ctx_unref(tls_ctx_t *ctx);

#ifdef HAVE_OPENSSL
SSL       *tls_ctx_SSL_new(tls_ctx_t *ctx);
#endif

#endif
