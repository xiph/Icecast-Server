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
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef __MINGW32__
#include <sys/timeb.h>
#endif

#include "timing.h"

/* see timing.h for an explanation of _mangle() */

/* 
 * Returns milliseconds no matter what. 
 */
uint64_t timing_get_time(void)
{
#ifdef _WIN32
#ifdef __MINGW32__
  struct timeb t;

  ftime(&t);
  return t.time * 1000 + t.millitm;
#else
    return timeGetTime();
#endif
#else 
    struct timeval mtv;

    gettimeofday(&mtv, NULL);

    return (uint64_t)(mtv.tv_sec) * 1000 + (uint64_t)(mtv.tv_usec) / 1000;
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
