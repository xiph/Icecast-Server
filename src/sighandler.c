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
#include "event.h"

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
    /* We do this elsewhere because it's a bad idea to hang around for too
     * long re-reading an entire config file inside a signal handler. Bad
     * practice.
     */

    INFO1("Caught signal %d, scheduling config reread ...", 
            signo);

    /* reread config file */

    connection_inject_event(EVENT_CONFIG_READ, NULL);
    
    /* reopen logfiles (TODO: We don't do this currently) */

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
