/* 
** Logging framework.
**
** This program is distributed under the GNU General Public License, version 2.
** A copy of this license is included with this source.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


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

    unsigned level;

    char *filename;
    FILE *logfile;
    unsigned size;
    unsigned trigger_level;
    
    char *buffer;
} log_t;

static log_t loglist[LOG_MAXLOGS];

static int _get_log_id();
static void _release_log_id(int log_id);
static void _lock_logger();
static void _unlock_logger();


static int _log_open (int id)
{
    if (loglist [id] . in_use == 0)
        return 0;

    /* check for cases where an open of the logfile is wanted */
    if (loglist [id] . logfile == NULL || 
       (loglist [id] . trigger_level && loglist [id] . size > loglist [id] . trigger_level))
    {
        if (loglist [id] . filename)  /* only re-open files where we have a name */
        {
            struct stat st;

            if (loglist [id] . logfile)
            {
                char new_name [255];
                fclose (loglist [id] . logfile);
                loglist [id] . logfile = NULL;
                /* simple rename, but could use time providing locking were used */
                snprintf (new_name,  sizeof(new_name), "%s.old", loglist [id] . filename);
                rename (loglist [id] . filename, new_name);
            }
            loglist [id] . logfile = fopen (loglist [id] . filename, "a");
            if (loglist [id] . logfile == NULL)
                return 0;
            setvbuf (loglist [id] . logfile, NULL, IO_BUFFER_TYPE, 0);
            if (stat (loglist [id] . filename, &st) < 0)
                loglist [id] . size = 0;
            else
                loglist [id] . size = st.st_size;
        }
        else
            loglist [id] . size = 0;
    }
    return 1;
}

void log_initialize()
{
    int i;

    if (_initialized) return;

    for (i = 0; i < LOG_MAXLOGS; i++) {
        loglist[i].in_use = 0;
        loglist[i].level = 2;
        loglist[i].size = 0;
        loglist[i].trigger_level = 0;
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
    loglist[log_id].filename = NULL;
    loglist[log_id].size = 0;

    return log_id;
}


int log_open(const char *filename)
{
    int id;
    FILE *file;

    if (filename == NULL) return LOG_EINSANE;
    if (strcmp(filename, "") == 0) return LOG_EINSANE;
    
    file = fopen(filename, "a");

    id = log_open_file(file);

    if (id >= 0)
    {
        struct stat st;

        setvbuf (loglist [id] . logfile, NULL, IO_BUFFER_TYPE, 0);
        loglist [id] . filename = strdup (filename);
        if (stat (loglist [id] . filename, &st) == 0)
            loglist [id] . size = st.st_size;
    }

    return id;
}


/* set the trigger level to trigger, represented in kilobytes */
void log_set_trigger(int id, unsigned trigger)
{
    if (id >= 0 && id < LOG_MAXLOGS && loglist [id] . in_use)
    {
         loglist [id] . trigger_level = trigger*1024;
    }
}


int log_set_filename(int id, const char *filename)
{
    if (id < 0 || id >= LOG_MAXLOGS)
        return LOG_EINSANE;
    if (!strcmp(filename, "") || loglist [id] . in_use == 0)
        return LOG_EINSANE;
     _lock_logger();
    if (loglist [id] . filename)
        free (loglist [id] . filename);
    if (filename)
        loglist [id] . filename = strdup (filename);
    else
        loglist [id] . filename = NULL;
     _unlock_logger();
    return id;
}


int log_open_with_buffer(const char *filename, int size)
{
    /* not implemented */
    return LOG_ENOTIMPL;
}


void log_set_level(int log_id, unsigned level)
{
    if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
    if (loglist[log_id].in_use == 0) return;

    loglist[log_id].level = level;
}

void log_flush(int log_id)
{
    if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
    if (loglist[log_id].in_use == 0) return;

    _lock_logger();
    if (loglist[log_id].logfile)
        fflush(loglist[log_id].logfile);
    _unlock_logger();
}

void log_reopen(int log_id)
{
    if (log_id < 0 && log_id >= LOG_MAXLOGS)
        return;
    if (loglist [log_id] . filename && loglist [log_id] . logfile)
    {
        _lock_logger();

        fclose (loglist [log_id] . logfile);
        loglist [log_id] . logfile = NULL;

        _unlock_logger();
    }
}

void log_close(int log_id)
{
    if (log_id < 0 || log_id >= LOG_MAXLOGS) return;

    _lock_logger();

    if (loglist[log_id].in_use == 0)
    {
        _unlock_logger();
        return;
    }

    loglist[log_id].in_use = 0;
    loglist[log_id].level = 2;
    if (loglist[log_id].filename) free(loglist[log_id].filename);
    if (loglist[log_id].buffer) free(loglist[log_id].buffer);

    if (loglist [log_id] . logfile)
    {
        fclose (loglist [log_id] . logfile);
        loglist [log_id] . logfile = NULL;
    }
    _unlock_logger();
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

void log_write(int log_id, unsigned priority, const char *cat, const char *func, 
        const char *fmt, ...)
{
    static char *prior[] = { "EROR", "WARN", "INFO", "DBUG" };
    char tyme[128];
    char pre[256];
    char line[LOG_MAXLINELEN];
    time_t now;
    va_list ap;

    if (log_id < 0) return;
    if (log_id > LOG_MAXLOGS) return; /* Bad log number */
    if (loglist[log_id].level < priority) return;
    if (priority > sizeof(prior)/sizeof(prior[0])) return; /* Bad priority */

    va_start(ap, fmt);
    vsnprintf(line, LOG_MAXLINELEN, fmt, ap);

    now = time(NULL);

    _lock_logger();
    strftime(tyme, sizeof (tyme), "[%Y-%m-%d  %H:%M:%S]", localtime(&now)); 

    snprintf(pre, sizeof (pre), "%s %s%s", prior[priority-1], cat, func);

    if (_log_open (log_id))
    {
        int len = fprintf (loglist[log_id].logfile, "%s %s %s\n", tyme, pre, line); 
        if (len > 0)
            loglist[log_id].size += len;
    }
    _unlock_logger();

    va_end(ap);
}

void log_write_direct(int log_id, const char *fmt, ...)
{
    char line[LOG_MAXLINELEN];
    va_list ap;

    if (log_id < 0) return;
    
    va_start(ap, fmt);

    _lock_logger();
    vsnprintf(line, LOG_MAXLINELEN, fmt, ap);
    if (_log_open (log_id))
    {
        int len = fprintf(loglist[log_id].logfile, "%s\n", line);
        if (len > 0)
            loglist[log_id].size += len;
    }
    _unlock_logger();

    va_end(ap);

    fflush(loglist[log_id].logfile);
}

static int _get_log_id()
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

static void _release_log_id(int log_id)
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




