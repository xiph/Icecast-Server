#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#else
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

#include "sock.h"

#include "config.h"
#include "util.h"
#include "os.h"

/* Abstract out an interface to use either poll or select depending on which
 * is available (poll is preferred) to watch a single fd.
 *
 * timeout is in milliseconds.
 *
 * returns > 0 if activity on the fd occurs before the timeout.
 *           0 if no activity occurs
 *         < 0 for error.
 */
int util_timed_wait_for_fd(int fd, int timeout)
{
#ifdef HAVE_POLL
    struct pollfd ufds;

    ufds.fd = fd;
    ufds.events = POLLIN;
    ufds.revents = 0;

    return poll(&ufds, 1, timeout);
#else
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    tv.tv_sec = timeout/1000;
    tv.tv_usec = (timeout - tv.tv_sec)*1000;

    return select(fd+1, &rfds, NULL, NULL, &tv);
#endif
}

int util_read_header(int sock, char *buff, unsigned long len)
{
	int read_bytes, ret;
	unsigned long pos;
	char c;
	ice_config_t *config;

	config = config_get_config();

	read_bytes = 1;
	pos = 0;
	ret = 0;

	while ((read_bytes == 1) && (pos < (len - 1))) {
		read_bytes = 0;

        if (util_timed_wait_for_fd(sock, config->header_timeout*1000) > 0) {

			if ((read_bytes = recv(sock, &c, 1, 0))) {
				if (c != '\r') buff[pos++] = c;
				if ((pos > 1) && (buff[pos - 1] == '\n' && buff[pos - 2] == '\n')) {
					ret = 1;
					break;
				}
			}
		} else {
			break;
		}
	}

	if (ret) buff[pos] = '\0';
	
	return ret;
}

int util_get_full_path(char *uri, char *fullPath, int fullPathLen) {
	int ret = 0;
	if (uri) {
		memset(fullPath, '\000', fullPathLen);
		snprintf(fullPath, fullPathLen-1, "%s%s", config_get_config()->webroot_dir, uri);
		ret = 1;
	}
	return ret;
}

char *util_get_extension(char *path) {
    char *ext = strrchr(path, '.');

    if(ext == NULL)
        return "";
    else
        return ext+1;
}

int util_check_valid_extension(char *uri) {
	int	ret = 0;
	char	*p2;

	if (uri) {
		p2 = strrchr(uri, '.');
		if (p2) {
			p2++;
			if (strncmp(p2, "xsl", strlen("xsl")) == 0) {
				/* Build the full path for the request, concatenating the webroot from the config.
				** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
				*/
				ret = XSLT_CONTENT;
			}
			if (strncmp(p2, "htm", strlen("htm")) == 0) {
				/* Build the full path for the request, concatenating the webroot from the config.
				** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
				*/
				ret = HTML_CONTENT;
			}
			if (strncmp(p2, "html", strlen("html")) == 0) {
				/* Build the full path for the request, concatenating the webroot from the config.
				** Here would be also a good time to prevent accesses like '../../../../etc/passwd' or somesuch.
				*/
				ret = HTML_CONTENT;
			}

		}
	}
	return ret;
}


