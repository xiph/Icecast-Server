#ifndef __UTIL_H__
#define __UTIL_H__

#define XSLT_CONTENT 1
#define HTML_CONTENT 2

int util_timed_wait_for_fd(int fd, int timeout);
int util_read_header(int sock, char *buff, unsigned long len);
int util_check_valid_extension(char *uri);
char *util_get_extension(char *path);
char *util_get_path_from_uri(char *uri);
char *util_get_path_from_normalised_uri(char *uri);
char *util_normalise_uri(char *uri);
char *util_base64_encode(char *data);
char *util_base64_decode(unsigned char *input);

char *util_url_escape(char *src);

#endif  /* __UTIL_H__ */
