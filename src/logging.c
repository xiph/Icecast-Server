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

#include "os.h"
#include "cfgfile.h"
#include "logging.h"
#include "util.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

/* the global log descriptors */
int errorlog = 0;
int accesslog = 0;

/* 
** ADDR USER AUTH DATE REQUEST CODE BYTES REFERER AGENT [TIME]
**
** ADDR = client->con->ip
** USER = -      
**      we should do this for real once we support authentication
** AUTH = -
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
    char reqbuf[1024];
    struct tm thetime;
    time_t now;
    time_t stayed;
    char *referrer, *user_agent;

    now = time(NULL);

    /* build the data */
    localtime_r (&now, &thetime);
    strftime (datebuf, sizeof(datebuf), LOGGING_FORMAT_CLF, &thetime);

    /* build the request */
    snprintf (reqbuf, sizeof(reqbuf), "%s %s %s/%s",
            httpp_getvar (client->parser, HTTPP_VAR_REQ_TYPE),
            httpp_getvar (client->parser, HTTPP_VAR_URI),
            httpp_getvar (client->parser, HTTPP_VAR_PROTOCOL),
            httpp_getvar (client->parser, HTTPP_VAR_VERSION));

    stayed = now - client->con->con_time;

    referrer = httpp_getvar (client->parser, "referer");
    if (referrer == NULL)
        referrer = "-";

    user_agent = httpp_getvar (client->parser, "user-agent");
    if (user_agent == NULL)
        user_agent = "-";

#ifdef HAVE_LOGGING_IP
    log_write_direct (accesslog, "%s - - [%s] \"%s\" %d %lld \"%s\" \"%s\" %d",
             client->con->ip,
             datebuf, reqbuf, client->respcode, client->con->sent_bytes,
             referrer, user_agent, (int)stayed);
#else
    log_write_direct (accesslog, "- - - [%s] \"%s\" %d %lld \"%s\" \"%s\" %d",
             datebuf, reqbuf, client->respcode, client->con->sent_bytes,
             referrer, user_agent, (int)stayed);
#endif
}



void restart_logging ()
{
    ice_config_t *config = config_get_config_unlocked();

    if (strcmp (config->error_log, "-"))
    {
        char fn_error[FILENAME_MAX];
        snprintf (fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
        log_set_filename (errorlog, fn_error);
        log_set_level (errorlog, config->loglevel);
        log_reopen (errorlog);
    }

    if (strcmp (config->access_log, "-"))
    {
        char fn_error[FILENAME_MAX];
        snprintf (fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);
        log_set_filename (accesslog, fn_error);
        log_reopen (accesslog);
    }
}
