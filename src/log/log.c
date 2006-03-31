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

typedef struct _log_entry_t
{
   char *line;
   unsigned int len;
   struct _log_entry_t *next;
} log_entry_t;


typedef struct log_tag
{
    int in_use;

    unsigned level;

    char *filename;
    FILE *logfile;
    off_t size;
    off_t trigger_level;
    int archive_timestamp;

    unsigned long total;
    unsigned int entries;
    unsigned int keep_entries;
    log_entry_t *log_head;
    log_entry_t **log_tail;
    
    char *buffer;
} log_t;

static log_t loglist[LOG_MAXLOGS];

static int _get_log_id(void);
static void _release_log_id(int log_id);
static void _lock_logger(void);
static void _unlock_logger(void);


static int _log_open (int id, const char *file_timestamp)
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
                char new_name [4096];
                fclose (loglist [id] . logfile);
                loglist [id] . logfile = NULL;
                /* simple rename, but could use time providing locking were used */
                if (loglist[id].archive_timestamp && file_timestamp) {
                    snprintf (new_name,  sizeof(new_name), "%s.%s", loglist[id].filename, file_timestamp);
                }
                else {
                    snprintf (new_name,  sizeof(new_name), "%s.old", loglist [id] . filename);
                }
#ifdef _WIN32
                if (stat (new_name, &st) == 0)
                    remove (new_name);
#endif
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

void log_initialize(void)
{
    int i;

    if (_initialized) return;

    for (i = 0; i < LOG_MAXLOGS; i++) {
        loglist[i].in_use = 0;
        loglist[i].level = 2;
        loglist[i].size = 0;
        loglist[i].trigger_level = 1000000000;
        loglist[i].filename = NULL;
        loglist[i].logfile = NULL;
        loglist[i].buffer = NULL;
        loglist[i].total = 0;
        loglist[i].entries = 0;
        loglist[i].keep_entries = 0;
        loglist[i].log_head = NULL;
        loglist[i].log_tail = &loglist[i].log_head;
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
        loglist [id] . entries = 0;
        loglist [id] . log_head = NULL;
        loglist [id] . log_tail = &loglist [id] . log_head;
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
    /* NULL filename is ok, empty filename is not. */
    if ((filename && !strcmp(filename, "")) || loglist [id] . in_use == 0)
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

int log_set_archive_timestamp(int id, int value)
{
    if (id < 0 || id >= LOG_MAXLOGS)
        return LOG_EINSANE;
     _lock_logger();
     loglist[id].archive_timestamp = value;
     _unlock_logger();
    return id;
}


int log_open_with_buffer(const char *filename, int size)
{
    /* not implemented */
    return LOG_ENOTIMPL;
}


void log_set_lines_kept (int log_id, unsigned int count)
{
    if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
    if (loglist[log_id].in_use == 0) return;

    _lock_logger ();
    loglist[log_id].keep_entries = count;
    while (loglist[log_id].entries > count)
    {
        log_entry_t *to_go = loglist [log_id].log_head;
        loglist [log_id].log_head = to_go->next;
        loglist [log_id].total -= to_go->len;
        free (to_go->line);
        free (to_go);
        loglist [log_id].entries--;
    }
    _unlock_logger ();
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
    while (loglist[log_id].entries)
    {
        log_entry_t *to_go = loglist [log_id].log_head;
        loglist [log_id].log_head = to_go->next;
        loglist [log_id].total -= to_go->len;
        free (to_go->line);
        free (to_go);
        loglist [log_id].entries--;
    }
    _unlock_logger();
}

void log_shutdown(void)
{
    /* destroy mutexes */
#ifndef _WIN32
    pthread_mutex_destroy(&_logger_mutex);
#else
    DeleteCriticalSection(&_logger_mutex);
#endif 

    _initialized = 0;
}


static int create_log_entry (int log_id, const char *pre, const char *line)
{
    int len;
    log_entry_t *entry;

    if (loglist[log_id].keep_entries == 0)
        return fprintf (loglist[log_id].logfile, "%s%s\n", pre, line); 
    
    entry = calloc (1, sizeof (log_entry_t));
    entry->line = malloc (LOG_MAXLINELEN);
    len = snprintf (entry->line, LOG_MAXLINELEN, "%s%s\n", pre, line);
    entry->len = len;
    loglist [log_id].total += len;
    fprintf (loglist[log_id].logfile, "%s", entry->line);

    *loglist [log_id].log_tail = entry;
    loglist [log_id].log_tail = &entry->next;

    if (loglist [log_id].entries >= loglist [log_id].keep_entries)
    {
        log_entry_t *to_go = loglist [log_id].log_head;
        loglist [log_id].log_head = to_go->next;
        loglist [log_id].total -= to_go->len;
        free (to_go->line);
        free (to_go);
    }
    else
        loglist [log_id].entries++;
    return len;
}


void log_contents (int log_id, char **_contents, unsigned int *_len)
{
    int remain;
    log_entry_t *entry;
    char *ptr;

    if (log_id < 0) return;
    if (log_id >= LOG_MAXLOGS) return; /* Bad log number */

    _lock_logger ();
    remain = loglist [log_id].total + 1;
    *_contents = malloc (remain);
    *_len = loglist [log_id].total;

    entry = loglist [log_id].log_head;
    ptr = *_contents;
    while (entry)
    {
        int len = snprintf (ptr, remain, "%s", entry->line);
        if (len > 0)
        {
            ptr += len;
            remain -= len;
        }
        entry = entry->next;
    }
    _unlock_logger ();
}


void log_write(int log_id, unsigned priority, const char *cat, const char *func, 
        const char *fmt, ...)
{
    static char *prior[] = { "EROR", "WARN", "INFO", "DBUG" };
    int datelen;
    char filename_tyme[128];
    char pre[256];
    char line[LOG_MAXLINELEN];
    time_t now;
    va_list ap;

    if (log_id < 0 || log_id >= LOG_MAXLOGS) return; /* Bad log number */
    if (loglist[log_id].level < priority) return;
    if (priority > sizeof(prior)/sizeof(prior[0])) return; /* Bad priority */

    va_start(ap, fmt);
    vsnprintf(line, LOG_MAXLINELEN, fmt, ap);

    now = time(NULL);

    _lock_logger();
    datelen = strftime (pre, sizeof (pre), "[%Y-%m-%d  %H:%M:%S]", localtime(&now)); 
    strftime(filename_tyme, sizeof (filename_tyme), "%Y%m%d_%H%M%S", localtime(&now)); 

    snprintf (pre+datelen, sizeof (pre)-datelen, " %s %s%s ", prior [priority-1], cat, func);

    if (_log_open (log_id, filename_tyme))
    {
        int len = create_log_entry (log_id, pre, line);
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
    char filename_tyme[128];
    time_t now;

    if (log_id < 0 || log_id >= LOG_MAXLOGS) return;
    
    va_start(ap, fmt);

    now = time(NULL);

    _lock_logger();
    vsnprintf(line, LOG_MAXLINELEN, fmt, ap);
    strftime(filename_tyme, sizeof (filename_tyme), "%Y%m%d_%H%M%S", localtime(&now)); 
    if (_log_open (log_id, filename_tyme))
    {
        int len = create_log_entry (log_id, "", line);
        if (len > 0)
            loglist[log_id].size += len;
    }
    _unlock_logger();

    va_end(ap);

    fflush(loglist[log_id].logfile);
}

static int _get_log_id(void)
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

static void _lock_logger(void)
{
#ifndef _WIN32
    pthread_mutex_lock(&_logger_mutex);
#else
    EnterCriticalSection(&_logger_mutex);
#endif
}

static void _unlock_logger(void)
{
#ifndef _WIN32
    pthread_mutex_unlock(&_logger_mutex);
#else
    LeaveCriticalSection(&_logger_mutex);
#endif    
}




