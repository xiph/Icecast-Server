/*
** resolver.c
**
** name resolver library
**
*/

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#define sethostent(x)
#endif

#include "resolver.h"
#include "sock.h"

/* internal function */

static void _lock_resolver(void);
static void _unlock_resolver(void);
static char *_lookup(const char *what, char *buff, int len);
static int _isip(const char *what);

/* internal data */

#ifdef _WIN32
#define mutex_t CRITICAL_SECTION
#else
#define mutex_t pthread_mutex_t
#endif

static mutex_t _resolver_mutex;
static int _initialized = 0;

char *resolver_getname(const char *ip, char *buff, int len)
{
	if (!_isip(ip)) {
		strncpy(buff, ip, len);
		return buff;
	}

	return _lookup(ip, buff, len);
}

char *resolver_getip(const char *name, char *buff, int len)
{
	if (_isip(name)) {
		strncpy(buff, name, len);
		return buff;
	}

	return _lookup(name, buff, len);
}

static int _isip(const char *what)
{
#ifdef HAVE_IPV6
	union {
		struct in_addr v4addr;
		struct in6_addr v6addr;
	} addr_u;

	if (inet_pton(AF_INET, what, &addr_u.v4addr) <= 0)
		return inet_pton(AF_INET6, what, &addr_u.v6addr) > 0 ? 1 : 0;

	return 1;
#else
	struct in_addr inp;

	return inet_aton(what, &inp);
#endif
}

static char *_lookup(const char *what, char *buff, int len)
{
	/* linux doesn't appear to have getipnodebyname as of glibc-2.2.3, so the IPV6 lookup is untested */
#ifdef HAVE_GETIPNODEBYNAME
	int err;
#else
	struct in_addr inp;
#endif
	struct hostent *host = NULL;
	char *temp;

	/* do a little sanity checking */
	if (what == NULL || buff == NULL || len <= 0)
		return NULL;

#ifdef HAVE_GETIPNODEBYNAME
	host = getipnodebyname(what, AF_INET6, AI_DEFAULT, &err);
	if (host) {
		if (_isip(what))
			strncpy(buff, host->h_name, len);
		else
			inet_ntop(host->h_addrtype, host->h_addr_list[0], buff, len);
		
		freehostent(host);
	} else
		buff = NULL;
#else
	if (_isip(what)) {
		/* gotta lock calls for now, since gethostbyname and such
		 * aren't threadsafe */
		_lock_resolver();
		host = gethostbyaddr((char *)&inp, sizeof(struct in_addr), AF_INET);
		_unlock_resolver();
		if (host == NULL) {
			buff = NULL;
		} else {
			strncpy(buff, host->h_name, len);
		}
	} else {
		_lock_resolver();
		host = gethostbyname(what);
		_unlock_resolver();

		if (host == NULL) {
			buff = NULL;
		} else {
            // still need to be locked here? 
			temp = inet_ntoa(*(struct in_addr *)host->h_addr);
			strncpy(buff, temp, len);
		}
	}

#endif
	return buff;
}

void resolver_initialize()
{
	/* initialize the lib if we havne't done so already */

	if (!_initialized) {
		_initialized = 1;
#ifndef _WIN32
		pthread_mutex_init(&_resolver_mutex, NULL);
#else
		InitializeCriticalSection(&_resolver_mutex);
#endif

		/* keep dns connects (TCP) open */
		sethostent(1);
	}
}

void resolver_shutdown(void)
{
	if (_initialized) {
#ifndef _WIN32
		pthread_mutex_destroy(&_resolver_mutex);
#else
		DeleteCriticalSection(&_resolver_mutex);
#endif

		_initialized = 0;
	}
}

static void _lock_resolver()
{
#ifndef _WIN32
	pthread_mutex_lock(&_resolver_mutex);
#else
	EnterCriticalSection(&_resolver_mutex);
#endif
}

static void _unlock_resolver()
{
#ifndef _WIN32
	pthread_mutex_unlock(&_resolver_mutex);
#else
	LeaveCriticalSection(&_resolver_mutex);
#endif	
}
