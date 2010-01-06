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

void fatal_error (const char *perr);

/* the global log descriptors */
int errorlog = 0;
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

    gmtime_r(&time1, &gmt);

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
void logging_access_id (access_log *accesslog, client_t *client)
{
    char datebuf[128];
    char reqbuf[1024];
    struct tm thetime;
    time_t now;
    time_t stayed;
    const char *referrer, *user_agent, *username, *ip = "-";

    if (httpp_getvar (client->parser, "__avoid_access_log"))
        return;

    now = time(NULL);

    localtime_r (&now, &thetime);
    /* build the data */
#ifdef _WIN32
    memset(datebuf, '\000', sizeof(datebuf));
    get_clf_time(datebuf, sizeof(datebuf)-1, &thetime);
#else
    strftime (datebuf, sizeof(datebuf), LOGGING_FORMAT_CLF, &thetime);
#endif
    /* build the request */
    snprintf (reqbuf, sizeof(reqbuf), "%s %s %s/%s",
            httpp_getvar (client->parser, HTTPP_VAR_REQ_TYPE),
            httpp_getvar (client->parser, HTTPP_VAR_URI),
            httpp_getvar (client->parser, HTTPP_VAR_PROTOCOL),
            httpp_getvar (client->parser, HTTPP_VAR_VERSION));

    stayed = now - client->connection.con_time;

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

    if (accesslog->log_ip)
        ip = client->connection.ip;
    log_write_direct (accesslog->logid,
            "%s - %s [%s] \"%s\" %d %" PRIu64 " \"%s\" \"%s\" %lu",
            ip, username,
            datebuf, reqbuf, client->respcode, client->connection.sent_bytes,
            referrer, user_agent, (unsigned long)stayed);
    client->respcode = -1;
}

void logging_access (client_t *client)
{
    ice_config_t *config = config_get_config();
    logging_access_id (&config->access_log, client);
    config_release_config ();
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


static int recheck_log_file (ice_config_t *config, int *id, const char *file)
{
    char fn [FILENAME_MAX];

    if (file == NULL || strcmp (file, "-") == 0)
    {
        log_close (*id);
        *id = -1;
        return 0;
    }
    snprintf (fn, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, file);
    if (*id < 0)
    {
        *id = log_open (fn);
        if (*id < 0)
        {
            char buf[1024];
            snprintf (buf,1024, "FATAL: could not open log %s: %s", fn, strerror(errno));
            fatal_error (buf);
            return -1;
        }
        return 0;
    }
    log_set_filename (*id, fn);
    log_reopen (*id);
    return 0;
}


int restart_logging (ice_config_t *config)
{
    ice_config_t *current = config_get_config_unlocked();
    mount_proxy *m;
    int ret = 0;

    config->error_log.logid = current->error_log.logid;
    config->access_log.logid = current->access_log.logid;
    config->playlist_log.logid = current->playlist_log.logid;

    if (recheck_log_file (config, &config->error_log.logid, config->error_log.name) < 0)
        ret = -1;
    else
    {
        log_set_trigger (config->error_log.logid, config->error_log.size);
        log_set_lines_kept (config->error_log.logid, config->error_log.display);
        log_set_archive_timestamp (config->error_log.logid, config->error_log.archive);
        log_set_level (config->error_log.logid, config->error_log.level);
    }
    thread_use_log_id (config->error_log.logid);
    errorlog = config->error_log.logid; /* value stays static so avoid taking the config lock */

    if (recheck_log_file (config, &config->access_log.logid, config->access_log.name) < 0)
        ret = -1;
    else
    {
        log_set_trigger (config->access_log.logid, config->access_log.size);
        log_set_lines_kept (config->access_log.logid, config->access_log.display);
        log_set_archive_timestamp (config->access_log.logid, config->access_log.archive);
        log_set_level (config->access_log.logid, 4);
    }

    if (recheck_log_file (config, &config->playlist_log.logid, config->playlist_log.name) < 0)
        ret = -1;
    else
    {
        log_set_trigger (config->playlist_log.logid, config->playlist_log.size);
        log_set_lines_kept (config->playlist_log.logid, config->playlist_log.display);
        log_set_archive_timestamp (config->playlist_log.logid, config->playlist_log.archive);
        log_set_level (config->playlist_log.logid, 4);
    }
    playlistlog = config->playlist_log.logid;
    m = config->mounts;
    while (m)
    {
        if (recheck_log_file (config, &m->access_log.logid, m->access_log.name) < 0)
            ret = -1;
        else
        {
            log_set_trigger (m->access_log.logid, m->access_log.size);
            log_set_lines_kept (m->access_log.logid, m->access_log.display);
            log_set_archive_timestamp (m->access_log.logid, m->access_log.archive);
            log_set_level (m->access_log.logid, 4);
        }
        m = m->next;
    }
    return ret;
}


int start_logging (ice_config_t *config)
{
    if (strcmp (config->error_log.name, "-") == 0)
        errorlog = log_open_file (stderr);
    if (strcmp(config->access_log.name, "-") == 0)
        config->access_log.logid = log_open_file (stderr);
    return restart_logging (config);
}


void stop_logging(void)
{
    ice_config_t *config = config_get_config_unlocked();
    log_close (errorlog);
    log_close (config->access_log.logid);
    log_close (config->playlist_log.logid);
}

