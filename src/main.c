#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "avl.h"
#include "log.h"
#include "sock.h"
#include "resolver.h"
#include "httpp.h"


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

void _print_usage()
{
	printf("icecast 2.0 usage:\n");
	printf("\t-c <file>\t\tSpecify configuration file\n");
	printf("\n");
}

void _initialize_subsystems(void)
{
	log_initialize();
	thread_initialize();
	sock_initialize();
	resolver_initialize();
	config_initialize();
	connection_initialize();
	global_initialize();
	stats_initialize();
	refbuf_initialize();
}

void _shutdown_subsystems(void)
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

int _parse_config_file(int argc, char **argv, char *filename, int size)
{
	int i = 1;

	if (argc < 3) return 0;

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

	return 0;
}

int _start_logging(void)
{
	char fn_error[FILENAME_MAX];
	char fn_access[FILENAME_MAX];
	ice_config_t *config = config_get_config();

	snprintf(fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
	snprintf(fn_access, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);

	errorlog = log_open(fn_error);
	accesslog = log_open(fn_access);
	
	log_set_level(errorlog, 4);
	log_set_level(accesslog, 4);

	if (errorlog < 0)
		fprintf(stderr, "FATAL: could not open %s for error logging\n", fn_error);
	if (accesslog < 0)
		fprintf(stderr, "FATAL: could not open %s for access logging\n", fn_access);

	if (errorlog >= 0 && accesslog >= 0) return 1;
	
	return 0;
}

void _stop_logging(void)
{
	log_close(errorlog);
	log_close(accesslog);
}

int _setup_socket(void)
{
	ice_config_t *config;

	config = config_get_config();

	global.serversock = sock_get_server_socket(config->port, config->bind_address);
	if (global.serversock == SOCK_ERROR)
		return 0;
	
	return 1;
}

int _start_listening(void)
{
	if (sock_listen(global.serversock, ICE_LISTEN_QUEUE) == SOCK_ERROR)
		return 0;

	sock_set_blocking(global.serversock, SOCK_NONBLOCK);

	return 1;
}

/* this is the heart of the beast */
void _server_proc(void)
{
	if (!_setup_socket()) {
		ERROR1("Could not create listener socket on port %d", config_get_config()->port);
		return;
	}

	if (!_start_listening()) {
		ERROR0("Failed trying to listen on server socket");
		return;
	}

	connection_accept_loop();

	sock_close(global.serversock);
}

int main(int argc, char **argv)
{
	int res, ret;
	char filename[256];

	/* startup all the modules */
	_initialize_subsystems();

	/* setup the default signal handlers */
	sighandler_initialize();

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
		fprintf(stderr, "FATAL: -c option must have a filename\n");
		_print_usage();
		_shutdown_subsystems();
		return 1;
	}
	
	/* override config file options with commandline options */
	config_parse_cmdline(argc, argv);

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















