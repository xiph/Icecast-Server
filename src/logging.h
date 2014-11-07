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

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include "cfgfile.h"
#include "log/log.h"

/* declare the global log descriptors */

extern int errorlog;
extern int accesslog;
extern int playlistlog;

#ifdef _WIN32
#include <string.h>
#define __func__ strrchr (__FILE__, '\\') ? strrchr (__FILE__, '\\') + 1 : __FILE__
#endif

/*
** Variadic macros for logging
*/

#define ICECAST_LOG_ERROR(...) log_write(errorlog, 1, CATMODULE "/", __func__, __VA_ARGS__)
#define ICECAST_LOG_WARN(...) log_write(errorlog, 2, CATMODULE "/", __func__, __VA_ARGS__)
#define ICECAST_LOG_INFO(...) log_write(errorlog, 3, CATMODULE "/", __func__, __VA_ARGS__)
#define ICECAST_LOG_DEBUG(...) log_write(errorlog, 4, CATMODULE "/", __func__, __VA_ARGS__)

/* CATMODULE is the category or module that logging messages come from.
** we set one here in cause someone forgets in the .c file.
*/
/*#define CATMODULE "unknown"
 */

/* this is the logging call to write entries to the access_log
** the combined log format is:
** ADDR USER AUTH DATE REQUEST CODE BYTES REFERER AGENT [TIME]
** ADDR = ip address of client
** USER = username if authenticated
** AUTH = auth type, not used, and set to "-"
** DATE = date in "[30/Apr/2001:01:25:34 -0700]" format
** REQUEST = request, ie "GET /live.ogg HTTP/1.0"
** CODE = response code, ie, 200 or 404
** BYTES = total bytes of data sent (other than headers)
** REFERER = the refering URL
** AGENT = the user agent
**
** for icecast, we add on extra field at the end, which will be 
** ignored by normal log parsers
**
** TIME = seconds that the connection lasted
** 
** this allows you to get bitrates (BYTES / TIME)
** and figure out exact times of connections
**
** it should be noted also that events are sent on client disconnect,
** so the DATE is the timestamp of disconnection.  DATE - TIME is the 
** time of connection.
*/

#define LOGGING_FORMAT_CLF "%d/%b/%Y:%H:%M:%S %z"

void logging_access(client_t *client);
void logging_playlist(const char *mount, const char *metadata, long listeners);
void restart_logging (ice_config_t *config);
void log_parse_failure (void *ctx, const char *fmt, ...);

#endif  /* __LOGGING_H__ */

