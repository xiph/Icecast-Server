/*
** resolver.h
**
** name resolver library header
**
*/

#ifndef __RESOLVER_H
#define __RESOLVER_H


/*
** resolver_lookup
**
** resolves a hosts name from it's ip address
** or
** resolves an ip address from it's host name
**
** returns a pointer to buff, or NULL if an error occured
**
*/

void resolver_initialize(void);
void resolver_shutdown(void);

char *resolver_getname(const char *ip, char *buff, int len);
char *resolver_getip(const char *name, char *buff, int len);

#endif





