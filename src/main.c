/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org,
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>,
 *                      and others (see AUTHORS for details).
 * Copyright 2011-2022, Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 * Copyright 2014,      Thomas B. Ruecker <thomas@ruecker.fi>.
 */

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_CURL
#include "curl.h"
#endif

#ifdef WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
/* For getpid() */
#include <process.h>
#include <windows.h>
#define snprintf _snprintf
#define getpid _getpid
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#include "common/thread/thread.h"
#include "common/net/sock.h"
#include "common/net/resolver.h"
#include "common/httpp/httpp.h"

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_GRP_H
#include <grp.h>
#endif
#if HAVE_PWD_H
#include <pwd.h>
#endif

#include "main.h"
#include "cfgfile.h"
#include "util.h"
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
#include "yp.h"
#include "auth.h"
#include "event.h"
#include "listensocket.h"
#include "fastevent.h"
#include "prng.h"
#include "navigation.h"

#include <libxml/xmlmemory.h>

#undef CATMODULE
#define CATMODULE "main"

static bool background;
static char *pidfile = NULL;

static void pidfile_update(ice_config_t *config, int always_try);

static void _fatal_error(const char *perr)
{
#ifdef WIN32_SERVICE
    MessageBox(NULL, perr, "Error", MB_SERVICE_NOTIFICATION);
#elif defined(WIN32)
    MessageBox(NULL, perr, "Error", MB_OK);
#else
    fprintf(stdout, "%s\n", perr);
#endif
}

static void _print_usage(void)
{
    printf("%s\n\n", ICECAST_VERSION_STRING);
    printf("usage: icecast [-b] -c <file>\n");
    printf("or   : icecast {-v|--version}\n");
    printf("options:\n");
    printf("\t-c <file>       Specify configuration file\n");
    printf("\t-v or --version Display version info\n");
    printf("\t-b              Run icecast in the background\n");
    printf("\n");
}

static void _stop_logging(void)
{
    log_close(errorlog);
    log_close(accesslog);
    log_close(playlistlog);
}

#ifndef FASTEVENT_ENABLED
static void __fastevent_cb(const void *userdata, fastevent_type_t type, fastevent_flag_t flags, fastevent_datatype_t datatype, va_list ap)
{
    event_t *event;

    if (datatype != FASTEVENT_DATATYPE_EVENT)
        return;

    event = va_arg(ap, event_t*);

    if (event == NULL) {
        ICECAST_LOG_DEBUG("event=%p", event);
    } else {
        ICECAST_LOG_DEBUG("event=%p{.trigger='%s', ...}", event, event->trigger);
    }
}

static refobject_t fastevent_reg;
#endif

static void initialize_subsystems(void)
{
    log_initialize();
    thread_initialize();
    prng_initialize();
    navigation_initialize();
    global_initialize();
#ifndef FASTEVENT_ENABLED
    fastevent_initialize();
    fastevent_reg = fastevent_register(FASTEVENT_TYPE_SLOWEVENT, __fastevent_cb, NULL, NULL);
#endif
    sock_initialize();
    resolver_initialize();
    config_initialize();
    tls_initialize();
    client_initialize();
    connection_initialize();
    refbuf_initialize();

    xslt_initialize();
#ifdef HAVE_CURL
    icecast_curl_initialize();
#endif
}

static void shutdown_subsystems(void)
{
    event_shutdown();
    fserve_shutdown();
    refbuf_shutdown();
    slave_shutdown();
    auth_shutdown();
    yp_shutdown();
    stats_shutdown();

    connection_shutdown();
    client_shutdown();
    tls_shutdown();
    prng_deconfigure();
    config_shutdown();
    resolver_shutdown();
    sock_shutdown();
#ifndef FASTEVENT_ENABLED
    refobject_unref(fastevent_reg);
    fastevent_shutdown();
#endif
    navigation_shutdown();
    prng_shutdown();
    global_shutdown();
    thread_shutdown();

#ifdef HAVE_CURL
    icecast_curl_shutdown();
#endif

    /* Now that these are done, we can stop the loggers. */
    _stop_logging();
    log_shutdown();
    xslt_shutdown();
}

void main_config_reload(ice_config_t *config)
{
    ICECAST_LOG_DEBUG("Reloading configuration.");
    pidfile_update(config, 0);
}

static bool _parse_config_opts(int argc, char **argv, char *filename, size_t size)
{
    int i;
    bool config_ok = false;

    background = false;
    if (argc < 2) {
        if (filename[0] != 0) {
            /* We have a default filename, so we can work with no options. */
            return true;
        } else {
            /* We need at least a config filename. */
            return false;
        }
    }

    for (i = 1; i < argc; i++) {
        const char *opt = argv[i];

        if (strcmp(opt, "-b") == 0) {
#ifndef WIN32
            pid_t pid;
            fprintf(stdout, "Starting icecast2\nDetaching from the console\n");

            pid = fork();

            if (pid > 0) {
                /* exit the parent */
                exit(0);
            } else if (pid < 0) {
                fprintf(stderr, "FATAL: Unable to fork child!\n");
                exit(1);
            }
            background = true;
#endif
        } else if (strcmp(opt, "-v") == 0 || strcmp(opt, "--version") == 0) {
            fprintf(stdout, "%s\n", ICECAST_VERSION_STRING);
            exit(0);
        } else if (strcmp(opt, "-c") == 0) {
            if ((i + 1) < argc) {
                strncpy(filename, argv[++i], size-1);
                filename[size-1] = 0;
                config_ok = true;
            } else {
                return false;
            }
        } else {
            fprintf(stderr, "FATAL: Invalid option: %s\n", opt);
            return false;
        }
    }

    return config_ok;
}

static int _start_logging_stdout(void) {
    errorlog = log_open_file(stderr);
    if ( errorlog < 0 )
        return 0;

    log_set_level(errorlog, ICECAST_LOGLEVEL_WARN);

    return 1;
}

static int _start_logging(void)
{
    char fn_error[FILENAME_MAX];
    char fn_access[FILENAME_MAX];
    char fn_playlist[FILENAME_MAX];
    char buf[FILENAME_MAX+1024];
    int log_to_stderr;

    ice_config_t *config = config_get_config_unlocked();

    if(strcmp(config->error_log, "-") == 0) {
        /* this is already in place because of _start_logging_stdout() */
    } else {
        snprintf(fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
        errorlog = log_open(fn_error);
        log_to_stderr = 0;
        if (config->logsize)
            log_set_trigger (errorlog, config->logsize);
        log_set_archive_timestamp(errorlog, config->logarchive);
    }

    if (errorlog < 0) {
        buf[sizeof(buf)-1] = 0;
        snprintf(buf, sizeof(buf)-1,
                "FATAL: could not open error logging (%s): %s",
                log_to_stderr?"standard error":fn_error,
                strerror(errno));
        _fatal_error(buf);
    }
    log_set_level(errorlog, config->loglevel);

    if(strcmp(config->access_log, "-") == 0) {
        accesslog = log_open_file(stderr);
        log_to_stderr = 1;
    } else {
        snprintf(fn_access, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);
        accesslog = log_open(fn_access);
        log_to_stderr = 0;
        if (config->logsize)
            log_set_trigger (accesslog, config->logsize);
        log_set_archive_timestamp(accesslog, config->logarchive);
    }

    if (accesslog < 0) {
        buf[sizeof(buf)-1] = 0;
        snprintf(buf, sizeof(buf) - 1,
            "FATAL: could not open access logging (%s): %s",
            log_to_stderr ? "standard error" : fn_access,
            strerror(errno));
        _fatal_error(buf);
    }

    if(config->playlist_log) {
        snprintf(fn_playlist, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->playlist_log);
        playlistlog = log_open(fn_playlist);
        if (playlistlog < 0) {
            buf[sizeof(buf)-1] = 0;
            snprintf(buf, sizeof(buf)-1,
                "FATAL: could not open playlist logging (%s): %s",
                log_to_stderr?"standard error":fn_playlist,
                strerror(errno));
            _fatal_error(buf);
        }
        log_to_stderr = 0;
        if (config->logsize)
            log_set_trigger (playlistlog, config->logsize);
        log_set_archive_timestamp(playlistlog, config->logarchive);
    } else {
        playlistlog = -1;
    }

    log_set_level(errorlog, config->loglevel);
    log_set_level(accesslog, 4);
    log_set_level(playlistlog, 4);

    log_set_lines_kept(errorlog, config->error_log_lines_kept);
    log_set_lines_kept(accesslog, config->access_log_lines_kept);
    log_set_lines_kept(playlistlog, config->playlist_log_lines_kept);

    if (errorlog >= 0 && accesslog >= 0) return 1;

    return 0;
}

static void pidfile_update(ice_config_t *config, int always_try)
{
    char *newpidfile = NULL;

    if (config->pidfile) {
        FILE *f;

        /* check if the file actually changed */
        if (pidfile && strcmp(pidfile, config->pidfile) == 0)
            return;

        ICECAST_LOG_DEBUG("New pidfile on %H", config->pidfile);

        if (!always_try) {
            if (config->chuid) {
                ICECAST_LOG_ERROR("Can not write new pidfile, changeowner in effect.");
                return;
            }

            if (config->chroot) {
                ICECAST_LOG_ERROR("Can not write new pidfile, chroot in effect.");
                return;
            }
        }

        newpidfile = strdup(config->pidfile);
        if (!newpidfile) {
            ICECAST_LOG_ERROR("Can not allocate memory for pidfile filename. BAD.");
            return;
        }

        f = fopen(newpidfile, "w");
        if (!f) {
            free(newpidfile);
            ICECAST_LOG_ERROR("Can not open new pidfile for writing.");
            return;
        }

        fprintf(f, "%lld\n", (long long int)getpid());
        fclose(f);

        ICECAST_LOG_INFO("pidfile %H updated.", pidfile);
    }

    if (newpidfile != pidfile) {
        if (pidfile)
            remove(pidfile);
        free(pidfile);
        pidfile = newpidfile;
    }
}

/* bind the socket and start listening */
static int _server_proc_init(void)
{
    ice_config_t *config = config_get_config_unlocked();

    connection_setup_sockets(config);

    if (listensocket_container_sockcount(global.listensockets) < 1) {
        ICECAST_LOG_ERROR("Can not listen on any sockets.");
        return 0;
    }

    pidfile_update(config, 1);

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
    connection_accept_loop();

    connection_setup_sockets (NULL);
}

/* chroot the process. Watch out - we need to do this before starting other
 * threads. Change uid as well, after figuring out uid _first_ */
#if defined(HAVE_SETUID) || defined(HAVE_CHROOT) || defined(HAVE_SETUID)
static void _ch_root_uid_setup(void)
{
   ice_config_t *conf = config_get_config_unlocked();
#ifdef HAVE_SETUID
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

#if HAVE_CHROOT
   if (conf->chroot)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Cannot change server root unless running as root.\n");
       }
       if(chroot(conf->base_dir) == -1 || chdir("/") == -1)
       {
           fprintf(stderr,"WARNING: Couldn't change server root: %s\n", strerror(errno));
           return;
       }
       else
           fprintf(stdout, "Changed root successfully to \"%s\".\n", conf->base_dir);

   }
#endif

#if HAVE_SETUID
   if(conf->chuid)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Can't change user id unless you are root.\n");
           return;
       }

       if(uid != (uid_t)-1 && gid != (gid_t)-1) {
#ifdef HAVE_SETRESGID
           if(!setresgid(gid, gid, gid)) {
#else
           if(!setgid(gid)) {
#endif
               fprintf(stdout, "Changed groupid to %i.\n", (int)gid);
           } else {
               fprintf(stdout, "Error changing groupid: %s.\n", strerror(errno));
           }
           if(!initgroups(conf->user, gid))
               fprintf(stdout, "Changed supplementary groups based on user: %s.\n", conf->user);
           else
               fprintf(stdout, "Error changing supplementary groups: %s.\n", strerror(errno));
#ifdef HAVE_SETRESUID
           if(!setresuid(uid, uid, uid)) {
#else
           if(!setuid(uid)) {
#endif
               fprintf(stdout, "Changed userid to %i.\n", (int)uid);
           } else {
               fprintf(stdout, "Error changing userid: %s.\n", strerror(errno));
           }
       }
   }
#endif
}
#endif

static inline void __log_system_name(void) {
    char hostname[80] = "(unknown)";
    char system[1024] = "(unknown)";
    int have_hostname = 0;
#ifdef HAVE_UNAME
    struct utsname utsname;
#endif
    ice_config_t *config;

#ifdef HAVE_GETHOSTNAME
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "(unknown)", sizeof(hostname));
    } else {
        have_hostname = 1;
    }
#endif
#ifdef HAVE_UNAME
    if(uname(&utsname) == 0) {
        snprintf(system, sizeof(system), "%s %s, %s, %s, %s",
                 utsname.sysname, utsname.release, utsname.nodename, utsname.version, utsname.machine);
        if (!have_hostname) {
            strncpy(hostname, utsname.nodename, sizeof(hostname));
            have_hostname = 1;
        }
    }
#elif defined(WIN32)
    strncpy(system, "MS Windows", sizeof(system));
#endif

   ICECAST_LOG_INFO("Running on %s; OS: %s; Address Bits: %i", hostname, system, sizeof(void*)*8);

   config = config_get_config();
   if (have_hostname) {
       if ((config->config_problems & CONFIG_PROBLEM_HOSTNAME) && util_hostcheck(hostname) == HOSTCHECK_SANE) {
           ICECAST_LOG_WARN("Hostname is not set to anything useful in <hostname>, Consider setting it to the system's name \"%s\".", hostname);
       }
   }

   ICECAST_LOG_INFO("From configuration: Our hostname is %#H, located % #H, with admin contact % #H", config->hostname, config->location, config->admin);
   config_release_config();
}

#ifdef WIN32_SERVICE
int mainService(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int ret;
#ifdef ICECAST_DEFAULT_CONFIG
    char filename[512] = ICECAST_DEFAULT_CONFIG;
#else
    char filename[512] = "";
#endif
    char pbuf[1024];
    ice_config_t *config;

    /* parse the '-c icecast.xml' option
    ** only, so that we can read a configfile
    */
    if (_parse_config_opts(argc, argv, filename, sizeof(filename))) {
#if !defined(_WIN32) || defined(_CONSOLE) || defined(__MINGW32__) || defined(__MINGW64__)
        /* startup all the modules */
        initialize_subsystems();
        if (!_start_logging_stdout()) {
            _fatal_error("FATAL: Could not start logging on stderr.");
            shutdown_subsystems();
            return 1;
        }
#endif
        /* parse the config file */
        config_get_config();
        ret = config_initial_parse_file(filename);
        config_release_config();
        if (ret < 0) {
            memset(pbuf, '\000', sizeof(pbuf));
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
#if !defined(_WIN32) || defined(_CONSOLE) || defined(__MINGW32__) || defined(__MINGW64__)
            shutdown_subsystems();
#endif
            return 1;
        }
    } else {
        _print_usage();
        return 1;
    }

    /* override config file options with commandline options */
    config_parse_cmdline(argc, argv);

    /* Bind socket, before we change userid */
    if(!_server_proc_init()) {
        _fatal_error("Server startup failed. Exiting");
        shutdown_subsystems();
        return 1;
    }

#if defined(HAVE_SETUID) || defined(HAVE_CHROOT) || defined(HAVE_SETUID)
    _ch_root_uid_setup(); /* Change user id and root if requested/possible */
#endif

    if (!_start_logging()) {
        _fatal_error("FATAL: Could not start logging");
        shutdown_subsystems();
        return 1;
    }

    config = config_get_config();
    prng_configure(config);
    config_release_config();

    stats_initialize(); /* We have to do this later on because of threading */
    fserve_initialize(); /* This too */

#ifdef HAVE_SETUID
    /* We'll only have getuid() if we also have setuid(), it's reasonable to
     * assume */
    if(!getuid() && getpid() != 1) /* Running as root! Don't allow this */
    {
        fprintf(stderr, "ERROR: You should not run icecast2 as root\n");
        fprintf(stderr, "Use the changeowner directive in the config file\n");
        shutdown_subsystems();
        return 1;
    }
#endif

    /* setup default signal handlers */
    sighandler_initialize();

    ICECAST_LOG_INFO("%s server started", ICECAST_VERSION_STRING);
    ICECAST_LOG_INFO("Server's PID is %lli", (long long int)getpid());
    __log_system_name();

    /* REM 3D Graphics */

    /* let her rip */
    global.running = ICECAST_RUNNING;

    /* Startup yp thread */
    yp_initialize();

    /* Do this after logging init */
    slave_initialize();
    auth_initialise ();
    event_initialise();

    event_emit_global("icecast-start");
    _server_proc();
    event_emit_global("icecast-stop");

    ICECAST_LOG_INFO("Shutting down");
#if !defined(_WIN32) || defined(_CONSOLE) || defined(__MINGW32__) || defined(__MINGW64__)
    shutdown_subsystems();
#endif
    if (pidfile)
    {
        remove (pidfile);
        free (pidfile);
    }

    return 0;
}


