#ifndef __OS_H__
#define __OS_H__

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#define size_t int
#define ssize_t int
#else
#define PATH_SEPARATOR "/"
#endif

#endif  /* __OS_H__ */
