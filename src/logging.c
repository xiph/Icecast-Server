#include <stdio.h>
#include <time.h>

#include "thread.h"
#include "httpp.h"
#include "log.h"

#include "connection.h"
#include "refbuf.h"
#include "client.h"

#include "logging.h"

/* the global log descriptors */
int errorlog;
int accesslog;

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
	struct tm *thetime;
	time_t now;
	time_t stayed;

	now = time(NULL);

	/* build the data */
	/* TODO: localtime is not threadsafe on all platforms
	** we should probably use localtime_r if it's available
	*/
	PROTECT_CODE(thetime = localtime(&now); strftime(datebuf, 128, LOGGING_FORMAT_CLF, thetime))

	/* build the request */
	snprintf(reqbuf, 1024, "%s %s %s/%s", httpp_getvar(client->parser, HTTPP_VAR_REQ_TYPE), httpp_getvar(client->parser, HTTPP_VAR_URI),
		 httpp_getvar(client->parser, HTTPP_VAR_PROTOCOL), httpp_getvar(client->parser, HTTPP_VAR_VERSION));

	stayed = now - client->con->con_time;

	log_write_direct(accesslog, "%s - - [%s] \"%s\" %d %lld \"%s\" \"%s\" %d",
			 client->con->ip,
			 datebuf,
			 reqbuf,
			 client->respcode,
			 client->con->sent_bytes,
			 (httpp_getvar(client->parser, "referer") != NULL) ? httpp_getvar(client->parser, "referer") : "-",
			 (httpp_getvar(client->parser, "user-agent") != NULL) ? httpp_getvar(client->parser, "user-agent") : "-",
			 (int)stayed);
}







