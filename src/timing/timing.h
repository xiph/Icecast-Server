#ifndef __TIMING_H__
#define __TIMING_H__

#include <sys/types.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef _WIN32
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#endif

uint64_t timing_get_time(void);
void timing_sleep(uint64_t sleeptime);

#endif  /* __TIMING_H__ */
