#ifndef __LOG_H__
#define __LOG_H__

#define LOG_EINSANE -1
#define LOG_ENOMORELOGS -2
#define LOG_ECANTOPEN -3
#define LOG_ENOTOPEN -4


void log_initialize();
int log_open(const char *filename);
int log_open_with_buffer(const char *filename, int size);
void log_set_level(int log_id, int level);
void log_flush(int log_id);
void log_reopen(int log_id);
void log_close(int log_id);
void log_shutdown();

void log_write(int log_id, int priority, const char *cat, const char *fmt, ...);
void log_write_direct(int log_id, const char *fmt, ...);

#endif  /* __LOG_H__ */
