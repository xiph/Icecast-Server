/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2016,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * TLS support functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include "tls.h"

#include "logging.h"
#define CATMODULE "tls"

#ifdef HAVE_OPENSSL
struct tls_ctx_tag {
    size_t refc;
    SSL_CTX *ctx;
};

void       tls_initialize(void)
{
    SSL_load_error_strings(); /* readable error messages */
    SSL_library_init(); /* initialize library */
}
void       tls_shutdown(void)
{
}

tls_ctx_t *tls_ctx_new(const char *cert_file, const char *key_file, const char *cipher_list)
{
    tls_ctx_t *ctx;
    SSL_METHOD *method;
    long ssl_opts;

    if (!cert_file || !key_file || !cipher_list)
        return NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    method = SSLv23_server_method();

    ctx->refc = 1;
    ctx->ctx = SSL_CTX_new(method);

    ssl_opts = SSL_CTX_get_options(ctx->ctx);
#ifdef SSL_OP_NO_COMPRESSION
    SSL_CTX_set_options(ctx->ctx, ssl_opts|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3|SSL_OP_NO_COMPRESSION);
#else
    SSL_CTX_set_options(ctx->ctx, ssl_opts|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
#endif

    do {
        if (SSL_CTX_use_certificate_chain_file(ctx->ctx, cert_file) <= 0) {
            ICECAST_LOG_WARN("Invalid cert file %s", cert_file);
            break;
        }
        if (SSL_CTX_use_PrivateKey_file(ctx->ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
            ICECAST_LOG_WARN("Invalid private key file %s", key_file);
            break;
        }
        if (!SSL_CTX_check_private_key(ctx->ctx)) {
            ICECAST_LOG_ERROR("Invalid %s - Private key does not match cert public key", key_file);
            break;
        }
        if (SSL_CTX_set_cipher_list(ctx->ctx, cipher_list) <= 0) {
            ICECAST_LOG_WARN("Invalid cipher list: %s", cipher_list);
        }
        ICECAST_LOG_INFO("Certificate found at %s", cert_file);
        ICECAST_LOG_INFO("Using ciphers %s", cipher_list);
        return ctx;
    } while (0);

    ICECAST_LOG_INFO("Can not setup TLS.");
    tls_ctx_unref(ctx);
    return NULL;
}

void       tls_ctx_ref(tls_ctx_t *ctx)
{
    if (!ctx)
        return;

    ctx->refc++;
}

void       tls_ctx_unref(tls_ctx_t *ctx)
{
    if (!ctx)
        return;

    ctx->refc--;

    if (ctx->refc)
        return;

    if (ctx->ctx)
        SSL_CTX_free(ctx->ctx);

    free(ctx);
}

SSL       *tls_ctx_SSL_new(tls_ctx_t *ctx)
{
    if (!ctx)
        return NULL;
    return SSL_new(ctx->ctx);
}
#else
void       tls_initialize(void)
{
}
void       tls_shutdown(void)
{
}

tls_ctx_t *tls_ctx_new(const char *cert_file, const char *key_file, const char *cipher_list)
{
    return NULL;
}
void       tls_ctx_ref(tls_ctx_t *ctx)
{
}
void       tls_ctx_unref(tls_ctx_t *ctx)
{
}
#endif
