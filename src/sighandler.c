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
 * Copyright 2011-2015, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"
#include "common/httpp/httpp.h"

#include "global.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h"

#define CATMODULE "sighandler"

#ifndef _WIN32
sig_atomic_t caught_sig_die = 0;
sig_atomic_t caught_sig_hup = 0;

void _sig_hup(int signo);
void _sig_die(int signo);
void _sig_ignore(int signo);
#endif

void sighandler_initialize(void)
{
#ifndef _WIN32
    signal(SIGHUP, _sig_hup);
    signal(SIGINT, _sig_die);
    signal(SIGTERM, _sig_die);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, _sig_ignore);
#endif
}

void sighandler_handle_pending(void)
{
#ifndef _WIN32
    int signo = caught_sig_hup;

    if (signo) {
        ICECAST_LOG_INFO("Caught signal %d, scheduling config re-read...", signo);

        /* inform the server to reload configuration */
        global_lock();
        global . schedule_config_reread = 1;
        global_unlock();

        caught_sig_hup = 0;
    }

    signo = caught_sig_die;
    if (signo) {
        ICECAST_LOG_INFO("Caught signal %d, shutting down...", signo);

        /* inform the server to start shutting down */
        global_lock();
        global . running = ICECAST_HALTING;
        global_unlock();

        caught_sig_die = 0;
    }
#endif
}

#ifndef _WIN32

void _sig_ignore(int signo)
{
    signal(signo, _sig_ignore);
}

void _sig_hup(int signo)
{
    caught_sig_hup = signo;

    /* some OSes require us to reattach the signal handler */
    signal(SIGHUP, _sig_hup);
}

void _sig_die(int signo)
{
    caught_sig_die = signo;
}

#endif
