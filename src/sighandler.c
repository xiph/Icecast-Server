#include <signal.h>

#include "thread.h"
#include "avl.h"
#include "log.h"
#include "httpp.h"

#include "global.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"

#include "sighandler.h"

#define CATMODULE "sighandler"

#ifndef _WIN32
void _sig_hup(int signo);
void _sig_die(int signo);
#endif

void sighandler_initialize(void)
{
#ifndef _WIN32
	signal(SIGHUP, _sig_hup);
	signal(SIGINT, _sig_die);
	signal(SIGTERM, _sig_die);
	signal(SIGPIPE, SIG_IGN);
#endif
}

#ifndef _WIN32

void _sig_hup(int signo)
{
	INFO1("Caught signal %d, rehashing config and reopening logfiles...", signo);

	/* reread config file */

	/* reopen logfiles */

#ifdef __linux__
	/* linux requires us to reattach the signal handler */
	signal(SIGHUP, _sig_hup);
#endif
}

void _sig_die(int signo)
{
	INFO1("Caught signal %d, shutting down...", signo);

	/* inform the server to start shutting down */
	global.running = ICE_HALTING;
}

#endif
