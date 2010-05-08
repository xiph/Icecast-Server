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

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef WIN32
#define _WIN32_WINNT 0x0400
/* For getpid() */
#include <process.h>
#include <windows.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "net/resolver.h"
#include "httpp/httpp.h"

#ifdef CHUID
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#endif

#include "cfgfile.h"
#include "sighandler.h"

#include "global.h"
#include "compat.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "slave.h"
#include "stats.h"
#include "logging.h"
#include "xslt.h"
#include "fserve.h"
#include "auth.h"

#include <libxml/xmlmemory.h>

#undef CATMODULE
#define CATMODULE "main"

static int background;
static char *pidfile = NULL;

#define _fatal_error fatal_error
void fatal_error (const char *perr)
{
#ifdef WIN32_SERVICE
    MessageBox(NULL, perr, "Error", MB_SERVICE_NOTIFICATION);
#elif defined(WIN32)
    MessageBox(NULL, perr, "Error", MB_OK);
#else
    ERROR1("%s", perr);
#endif
}

static void _print_usage(void)
{
    printf("%s\n\n", ICECAST_VERSION_STRING);
    printf("usage: icecast [-b -v] -c <file>\n");
    printf("options:\n");
    printf("\t-c <file>\tSpecify configuration file\n");
    printf("\t-v\t\tDisplay version info\n");
    printf("\t-b\t\tRun icecast in the background\n");
    printf("\n");
}


void _initialize_subsystems(void)
{
    log_initialize();
    errorlog = log_open_file (stderr);
    thread_initialize();
    sock_initialize();
    resolver_initialize();
    config_initialize();
    connection_initialize();
    global_initialize();
    refbuf_initialize();

    stats_initialize();
    xslt_initialize();
#ifdef HAVE_CURL_GLOBAL_INIT
    curl_global_init (CURL_GLOBAL_ALL);
#endif
}

void _shutdown_subsystems(void)
{
    connection_shutdown();
    auth_shutdown();
    slave_shutdown();
    fserve_shutdown();
    stats_shutdown();
    stop_logging();

    config_shutdown();
    refbuf_shutdown();
    resolver_shutdown();
    sock_shutdown();

    DEBUG0 ("library cleanups");
#ifdef HAVE_CURL
    curl_global_cleanup();
#endif

    /* Now that these are done, we can stop the loggers. */
    log_shutdown();
    xslt_shutdown();
    thread_shutdown();
    global_shutdown();
}

static int _parse_config_opts(int argc, char **argv, char *filename, int size)
{
    int i = 1;
    int config_ok = 0;

    background = 0;
    if (argc < 2) return -1;

    while (i < argc) {
        if (strcmp(argv[i], "-b") == 0) {
#ifndef WIN32
            pid_t pid;
            fprintf(stdout, "Starting icecast2\nDetaching from the console\n");

            pid = fork();

            if (pid > 0) {
                /* exit the parent */
                exit(0);
            }
            else if(pid < 0) {
                fprintf(stderr, "FATAL: Unable to fork child!");
                exit(1);
            }
            background = 1;
#endif
        }
        if (strcmp(argv[i], "-v") == 0) {
            fprintf(stdout, "%s\n", ICECAST_VERSION_STRING);
            exit(0);
        }

        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                strncpy(filename, argv[i + 1], size-1);
                filename[size-1] = 0;
                config_ok = 1;
            } else {
                return -1;
            }
        }
        i++;
    }

    if(config_ok)
        return 1;
    else
        return -1;
}


/* bind the socket and start listening */
static int _server_proc_init(void)
{
    ice_config_t *config = config_get_config_unlocked();

    if (config->chuid)
        connection_setup_sockets (config);

    /* recreate the pid file */
    if (config->pidfile)
    {
        FILE *f;
        pidfile = strdup (config->pidfile);
        if (pidfile && (f = fopen (config->pidfile, "w")) != NULL)
        {
            fprintf (f, "%d\n", (int)getpid());
            fclose (f);
        }
    }

    return 1;
}

/* this is the heart of the beast */
static void _server_proc(void)
{
    if (background)
    {
        fclose (stdin);
        fclose (stdout);
        fclose (stderr);
    }
    slave_initialize();

    connection_setup_sockets (NULL);
}

/* chroot the process. Watch out - we need to do this before starting other
 * threads. Change uid as well, after figuring out uid _first_ */

static void _ch_root_uid_setup(void)
{
   ice_config_t *conf = config_get_config_unlocked();
#ifdef CHUID
   struct passwd *user;
   struct group *group;
   uid_t uid=-1;
   gid_t gid=-1;

   if(conf->chuid)
   {
       if(conf->user) {
           user = getpwnam(conf->user);
           if(user)
               uid = user->pw_uid;
           else
               fprintf(stderr, "Couldn't find user \"%s\" in password file\n", conf->user);
       }
       if(conf->group) {
           group = getgrnam(conf->group);

           if(group)
               gid = group->gr_gid;
           else
               fprintf(stderr, "Couldn't find group \"%s\" in groups file\n", conf->group);
       }
   }
#endif

#ifdef CHROOT
   if (conf->chroot)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Cannot change server root unless running as root.\n");
           return;
       }
       if(chroot(conf->base_dir))
       {
           fprintf(stderr,"WARNING: Couldn't change server root: %s\n", strerror(errno));
           return;
       }
       else
           fprintf(stdout, "Changed root successfully to \"%s\".\n", conf->base_dir);

   }   
#endif
#ifdef CHUID

   if(conf->chuid)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Can't change user id unless you are root.\n");
           return;
       }

       if(gid != -1) {
           if(!setgid(gid))
               fprintf(stdout, "Changed groupid to %i.\n", (int)gid);
           else
               fprintf(stdout, "Error changing groupid: %s.\n", strerror(errno));
       }

       if(uid != -1) {
           if(!setuid(uid))
               fprintf(stdout, "Changed userid to %i.\n", (int)uid);
           else
               fprintf(stdout, "Error changing userid: %s.\n", strerror(errno));
       }
   }
#endif
}

#ifdef WIN32_SERVICE
int mainService(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int res, ret;
    char filename[512];
    char pbuf[1024];

    /* parse the '-c icecast.xml' option
    ** only, so that we can read a configfile
    */
    res = _parse_config_opts(argc, argv, filename, 512);
    if (res == 1) {
#if !defined(_WIN32) || defined(_CONSOLE)
        /* startup all the modules */
        _initialize_subsystems();
#endif
        /* parse the config file */
        config_get_config();
        ret = config_initial_parse_file(filename);
        config_release_config();
        if (ret < 0) {
            snprintf(pbuf, sizeof(pbuf)-1, 
                "FATAL: error parsing config file (%s)", filename);
            _fatal_error(pbuf);
            switch (ret) {
            case CONFIG_EINSANE:
                _fatal_error("filename was null or blank");
                break;
            case CONFIG_ENOROOT:
                _fatal_error("no root element found");
                break;
            case CONFIG_EBADROOT:
                _fatal_error("root element is not <icecast>");
                break;
            default:
                _fatal_error("XML config parsing error");
                break;
            }
#if !defined(_WIN32) || defined(_CONSOLE)
            _shutdown_subsystems();
#endif
            return -1;
        }
    } else if (res == -1) {
        _print_usage();
        return -1;
    }
    
    /* override config file options with commandline options */
    config_parse_cmdline(argc, argv);

    /* Bind socket, before we change userid */
    if(!_server_proc_init()) {
        _fatal_error("Server startup failed. Exiting");
        _shutdown_subsystems();
        return -1;
    }

    _ch_root_uid_setup(); /* Change user id and root if requested/possible */
    fserve_initialize();

#ifdef CHUID 
    /* We'll only have getuid() if we also have setuid(), it's reasonable to
     * assume */
    if(!getuid()) /* Running as root! Don't allow this */
    {
        fprintf(stderr, "ERROR: You should not run icecast2 as root\n");
        fprintf(stderr, "Use the changeowner directive in the config file\n");
        _shutdown_subsystems();
        return 1;
    }
#endif

    /* setup default signal handlers */
    sighandler_initialize();

    if (start_logging (config_get_config_unlocked()) < 0)
    {
        _fatal_error("FATAL: Could not start logging");
        _shutdown_subsystems();
        return -1;
    }

    INFO1 ("%s server started", ICECAST_VERSION_STRING);

    /* REM 3D Graphics */

    /* let her rip */
    global.running = ICE_RUNNING;

    /* Do this after logging init */
    auth_initialise ();

    _server_proc();

    INFO0("Shutting down");
#if !defined(_WIN32) || defined(_CONSOLE)
    _shutdown_subsystems();
#endif
    if (pidfile)
    {
        remove (pidfile);
        free (pidfile);
    }

    return 0;
}


