#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "sock.h"

#include "config.h"
#include "util.h"

int util_read_header(int sock, char *buff, unsigned long len)
{
	fd_set rfds;
	int read_bytes, ret;
	unsigned long pos;
	char c;
	struct timeval tv;
	ice_config_t *config;

	config = config_get_config();

	read_bytes = 1;
	pos = 0;
	ret = 0;

	while ((read_bytes == 1) && (pos < (len - 1))) {
		read_bytes = 0;

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);

		tv.tv_sec = config->header_timeout;
		tv.tv_usec = 0;

		if (select(sock + 1, &rfds, NULL, NULL, &tv) > 0) {
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




