#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#endif

#include "log.h"

#define LOG_MAXLOGS 25
#define LOG_MAXLINELEN 1024

#ifdef _WIN32
#define mutex_t CRITICAL_SECTION
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#else
#define mutex_t pthread_mutex_t
#endif

static mutex_t _logger_mutex;
static int _initialized = 0;

typedef struct log_tag
{
	int in_use;

	int level;

	char *filename;
	FILE *logfile;
	
    char *buffer;
} log_t;

log_t loglist[LOG_MAXLOGS];

int _get_log_id();
void _release_log_id(int log_id);
static void _lock_logger();
static void _unlock_logger();

void log_initialize()
{
	int i;

	if (_initialized) return;

	for (i = 0; i < LOG_MAXLOGS; i++) {
		loglist[i].in_use = 0;
		loglist[i].level = 2;
		loglist[i].filename = NULL;
		loglist[i].logfile = NULL;
		loglist[i].buffer = NULL;
	}

	/* initialize mutexes */
#ifndef _WIN32
	pthread_mutex_init(&_logger_mutex, NULL);
#else
	InitializeCriticalSection(&_logger_mutex);
#endif

	_initialized = 1;
}

int log_open_file(FILE *file)
{
    int log_id;

    if(file == NULL) return LOG_EINSANE;

	log_id = _get_log_id();
	if (log_id < 0) return LOG_ENOMORELOGS;

	loglist[log_id].logfile = file;
	if (loglist[log_id].logfile != NULL) {
		loglist[log_id].filename = NULL;
	} else {
		_release_log_id(log_id);
		return LOG_ECANTOPEN;
	}

	return log_id;
}


int log_open(const char *filename)
{
	int ret;
    FILE *file;

	if (filename == NULL) return LOG_EINSANE;
	if (strcmp(filename, "") == 0) return LOG_EINSANE;
    
    file = fopen(filename, "a");

    ret = log_open_file(file);

	if(ret >= 0)
        setvbuf(file, NULL, IO_BUFFER_TYPE, 0);

    return ret;
}

int log_open_with_buffer(const char *filename, int size)
{
	/* not implemented */
	return LOG_ENOTIMPL;
}

void log_set_level(int log_id, int level)
{
	if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
	if (loglist[log_id].in_use == 0) return;

	loglist[log_id].level = level;
}

void log_flush(int log_id)
{
	if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
	if (loglist[log_id].in_use == 0) return;

	fflush(loglist[log_id].logfile);
}

void log_reopen(int log_id)
{
	/* not implemented yet */
}

void log_close(int log_id)
{
	if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
	if (loglist[log_id].in_use == 0) return;

	loglist[log_id].in_use = 0;
	loglist[log_id].level = 2;
	if (loglist[log_id].filename) free(loglist[log_id].filename);
	if (loglist[log_id].buffer) free(loglist[log_id].buffer);
	fclose(loglist[log_id].logfile);
	loglist[log_id].logfile = NULL;
}

void log_shutdown()
{
	/* destroy mutexes */
#ifndef _WIN32
	pthread_mutex_destroy(&_logger_mutex);
#else
	DeleteCriticalSection(&_logger_mutex);
#endif 

	_initialized = 0;
}

void log_write(int log_id, int priority, const char *cat, const char *func, 
        const char *fmt, ...)
{
        static char prior[4][5] = { "EROR\0", "WARN\0", "INFO\0", "DBUG\0" };
	char tyme[128];
	char pre[256];
	char line[LOG_MAXLINELEN];
	time_t now;
	va_list ap;

	if (log_id < 0) return;
    if (log_id > LOG_MAXLOGS) return; /* Bad log number */
	if (loglist[log_id].level < priority) return;
    if (priority > 4) return; /* Bad priority */


	va_start(ap, fmt);
	vsnprintf(line, LOG_MAXLINELEN, fmt, ap);

	now = time(NULL);

    /* localtime() isn't threadsafe, localtime_r isn't portable enough... */
    _lock_logger();
	strftime(tyme, 128, "[%Y-%m-%d  %H:%M:%S]", localtime(&now)); 
    _unlock_logger();

	snprintf(pre, 256, "%s %s%s", prior[priority-1], cat, func);

	fprintf(loglist[log_id].logfile, "%s %s %s\n", tyme, pre, line); 

	va_end(ap);
}

void log_write_direct(int log_id, const char *fmt, ...)
{
	char line[LOG_MAXLINELEN];
	va_list ap;

	if (log_id < 0) return;
	
	va_start(ap, fmt);
	vsnprintf(line, LOG_MAXLINELEN, fmt, ap);
	fprintf(loglist[log_id].logfile, "%s\n", line);
	va_end(ap);

	fflush(loglist[log_id].logfile);
}

int _get_log_id()
{
	int i;
	int id = -1;

	/* lock mutex */
	_lock_logger();

	for (i = 0; i < LOG_MAXLOGS; i++)
		if (loglist[i].in_use == 0) {
			loglist[i].in_use = 1;
			id = i;
			break;
		}

	/* unlock mutex */
	_unlock_logger();

	return id;
}

void _release_log_id(int log_id)
{
	/* lock mutex */
	_lock_logger();

	loglist[log_id].in_use = 0;

	/* unlock mutex */
	_unlock_logger();
}

static void _lock_logger()
{
#ifndef _WIN32
	pthread_mutex_lock(&_logger_mutex);
#else
	EnterCriticalSection(&_logger_mutex);
#endif
}

static void _unlock_logger()
{
#ifndef _WIN32
	pthread_mutex_unlock(&_logger_mutex);
#else
	LeaveCriticalSection(&_logger_mutex);
#endif	
}




