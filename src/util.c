#include <sys/types.h>

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
#endif

#include "sock.h"

#include "config.h"
#include "util.h"

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




