/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2016-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

/**
 * TLS support functions
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <stdlib.h>
#include <strings.h>

#include "tls.h"

#include "logging.h"
#define CATMODULE "tls"

/* Check for a specific implementation. Returns 0 if supported, 1 if unsupported and -1 on error. */
int        tls_check_impl(const char *impl)
{
#ifdef HAVE_OPENSSL
    if (!strcasecmp(impl, "openssl"))
        return 0;
#endif
#ifdef ICECAST_CAP_TLS
    if (!strcasecmp(impl, "generic"))
        return 0;
#endif

    return 1;
}

#ifdef HAVE_OPENSSL
struct tls_ctx_tag {
    size_t refc;
    SSL_CTX *ctx;
};

struct tls_tag {
    size_t refc;
    SSL *ssl;
    tls_ctx_t *ctx;
};

void       tls_initialize(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_load_error_strings(); /* readable error messages */
    SSL_library_init(); /* initialize library */
#endif
}

void       tls_shutdown(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ERR_free_strings();
#endif
}

tls_ctx_t *tls_ctx_new(const char *cert_file, const char *key_file, const char *cipher_list)
{
    tls_ctx_t *ctx;
    long ssl_opts = 0;

    if (!cert_file || !key_file || !cipher_list)
        return NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->refc = 1;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ctx->ctx = SSL_CTX_new(SSLv23_server_method());
    ssl_opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3; // Disable SSLv2 and SSLv3
#else
    ctx->ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx->ctx, TLS1_VERSION);
#endif

#ifdef SSL_OP_NO_RENEGOTIATION
    // Disable TLSv1.2 renegotiation
    ssl_opts |= SSL_OP_NO_RENEGOTIATION;
#endif

#ifdef SSL_OP_NO_COMPRESSION
    ssl_opts |= SSL_OP_NO_COMPRESSION;             // Never use compression
#endif

    /* Even though this function is called set, it adds the
     * flags to the already existing flags (possibly default
     * flags already set by OpenSSL)!
     * Calling SSL_CTX_get_options is not needed here, therefore.
     */
    SSL_CTX_set_options(ctx->ctx, ssl_opts);
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

tls_t     *tls_new(tls_ctx_t *ctx)
{
    tls_t *tls;
    SSL *ssl;

    if (!ctx)
        return NULL;

    ssl = SSL_new(ctx->ctx);
    if (!ssl)
        return NULL;

    tls = calloc(1, sizeof(*tls));
    if (!tls) {
        SSL_free(ssl);
        return NULL;
    }

    tls_ctx_ref(ctx);

    tls->refc = 1;
    tls->ssl  = ssl;
    tls->ctx  = ctx;

    return tls;
}
void       tls_ref(tls_t *tls)
{
    if (!tls)
        return;
    
    tls->refc++;
}
void       tls_unref(tls_t *tls)
{
    if (!tls)
        return;

    tls->refc--;

    if (tls->refc)
        return;

    SSL_shutdown(tls->ssl);
    SSL_free(tls->ssl);

    if (tls->ctx)
        tls_ctx_unref(tls->ctx);

    free(tls);
}

void       tls_set_incoming(tls_t *tls)
{
    if (!tls)
        return;

    SSL_set_accept_state(tls->ssl);
}
void       tls_set_socket(tls_t *tls, sock_t sock)
{
    if (!tls)
        return;

    SSL_set_fd(tls->ssl, sock);
}

int        tls_want_io(tls_t *tls)
{
    int what;

    if (!tls)
        return -1;

    what = SSL_want(tls->ssl);

    switch (what) {
        case SSL_WRITING:
        case SSL_READING:
            return 1;
        break;
        case SSL_NOTHING:
        default:
            return 0;
        break;
    }
}

int        tls_got_shutdown(tls_t *tls)
{
    if (!tls)
        return -1;

    if (SSL_get_shutdown(tls->ssl) & SSL_RECEIVED_SHUTDOWN) {
        return 1;
    } else {
        return 0;
    }
}

ssize_t    tls_read(tls_t *tls, void *buffer, size_t len)
{
    if (!tls)
        return -1;

    return SSL_read(tls->ssl, buffer, len);
}
ssize_t    tls_write(tls_t *tls, const void *buffer, size_t len)
{
    if (!tls)
        return -1;

    return SSL_write(tls->ssl, buffer, len);
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

tls_t     *tls_new(tls_ctx_t *ctx)
{
    return NULL;
}
void       tls_ref(tls_t *tls)
{
}
void       tls_unref(tls_t *tls)
{
}

void       tls_set_incoming(tls_t *tls)
{
}
void       tls_set_socket(tls_t *tls, sock_t sock)
{
}

int        tls_want_io(tls_t *tls)
{
    return -1;
}

int        tls_got_shutdown(tls_t *tls)
{
    return -1;
}

ssize_t    tls_read(tls_t *tls, void *buffer, size_t len)
{
    return -1;
}
ssize_t    tls_write(tls_t *tls, const void *buffer, size_t len)
{
    return -1;
}

#endif
