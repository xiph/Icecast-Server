/* sock.c
 * - General Socket Functions
 *
 * Copyright (c) 1999 the icecast team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <sys/poll.h>
#else
#include <winsock2.h>
#define vsnprintf _vsnprintf
#define EINPROGRESS WSAEINPROGRESS
#define ENOTSOCK WSAENOTSOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif

#include "sock.h"
#include "resolver.h"

#ifndef _WIN32
extern int errno;
#endif

/* sock_initialize
**
** initializes the socket library.  you must call this
** before using the library!
*/
void sock_initialize(void)
{
#ifdef _WIN32
	WSADATA wsad;
	WSAStartup(0x0101, &wsad);
#endif

	resolver_initialize();
}

/* sock_shutdown
**
** shutdown the socket library.  remember to call this when you're
** through using the lib
*/
void sock_shutdown(void)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

/* sock_get_localip
**
** gets the local ip address for the machine
** the ip it returns *should* be on the internet.
** in any case, it's as close as we can hope to get
** unless someone has better ideas on how to do this
*/
char *sock_get_localip(char *buff, int len)
{
	char temp[1024];

	if (gethostname(temp, 1024) != 0)
		return NULL;

	if (resolver_getip(temp, buff, len))
		return buff;

	return NULL;
}

/* sock_error
** 
** returns the last socket error
*/
int sock_error(void)
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

/* sock_recoverable
**
** determines if the socket error is recoverable
** in terms of non blocking sockets
*/
int sock_recoverable(int error)
{
	return (error == EAGAIN || error == EINTR || error == EINPROGRESS || error == EWOULDBLOCK);
}

/* sock_valid_socket
**
** determines if a sock_t represents a valid socket
*/
int sock_valid_socket(sock_t sock)
{
	int ret;
	int optval, optlen;

	optlen = sizeof(int);
	ret = getsockopt(sock, SOL_SOCKET, SO_TYPE, &optval, &optlen);

	return (ret == 0);
}

/* inet_aton
**
** turns an ascii ip address into a binary representation
*/
#ifdef _WIN32
int inet_aton(const char *s, struct in_addr *a)
{
	int lsb, b2, b3, msb;

	if (sscanf(s, "%d.%d.%d.%d", &lsb, &b2, &b3, &msb) < 4) {
		return 0;
	}

	a->s_addr = inet_addr(s);
    
	return (a->s_addr != INADDR_NONE);
}
#endif /* _WIN32 */

/* sock_set_blocking
**
** set the sock blocking or nonblocking
** SOCK_BLOCK for blocking
** SOCK_NONBLOCK for nonblocking
*/
int sock_set_blocking(sock_t sock, const int block)
{
#ifdef _WIN32
	int varblock = block;
#endif

	if ((!sock_valid_socket(sock)) || (block < 0) || (block > 1))
		return SOCK_ERROR;

#ifdef _WIN32
	return ioctlsocket(sock, FIONBIO, &varblock);
#else
	return fcntl(sock, F_SETFL, (block == SOCK_BLOCK) ? 0 : O_NONBLOCK);
#endif
}

/* sock_close
**
** close the socket
*/
int sock_close(sock_t sock)
{
#ifdef _WIN32
	return closesocket(sock);
#else
	return close(sock);
#endif
}

/* sock_write_bytes
**
** write bytes to the socket
** this function will block until all bytes are 
** written, or there is an unrecoverable error
** even if the socket is non-blocking
*/
int sock_write_bytes(sock_t sock, const char *buff, const int len)
{
//	int wrote, res, polled;
	int res;
//	struct pollfd socks;

	/* sanity check */
	if (!buff) {
		return SOCK_ERROR;
	} else if (len <= 0) {
		return SOCK_ERROR;
	} else if (!sock_valid_socket(sock)) {
		return SOCK_ERROR;
	}

//	socks.fd = sock;
//	socks.events = POLLOUT;

//	wrote = 0;
//	while (wrote < len) {
//		polled = poll(&socks, 1, 30000);

//		if ((polled == -1) && sock_recoverable(sock_error()))
//			continue;
//		if (polled != 1)
//			return SOCK_ERROR;
	
//		res = send(sock, &buff[wrote], len - wrote, 0);
		res = send(sock, buff, len, 0);
//
//		if ((res < 0) && (!sock_recoverable(sock_error())))
//			return SOCK_ERROR;
//		if (res > 0)
//			wrote += res;
//	}
//
//      return wrote;

	return res;
}

/* sock_write_string
**
** writes a string to a socket
** this function always blocks even if the socket is nonblocking
*/
int sock_write_string(sock_t sock, const char *buff)
{
	return (sock_write_bytes(sock, buff, strlen(buff)) > 0);
}

/* sock_write
**
** write a formatted string to the socket
** this function will always block, even if the socket is nonblocking
** will truncate the string if it's greater than 1024 chars.
*/
int sock_write(sock_t sock, const char *fmt, ...)
{
	char buff[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buff, 1024, fmt, ap);
	va_end(ap);
	
	return sock_write_bytes(sock, buff, strlen(buff));
}

int sock_read_bytes(sock_t sock, char *buff, const int len)
{
//	int ret;

	if (!sock_valid_socket(sock)) return 0;
	if (!buff) return 0;
	if (len <= 0) return 0;

	return recv(sock, buff, len, 0);
}

/* sock_read_line
**
** Read one line of at max len bytes from sock into buff.
** If ok, return 1 and nullterminate buff. Otherwize return 0.
** Terminating \n is not put into the buffer.
**
** this function will probably not work on sockets in nonblocking mode
*/
int sock_read_line(sock_t sock, char *buff, const int len)
{
	char c = '\0';
	int read_bytes, pos;
  
	if (!sock_valid_socket(sock)) {
		return 0;
	} else if (!buff) {
		return 0;
	} else if (len <= 0) {
		return 0;
	}

	pos = 0;
	read_bytes = recv(sock, &c, 1, 0);

	if (read_bytes < 0) {
		return 0;
	}

	while ((c != '\n') && (pos < len) && (read_bytes == 1)) {
		if (c != '\r')
			buff[pos++] = c;
		read_bytes = recv(sock, &c, 1, 0);
	}
	
	if (read_bytes == 1) {
		buff[pos] = '\0';
		return 1;
	} else {
		return 0;
	}
}

/* sock_connect_wto
**
** Connect to hostname on specified port and return the created socket.
** timeout specifies the maximum time to wait for this to finish and
** returns when it expires whether it connected or not
** setting timeout to 0 disable the timeout.
*/
sock_t sock_connect_wto(const char *hostname, const int port, const int timeout)
{
	sock_t  sock;
	struct sockaddr_in sin, server;
	char ip[20];

	if (!hostname || !hostname[0]) {
		return SOCK_ERROR;
	} else if (port <= 0) {
		return SOCK_ERROR;
	}
		
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == SOCK_ERROR) { 
		sock_close(sock); 
		return SOCK_ERROR; 
	}

	memset(&sin, 0, sizeof(struct sockaddr_in));
	memset(&server, 0, sizeof(struct sockaddr_in));

	if (!resolver_getip(hostname, ip, 20))
		return SOCK_ERROR;
	
	if (inet_aton(ip, (struct in_addr *)&sin.sin_addr) == 0) {
		sock_close(sock);
		return SOCK_ERROR;
	}

	memcpy(&server.sin_addr, &sin.sin_addr, sizeof(struct sockaddr_in));

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	/* if we have a timeout, use select, if not, use connect straight. */
	/* dunno if this is portable, and it sure is complicated for such a 
	   simple thing to want to do.  damn BSD sockets! */
	if (timeout > 0) {
		fd_set wfds;
		struct timeval tv;
		int retval;
		int val;
		int valsize = sizeof(int);

		FD_ZERO(&wfds);
		FD_SET(sock, &wfds);
		tv.tv_sec = timeout;
		tv.tv_usec = 0;

		sock_set_blocking(sock, SOCK_NONBLOCK);
		retval = connect(sock, (struct sockaddr *)&server, sizeof(server));
		if (retval == 0) {
			sock_set_blocking(sock, SOCK_BLOCK);
			return sock;
		} else {
			if (!sock_recoverable(sock_error())) {
				sock_close(sock);
				return SOCK_ERROR;
			}
		}

		if (select(sock + 1, NULL, &wfds, NULL, &tv)) {
			retval = getsockopt(sock, SOL_SOCKET, SO_ERROR, (void *)&val, (int *)&valsize);
			if ((retval == 0) && (val == 0)) {
				sock_set_blocking(sock, SOCK_BLOCK);
				return sock;
			} else {
				sock_close(sock);
				return SOCK_ERROR;
			}
		} else {
			sock_close(sock);
			return SOCK_ERROR;
		}
       } else {
	  	if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {
			return sock;
		} else {
			sock_close(sock);
			return SOCK_ERROR;
		}
       }
}

/* sock_get_server_socket
**
** create a socket for incoming requests on a specified port and
** interface.  if interface is null, listen on all interfaces.
** returns the socket, or SOCK_ERROR on failure
*/
sock_t sock_get_server_socket(const int port, char *sinterface)
{
#ifdef HAVE_IPV6
	struct sockaddr_storage sa;
#else	
	struct sockaddr_in sa;
#endif
	int sa_family, sa_len, error, opt;
	sock_t sock;
	char ip[40];

	if (port < 0)
		return SOCK_ERROR;

	/* defaults */
	memset(&sa, 0, sizeof(sa));
	sa_family = AF_INET;
	sa_len = sizeof (struct sockaddr_in);

	/* set the interface to bind to if specified */
	if (sinterface != NULL) {
		if (!resolver_getip(sinterface, ip, sizeof (ip)))
			return SOCK_ERROR;

#ifdef HAVE_IPV6
		if (inet_pton(AF_INET, ip, &((struct sockaddr_in*)&sa)->sin_addr) > 0) {
			((struct sockaddr_in*)&sa)->sin_family = AF_INET;
			((struct sockaddr_in*)&sa)->sin_port = htons(port);
		} else if (inet_pton(AF_INET6, ip, &((struct sockaddr_in6*)&sa)->sin6_addr) > 0) {
			sa_family = AF_INET6;
			sa_len = sizeof (struct sockaddr_in6);
			((struct sockaddr_in6*)&sa)->sin6_family = AF_INET6;
			((struct sockaddr_in6*)&sa)->sin6_port = htons(port);
		} else
			return SOCK_ERROR;
#else
		if (!inet_aton(ip, &sa.sin_addr))
			return SOCK_ERROR;
		else {
			sa.sin_family = AF_INET;
			sa.sin_port = htons(port);
		}
#endif
	} else {
		((struct sockaddr_in*)&sa)->sin_addr.s_addr = INADDR_ANY;
		((struct sockaddr_in*)&sa)->sin_family = AF_INET;
		((struct sockaddr_in*)&sa)->sin_port = htons(port);
	}

	/* get a socket */
	sock = socket(sa_family, SOCK_STREAM, 0);
	if (sock == -1)
		return SOCK_ERROR;

	/* reuse it if we can */
	opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(int));
	
	/* bind socket to port */
	error = bind(sock, (struct sockaddr *)&sa, sa_len);
	if (error == -1)
		return SOCK_ERROR;

	return sock;
}

int sock_listen(sock_t serversock, int backlog)
{
	if (!sock_valid_socket(serversock))
		return 0;

	if (backlog <= 0)
		backlog = 10;

	return (listen(serversock, backlog) == 0);
}

int sock_accept(sock_t serversock, char *ip, int len)
{
	struct sockaddr_in sin;
	int ret;
	int slen;

	if (!sock_valid_socket(serversock))
		return SOCK_ERROR;

	slen = sizeof(struct sockaddr_in);
	ret = accept(serversock, (struct sockaddr *)&sin, &slen);

	if (ret >= 0 && ip != NULL)
		strncpy(ip, inet_ntoa(sin.sin_addr), len);

	return ret;
}
