/* 
** Logging framework.
**
** This program is distributed under the GNU General Public License, version 2.
** A copy of this license is included with this source.
*/

#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#define LOG_EINSANE -1
#define LOG_ENOMORELOGS -2
#define LOG_ECANTOPEN -3
#define LOG_ENOTOPEN -4
#define LOG_ENOTIMPL -5

#ifdef _WIN32
#define IO_BUFFER_TYPE _IONBF
#else
#define IO_BUFFER_TYPE _IOLBF
#endif

void log_initialize();
int log_open_file(FILE *file);
int log_open(const char *filename);
int log_open_with_buffer(const char *filename, int size);
void log_set_level(int log_id, unsigned level);
void log_set_trigger(int id, unsigned trigger);
int  log_set_filename(int id, const char *filename);
void log_flush(int log_id);
void log_reopen(int log_id);
void log_close(int log_id);
void log_shutdown();

void log_write(int log_id, unsigned priority, const char *cat, const char *func, 
        const char *fmt, ...);
void log_write_direct(int log_id, const char *fmt, ...);

#endif  /* __LOG_H__ */
