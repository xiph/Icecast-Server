#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "avl.h"
#include "log.h"
#include "sock.h"
#include "resolver.h"
#include "httpp.h"

#ifdef CHUID
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#endif

#include "config.h"
#include "sighandler.h"

#include "global.h"
#include "os.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

#undef CATMODULE
#define CATMODULE "main"

static void _print_usage()
{
	printf("Usage:\n");
	printf("\ticecast -c <file>\t\tSpecify configuration file\n");
	printf("\n");
}

static void _initialize_subsystems(void)
{
	log_initialize();
	thread_initialize();
	sock_initialize();
	resolver_initialize();
	config_initialize();
	connection_initialize();
	global_initialize();
	refbuf_initialize();
}

static void _shutdown_subsystems(void)
{
	refbuf_shutdown();
	stats_shutdown();
	global_shutdown();
	connection_shutdown();
	config_shutdown();
	resolver_shutdown();
	sock_shutdown();
	thread_shutdown();
	log_shutdown();
}

static int _parse_config_file(int argc, char **argv, char *filename, int size)
{
	int i = 1;

	if (argc < 3) return -1;

	while (i < argc) {
		if (strcmp(argv[i], "-c") == 0) {
			if (i + 1 < argc) {
				strncpy(filename, argv[i + 1], size);
				return 1;
			} else {
				return -1;
			}
		}
		i++;
	}

	return -1;
}

static int _start_logging(void)
{
	char fn_error[FILENAME_MAX];
	char fn_access[FILENAME_MAX];
	ice_config_t *config = config_get_config();

	if(strcmp(config->error_log, "-")) {
        snprintf(fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
	    errorlog = log_open(fn_error);
    } else {
        errorlog = log_open_file(stderr);
    }
	if(strcmp(config->access_log, "-")) {
        snprintf(fn_access, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);
	    accesslog = log_open(fn_access);
    } else {
        accesslog = log_open_file(stderr);
    }
	
	log_set_level(errorlog, 4);
	log_set_level(accesslog, 4);

	if (errorlog < 0)
		fprintf(stderr, "FATAL: could not open %s for error logging\n", fn_error);
	if (accesslog < 0)
		fprintf(stderr, "FATAL: could not open %s for access logging\n", fn_access);

	if (errorlog >= 0 && accesslog >= 0) return 1;
	
	return 0;
}

static void _stop_logging(void)
{
	log_close(errorlog);
	log_close(accesslog);
}

static int _setup_socket(void)
{
	ice_config_t *config;

	config = config_get_config();

	global.serversock = sock_get_server_socket(config->port, config->bind_address);
	if (global.serversock == SOCK_ERROR)
		return 0;
	
	return 1;
}

static int _start_listening(void)
{
	if (sock_listen(global.serversock, ICE_LISTEN_QUEUE) == SOCK_ERROR)
		return 0;

	sock_set_blocking(global.serversock, SOCK_NONBLOCK);

	return 1;
}

/* bind the socket and start listening */
static void _server_proc_init(void)
{
	if (!_setup_socket()) {
		fprintf(stderr, "Could not create listener socket on port %d\n", config_get_config()->port);
		return;
	}

	if (!_start_listening()) {
		fprintf(stderr, "Failed trying to listen on server socket\n");
		return;
	}
}

/* this is the heart of the beast */
static void _server_proc(void)
{
	connection_accept_loop();

	sock_close(global.serversock);
}

/* chroot the process. Watch out - we need to do this before starting other
 * threads. Change uid as well, after figuring out uid _first_ */

static void _ch_root_uid_setup(void)
{
   ice_config_t *conf = config_get_config();
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
               fprintf(stdout, "Changed groupid to %i.\n", gid);
           else
               fprintf(stdout, "Error changing groupid: %s.\n", strerror(errno));
       }

       if(uid != -1) {
           if(!setuid(uid))
               fprintf(stdout, "Changed userid to %i.\n", uid);
           else
               fprintf(stdout, "Error changing userid: %s.\n", strerror(errno));
       }
   }
#endif
}

int main(int argc, char **argv)
{
	int res, ret;
	char filename[256];

	/* startup all the modules */
	_initialize_subsystems();

	/* parse the '-c icecast.xml' option
	** only, so that we can read a configfile
	*/
	res = _parse_config_file(argc, argv, filename, 256);
	if (res == 1) {
		/* parse the config file */
		ret = config_parse_file(filename);
		if (ret < 0) {
			fprintf(stderr, "FATAL: error parsing config file:");
			switch (ret) {
			case CONFIG_EINSANE:
				fprintf(stderr, "filename was null or blank\n");
				break;
			case CONFIG_ENOROOT:
				fprintf(stderr, "no root element found\n");
				break;
			case CONFIG_EBADROOT:
				fprintf(stderr, "root element is not <icecast>\n");
				break;
			default:
				fprintf(stderr, "parse error\n");
				break;
			}
		}
	} else if (res == -1) {
		_print_usage();
		_shutdown_subsystems();
		return 1;
	}
	
	/* override config file options with commandline options */
	config_parse_cmdline(argc, argv);

    _server_proc_init(); /* Bind socket, before we change userid */

    _ch_root_uid_setup(); /* Change user id and root if requested/possible */

    stats_initialize(); /* We have to do this later on because of threading */

#ifdef CHUID 
    /* We'll only have getuid() if we also have setuid(), it's reasonable to
     * assume */
    if(!getuid()) /* Running as root! Don't allow this */
    {
        fprintf(stderr, "WARNING: You should not run icecast2 as root\n");
        fprintf(stderr, "Use the changeowner directive in the config file\n");
        _shutdown_subsystems();
        return 1;
    }
#endif

    /* setup default signal handlers */
    sighandler_initialize();

	if (!_start_logging()) {
		fprintf(stderr, "FATAL: Could not start logging\n");
		_shutdown_subsystems();
		return 1;
	}

	INFO0("icecast server started");

	/* REM 3D Graphics */

	/* let her rip */
	global.running = ICE_RUNNING;
	_server_proc();

	INFO0("Shutting down");

	_stop_logging();

	_shutdown_subsystems();

	return 0;
}















