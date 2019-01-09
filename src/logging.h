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
 * Copyright 2014-2018, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include "common/log/log.h"

#include "icecasttypes.h"

/* declare the global log descriptors */

extern int errorlog;
extern int accesslog;
extern int playlistlog;

#ifdef _WIN32
#include <string.h>
#define __func__ strrchr (__FILE__, '\\') ? strrchr (__FILE__, '\\') + 1 : __FILE__
#endif

/* Log levels */
#define ICECAST_LOGLEVEL_ERROR  1
#define ICECAST_LOGLEVEL_WARN   2
#define ICECAST_LOGLEVEL_INFO   3
#define ICECAST_LOGLEVEL_DEBUG  4

/* Log flags */
#define ICECAST_LOGFLAG_NONE    0
#define ICECAST_LOGFLAG_DEVEL   1

/*
** Variadic macros for logging
*/

#define ICECAST_LOG(level,flags,...) log_write(errorlog, (level), CATMODULE "/", __func__, __VA_ARGS__)

#define ICECAST_LOG_ERROR(...)  ICECAST_LOG(ICECAST_LOGLEVEL_ERROR, ICECAST_LOGFLAG_NONE, __VA_ARGS__)
#define ICECAST_LOG_WARN(...)   ICECAST_LOG(ICECAST_LOGLEVEL_WARN,  ICECAST_LOGFLAG_NONE, __VA_ARGS__)
#define ICECAST_LOG_INFO(...)   ICECAST_LOG(ICECAST_LOGLEVEL_INFO,  ICECAST_LOGFLAG_NONE, __VA_ARGS__)
#define ICECAST_LOG_DEBUG(...)  ICECAST_LOG(ICECAST_LOGLEVEL_DEBUG, ICECAST_LOGFLAG_NONE, __VA_ARGS__)
/* Currently only an alias for ICECAST_LOG_DEBUG() */
#ifdef DEVEL_LOGGING
#define ICECAST_LOG_DERROR(...) ICECAST_LOG(ICECAST_LOGLEVEL_ERROR, ICECAST_LOGFLAG_DEVEL, __VA_ARGS__)
#define ICECAST_LOG_DWARN(...)  ICECAST_LOG(ICECAST_LOGLEVEL_WARN,  ICECAST_LOGFLAG_DEVEL, __VA_ARGS__)
#define ICECAST_LOG_DINFO(...)  ICECAST_LOG(ICECAST_LOGLEVEL_INFO,  ICECAST_LOGFLAG_DEVEL, __VA_ARGS__)
#define ICECAST_LOG_DDEBUG(...) ICECAST_LOG(ICECAST_LOGLEVEL_DEBUG, ICECAST_LOGFLAG_DEVEL, __VA_ARGS__)
#else
#define ICECAST_LOG_DERROR(...)
#define ICECAST_LOG_DWARN(...)
#define ICECAST_LOG_DINFO(...)
#define ICECAST_LOG_DDEBUG(...)
#endif

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
