/* timing.c
** - Timing functions
*/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _WIN32
#include <mmsystem.h>
#endif

#include "timing.h"

/* 
 * Returns milliseconds no matter what. 
 */
long long timing_get_time(void)
{
#ifdef _WIN32
	return timeGetTime();
#else 
	struct timeval mtv;

	gettimeofday(&mtv, NULL);

	return (long long)(mtv.tv_sec) * 1000 + (long long)(mtv.tv_usec) / 1000;
#endif
}

void timing_sleep(long long sleeptime)
{
	struct timeval sleeper;

	sleeper.tv_sec = 0;
	sleeper.tv_usec = sleeptime * 1000;

	select(0, NULL, NULL, NULL, &sleeper);
}
