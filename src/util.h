#ifndef __UTIL_H__
#define __UTIL_H__

#define XSLT_CONTENT 1
#define HTML_CONTENT 2

int util_timed_wait_for_fd(int fd, int timeout);
int util_read_header(int sock, char *buff, unsigned long len);
int util_get_full_path(char *uri, char *fullPath, int fullPathLen);
int util_check_valid_extension(char *uri);
char *util_get_extension(char *path);

#endif  /* __UTIL_H__ */
