/* timing.c
** - Timing functions
**
** This program is distributed under the GNU General Public License, version 2.
** A copy of this license is included with this source.
*/

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdlib.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#ifdef TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else
#  ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else
#    include <time.h>
#  endif
#endif

#include <unistd.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_TIMEB_H
#include <sys/timeb.h>
#endif

#include "timing.h"

/* see timing.h for an explanation of _mangle() */

/* 
 * Returns milliseconds no matter what. 
 */
uint64_t timing_get_time(void)
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval mtv;

    gettimeofday(&mtv, NULL);

    return (uint64_t)(mtv.tv_sec) * 1000 + (uint64_t)(mtv.tv_usec) / 1000;
#elif HAVE_FTIME
    struct timeb t;

    ftime(&t);
    return t.time * 1000 + t.millitm;
#else
#error need time query handler
#endif
}


void timing_sleep(uint64_t sleeptime)
{
    struct timeval sleeper;

    sleeper.tv_sec = sleeptime / 1000;
    sleeper.tv_usec = (sleeptime % 1000) * 1000;

    /* NOTE:
     * This should be 0 for the first argument.  The linux manpage
     * says so.  The solaris manpage also says this is a legal
     * value.  If you think differerntly, please provide references.
     */
#ifdef WIN32
	Sleep(sleeptime);
#else
    select(1, NULL, NULL, NULL, &sleeper);
#endif
}
