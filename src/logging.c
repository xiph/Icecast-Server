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
 * Copyright 2011-2012, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "thread/thread.h"
#include "httpp/httpp.h"

#include "connection.h"
#include "refbuf.h"
#include "client.h"

#include "compat.h"
#include "cfgfile.h"
#include "logging.h"
#include "util.h"

#ifdef _WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

/* the global log descriptors */
int errorlog = 0;
int accesslog = 0;
int playlistlog = 0;

#ifdef _WIN32
/* Since strftime's %z option on win32 is different, we need
   to go through a few loops to get the same info as %z */
int get_clf_time (char *buffer, unsigned len, struct tm *t)
{
    char    sign;
    char    *timezone_string;
    struct tm gmt;
    time_t time1 = time(NULL);
    int time_days, time_hours, time_tz;
    int tempnum1, tempnum2;
    struct tm *thetime;
    time_t now;

#if !defined(_WIN32)
    thetime = gmtime_r(&time1, &gmt)
#else
    /* gmtime() on W32 breaks POSIX and IS thread-safe (uses TLS) */
    thetime = gmtime (&time1);
    if (thetime)
      memcpy (&gmt, thetime, sizeof (gmt));
#endif
    /* FIXME: bail out if gmtime* returns NULL */

    time_days = t->tm_yday - gmt.tm_yday;

    if (time_days < -1) {
        tempnum1 = 24;
    }
    else {
        tempnum1 = 1;
    }
    if (tempnum1 < time_days) {
       tempnum2 = -24;
    }
    else {
        tempnum2 = time_days*24;
    }

    time_hours = (tempnum2 + t->tm_hour - gmt.tm_hour);
    time_tz = time_hours * 60 + t->tm_min - gmt.tm_min;

    if (time_tz < 0) {
        sign = '-';
        time_tz = -time_tz;
    }
    else {
        sign = '+';
    }

    timezone_string = calloc(1, 7);
    snprintf(timezone_string, 7, " %c%.2d%.2d", sign, time_tz / 60, time_tz % 60);

    now = time(NULL);

    thetime = localtime(&now);
    strftime (buffer, len-7, "%d/%b/%Y:%H:%M:%S", thetime);
    strcat(buffer, timezone_string);
	free(timezone_string);
    return 1;
}
#endif
/* 
** ADDR IDENT USER DATE REQUEST CODE BYTES REFERER AGENT [TIME]
**
** ADDR = client->con->ip
** IDENT = always - , we don't support it because it's useless
** USER = client->username
** DATE = _make_date(client->con->con_time)
** REQUEST = build from client->parser
** CODE = client->respcode
** BYTES = client->con->sent_bytes
** REFERER = get from client->parser
** AGENT = get from client->parser
** TIME = timing_get_time() - client->con->con_time
*/
void logging_access(client_t *client)
{
    char datebuf[128];
    struct tm thetime;
    time_t now;
    time_t stayed;
    const char *referrer, *user_agent, *username;

    now = time(NULL);

    localtime_r (&now, &thetime);
    /* build the data */
#ifdef _WIN32
    memset(datebuf, '\000', sizeof(datebuf));
    get_clf_time(datebuf, sizeof(datebuf)-1, &thetime);
#else
    strftime (datebuf, sizeof(datebuf), LOGGING_FORMAT_CLF, &thetime);
#endif

    stayed = now - client->con->con_time;

    if (client->username == NULL)
        username = "-"; 
    else
        username = client->username;

    referrer = httpp_getvar (client->parser, "referer");
    if (referrer == NULL)
        referrer = "-";

    user_agent = httpp_getvar (client->parser, "user-agent");
    if (user_agent == NULL)
        user_agent = "-";

    log_write_direct (accesslog,
            "%s - %H [%s] \"%H %H %H/%H\" %d %llu \"% H\" \"% H\" %llu",
            client->con->ip,
            username,
            datebuf,
            httpp_getvar (client->parser, HTTPP_VAR_REQ_TYPE),
            httpp_getvar (client->parser, HTTPP_VAR_URI),
            httpp_getvar (client->parser, HTTPP_VAR_PROTOCOL),
            httpp_getvar (client->parser, HTTPP_VAR_VERSION),
            client->respcode,
            (long long unsigned int)client->con->sent_bytes,
            referrer,
            user_agent,
            (long long unsigned int)stayed);
}
/* This function will provide a log of metadata for each
   mountpoint.  The metadata *must* be in UTF-8, and thus
   you can assume that the log itself is UTF-8 encoded */
void logging_playlist(const char *mount, const char *metadata, long listeners)
{
    char datebuf[128];
    struct tm thetime;
    time_t now;

    if (playlistlog == -1) {
        return;
    }

    now = time(NULL);

    localtime_r (&now, &thetime);
    /* build the data */
#ifdef _WIN32
    memset(datebuf, '\000', sizeof(datebuf));
    get_clf_time(datebuf, sizeof(datebuf)-1, &thetime);
#else
    strftime (datebuf, sizeof(datebuf), LOGGING_FORMAT_CLF, &thetime);
#endif
    /* This format MAY CHANGE OVER TIME.  We are looking into finding a good
       standard format for this, if you have any ideas, please let us know */
    log_write_direct (playlistlog, "%s|%s|%ld|%s",
             datebuf,
             mount,
             listeners,
             metadata);
}


void log_parse_failure (void *ctx, const char *fmt, ...)
{
    char line [200];
    va_list ap;
    char *eol;

    va_start (ap, fmt);
    vsnprintf (line, sizeof (line), fmt, ap);
    eol = strrchr (line, '\n');
    if (eol) *eol='\0';
    va_end (ap);
    log_write (errorlog, 2, (char*)ctx, "", "%s", line);
}


void restart_logging (ice_config_t *config)
{
    if (strcmp (config->error_log, "-"))
    {
        char fn_error[FILENAME_MAX];
        snprintf (fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
        log_set_filename (errorlog, fn_error);
        log_set_level (errorlog, config->loglevel);
        log_set_trigger (errorlog, config->logsize);
        log_set_archive_timestamp(errorlog, config->logarchive);
        log_reopen (errorlog);
    }

    if (strcmp (config->access_log, "-"))
    {
        char fn_error[FILENAME_MAX];
        snprintf (fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);
        log_set_filename (accesslog, fn_error);
        log_set_trigger (accesslog, config->logsize);
        log_set_archive_timestamp (accesslog, config->logarchive);
        log_reopen (accesslog);
    }

    if (config->playlist_log)
    {
        char fn_error[FILENAME_MAX];
        snprintf (fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->playlist_log);
        log_set_filename (playlistlog, fn_error);
        log_set_trigger (playlistlog, config->logsize);
        log_set_archive_timestamp (playlistlog, config->logarchive);
        log_reopen (playlistlog);
    }
}
