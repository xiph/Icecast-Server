#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"

#include "global.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"
#include "event.h"

#include "sighandler.h"

#define CATMODULE "sighandler"

#ifndef _WIN32
void _sig_hup(int signo);
void _sig_die(int signo);
#endif

int schedule_config_reread = 0;

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
    schedule_config_reread = 1;
    /* some OSes require us to reattach the signal handler */
    signal(SIGHUP, _sig_hup);
}

void _sig_die(int signo)
{
    INFO1("Caught signal %d, shutting down...", signo);

    /* inform the server to start shutting down */
    global.running = ICE_HALTING;
}

#endif
