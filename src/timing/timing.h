#ifndef __TIMING_H__
#define __TIMING_H__

#include <stdint.h>

uint64_t timing_get_time(void);
void timing_sleep(uint64_t sleeptime);

#endif  /* __TIMING_H__ */
