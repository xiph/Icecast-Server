#ifndef __UTIL_H__
#define __UTIL_H__

int util_timed_wait_for_fd(int fd, int timeout);
int util_read_header(int sock, char *buff, unsigned long len);

#endif  /* __UTIL_H__ */
