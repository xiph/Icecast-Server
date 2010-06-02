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
 */

#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include <sys/types.h>
#include <time.h>
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

struct source_tag;
struct ice_config_tag;
typedef struct connection_tag connection_t;

#include "compat.h"
#include "httpp/httpp.h"
#include "net/sock.h"

struct connection_tag
{
    unsigned long id;

    time_t con_time;
    time_t discon_time;
    uint64_t sent_bytes;

    sock_t sock;
    int error;

#ifdef HAVE_OPENSSL
    SSL *ssl;   /* SSL handler */
#endif

    char *ip;
};

#ifdef HAVE_OPENSSL
#define not_ssl_connection(x)    ((x)->ssl==NULL)
#else
#define not_ssl_connection(x)    (1)
#endif
void connection_initialize(void);
void connection_shutdown(void);
void connection_thread_startup();
void connection_thread_shutdown();
int  connection_setup_sockets (struct ice_config_tag *config);
void connection_close(connection_t *con);
int  connection_init (connection_t *con, sock_t sock, const char *addr);
int  connection_complete_source (struct source_tag *source);
void connection_uses_ssl (connection_t *con);
void connection_add_banned_ip (const char *ip, int duration);
void connection_stats (void);
#ifdef HAVE_OPENSSL
int  connection_read_ssl (connection_t *con, void *buf, size_t len);
int  connection_send_ssl (connection_t *con, const void *buf, size_t len);
#endif
int  connection_read (connection_t *con, void *buf, size_t len);
int  connection_send (connection_t *con, const void *buf, size_t len);
void connection_thread_shutdown_req (void);

int connection_check_pass (http_parser_t *parser, const char *user, const char *pass);
int connection_check_relay_pass(http_parser_t *parser);
int connection_check_admin_pass(http_parser_t *parser);

void connection_close_sigfd (void);

extern int connection_running;

#endif  /* __CONNECTION_H__ */
