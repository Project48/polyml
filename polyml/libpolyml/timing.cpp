/*
    Title:      Time functions.
    Author:     Dave Matthews, Cambridge University Computer Laboratory

    Copyright (c) 2000
        Cambridge University Technical Services Limited

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif


/************************************************************************
 *
 * Include runtime headers
 *
 ************************************************************************/

#include "proper_io.h"

#include "globals.h"
#include "mpoly.h"
#include "arb.h"
#include "machine_dep.h"
#include "run_time.h"
#include "sys.h"
#include "timing.h"
#include "polystring.h"
#include "save_vec.h"
#include "rts_module.h"
#include "processes.h"

#ifdef WINDOWS_PC
/* Windows file times are 64-bit numbers representing times in
   tenths of a microsecond. */
#define TICKS_PER_MICROSECOND 10

#ifdef __GNUC__
#define SECSSINCE1601 11644473600LL
#else
#define SECSSINCE1601 11644473600
#endif

#else
/* For Unix return times in microseconds. */
#define TICKS_PER_MICROSECOND 1
#endif

/*
    The original Poly timing functions used a variety of timing bases
    (e.g. seconds, tenths of a second).  The old functions have been
    retained but the intention is to phase them out in favour of new
    functions.  Most of these are handled through the timing_dispatch
    function.

    The intention behind the timing functions is to make use of the
    arbitrary precision arithmetic to allow for a wider range of dates
    than the usual mktime range of 1970 to 2036.  We also want to handle
    more accurate timing than per second or per microsecond where the
    operating system provides it.
*/

#ifdef WINDOWS_PC
static FILETIME startTime;
static LARGE_INTEGER gcUTime, gcSTime;
#else
static struct timeval startTime;
static struct timeval gcUTime, gcSTime;

/* Functions to add and subtract times. */
static void addTimes(struct timeval *result, struct timeval *x);
static void subTimes(struct timeval *result, struct timeval *x);
#endif


Handle timing_dispatch_c(TaskData *taskData, Handle args, Handle code)
{
    int c = get_C_long(taskData, DEREFWORDHANDLE(code));
    switch (c)
    {
    case 0: /* Get ticks per microsecond. */
        return Make_arbitrary_precision(taskData, TICKS_PER_MICROSECOND);
    case 1: /* Return time since the time base. */
        {
#ifdef WINDOWS_PC
            FILETIME ft;
			GetSystemTimeAsFileTime(&ft);
            return Make_arb_from_pair(taskData, ft.dwHighDateTime, ft.dwLowDateTime);
#else
            struct timeval tv;
            if (gettimeofday(&tv, NULL) != 0)
                raise_syscall(taskData, "gettimeofday failed", errno);
            return Make_arb_from_pair_scaled(taskData, tv.tv_sec, tv.tv_usec, 1000000);
#endif
        }
    case 2: /* Return the base year.  This is the year which corresponds to
               zero in the timing sequence. */
#ifdef WINDOWS_PC
        return Make_arbitrary_precision(taskData, 1601);
#else
        return Make_arbitrary_precision(taskData, 1970);
#endif

    case 3: /* In both Windows and Unix the time base is 1st of January
               in the base year.  This function is provided just in case
               we are running on a system with a different base.  It
               returns the number of seconds after 1st January of the
               base year that corresponds to zero of the time base. */
        return Make_arbitrary_precision(taskData, 0);

    case 4: /* Return the time offset which applied/will apply at the
               specified time (in seconds). */
        {
            int localoff;
            time_t theTime;
#ifdef WINDOWS_PC
            /* Although the offset is in seconds it is since 1601. */
            LARGE_INTEGER   liTime;
            get_C_pair(taskData, DEREFWORDHANDLE(args), (unsigned long*)&liTime.HighPart, (unsigned long*)&liTime.LowPart); /* May raise exception. */
            theTime = (long)(liTime.QuadPart - SECSSINCE1601);
#else
            theTime = get_C_long(taskData, DEREFWORDHANDLE(args)); /* May raise exception. */
#endif
            struct tm *loctime = gmtime(&theTime);
            if (loctime == NULL) raise_exception0(taskData, EXC_size);
            localoff = (loctime->tm_hour*60 + loctime->tm_min)*60 + loctime->tm_sec;

            loctime = localtime(&theTime);
            if (loctime == NULL) raise_exception0(taskData, EXC_size);
            localoff -= (loctime->tm_hour*60 + loctime->tm_min)*60 + loctime->tm_sec;
            return Make_arbitrary_precision(taskData, localoff);
        }

    case 5: /* Find out if Summer Time (daylight saving) was/will be in effect. */
        {
            time_t theTime;
#ifdef WINDOWS_PC
            /* Although the offset is in seconds it is since 1601. */
            LARGE_INTEGER   liTime;
            get_C_pair(taskData, DEREFWORDHANDLE(args), (unsigned long*)&liTime.HighPart, (unsigned long*)&liTime.LowPart); /* May raise exception. */
            theTime = (long)(liTime.QuadPart - SECSSINCE1601);
#else
            theTime = get_C_long(taskData, DEREFWORDHANDLE(args)); /* May raise exception. */
#endif
			struct tm *loctime = localtime(&theTime);
			if (loctime == NULL) raise_exception0(taskData, EXC_size);
			return Make_arbitrary_precision(taskData, loctime->tm_isdst);
        }

    case 6: /* Call strftime.  It would be possible to do much of this in
               ML except that it requires the current locale. */
        {
            struct  tm time;
            char    *format, buff[2048];
            Handle  resString;
            /* Get the format string. */
            format = Poly_string_to_C_alloc(DEREFHANDLE(args)->Get(0));

            /* Copy the time information. */
            time.tm_year = get_C_long(taskData, DEREFHANDLE(args)->Get(1)) - 1900;
            time.tm_mon = get_C_long(taskData, DEREFHANDLE(args)->Get(2));
            time.tm_mday = get_C_long(taskData, DEREFHANDLE(args)->Get(3));
            time.tm_hour = get_C_long(taskData, DEREFHANDLE(args)->Get(4));
            time.tm_min = get_C_long(taskData, DEREFHANDLE(args)->Get(5));
            time.tm_sec = get_C_long(taskData, DEREFHANDLE(args)->Get(6));
            time.tm_wday = get_C_long(taskData, DEREFHANDLE(args)->Get(7));
            time.tm_yday = get_C_long(taskData, DEREFHANDLE(args)->Get(8));
            time.tm_isdst = get_C_long(taskData, DEREFHANDLE(args)->Get(9));
#ifdef WINDOWS_PC
            _tzset(); /* Make sure we set the current locale. */
#else
            setlocale(LC_TIME, "");
#endif
            /* It would be better to dynamically allocate the string rather
               than use a fixed size but Unix unlike Windows does not distinguish
               between an error in the input and the buffer being too small. */
            if (strftime(buff, sizeof(buff), format, &time) <= 0)
            {
                /* Error */
                free(format);
                raise_exception0(taskData, EXC_size);
            }
            resString = taskData->saveVec.push(C_string_to_Poly(taskData, buff));
            free(format);
            return resString;
        }

    case 7: /* Return User CPU time since the start. */
        {
#ifdef WINDOWS_PC
            FILETIME ut, ct, et, kt;
            if (GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
                return Make_arb_from_pair(taskData, ut.dwHighDateTime, ut.dwLowDateTime);
            /* GetProcessTimes failed, assume because this is not NT.
               Have to use real time. */
			GetSystemTimeAsFileTime(&ut);
            LARGE_INTEGER li, lj;
            li.LowPart = ut.dwLowDateTime;
            li.HighPart = ut.dwHighDateTime;
            lj.LowPart = startTime.dwLowDateTime;
            lj.HighPart = startTime.dwHighDateTime;
            li.QuadPart -= lj.QuadPart;
            return Make_arb_from_pair(taskData, li.HighPart, li.LowPart);
#else
            struct rusage rusage;
            if (proper_getrusage(RUSAGE_SELF, &rusage) != 0)
                raise_syscall(taskData, "getrusage failed", errno);
            return Make_arb_from_pair_scaled(taskData, rusage.ru_utime.tv_sec,
                        rusage.ru_utime.tv_usec, 1000000);
#endif
        }

    case 8: /* Return System CPU time since the start. */
        {
#ifdef WINDOWS_PC
            FILETIME ct, et, kt, ut;
            if (GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
                return Make_arb_from_pair(taskData, kt.dwHighDateTime, kt.dwLowDateTime);
            /* If GetProcessTimes fails just return 0. */
            return Make_arbitrary_precision(taskData, 0);
#else
            struct rusage rusage;
            if (proper_getrusage(RUSAGE_SELF, &rusage) != 0)
                raise_syscall(taskData, "getrusage failed", errno);
            return Make_arb_from_pair_scaled(taskData, rusage.ru_stime.tv_sec,
                        rusage.ru_stime.tv_usec, 1000000);
#endif
        }

    case 9: /* Return GC time since the start. */
#ifdef WINDOWS_PC
        return Make_arb_from_pair(taskData, gcUTime.HighPart, gcUTime.LowPart);
#else
        return Make_arb_from_pair_scaled(taskData, gcUTime.tv_sec, gcUTime.tv_usec, 1000000);
#endif

    case 10: /* Return real time since the start. */
        {
#ifdef WINDOWS_PC
            FILETIME ft;
            LARGE_INTEGER li, lj;
			GetSystemTimeAsFileTime(&ft);
            li.LowPart = ft.dwLowDateTime;
            li.HighPart = ft.dwHighDateTime;
            lj.LowPart = startTime.dwLowDateTime;
            lj.HighPart = startTime.dwHighDateTime;
            li.QuadPart -= lj.QuadPart;
            return Make_arb_from_pair(taskData, li.HighPart, li.LowPart);
#else
            struct timeval tv;
            if (gettimeofday(&tv, NULL) != 0)
                raise_syscall(taskData, "gettimeofday failed", errno);
            subTimes(&tv, &startTime);
            return Make_arb_from_pair_scaled(taskData, tv.tv_sec, tv.tv_usec, 1000000);
#endif
        }

        /* These next two are used only in the Posix structure. */
    case 11: /* Return User CPU time used by child processes. */
        {
#ifdef WINDOWS_PC
            return Make_arbitrary_precision(taskData, 0);
#else
            struct rusage rusage;
            if (proper_getrusage(RUSAGE_CHILDREN, &rusage) != 0)
                raise_syscall(taskData, "getrusage failed", errno);
            return Make_arb_from_pair_scaled(taskData, rusage.ru_utime.tv_sec,
                        rusage.ru_utime.tv_usec, 1000000);
#endif
        }

    case 12: /* Return System CPU time used by child processes. */
        {
#ifdef WINDOWS_PC
            return Make_arbitrary_precision(taskData, 0);
#else
            struct rusage rusage;
            if (proper_getrusage(RUSAGE_CHILDREN, &rusage) != 0)
                raise_syscall(taskData, "getrusage failed", errno);
            return Make_arb_from_pair_scaled(taskData, rusage.ru_stime.tv_sec,
                        rusage.ru_stime.tv_usec, 1000000);
#endif
        }

    case 13: /* Return GC system time since the start. */
        /* This function was added in Gansner & Reppy. */
#ifdef WINDOWS_PC
        return Make_arb_from_pair(taskData, gcSTime.HighPart, gcSTime.LowPart);
#else
        return Make_arb_from_pair_scaled(taskData, gcSTime.tv_sec, gcSTime.tv_usec, 1000000);
#endif

    default:
        {
            char msg[100];
            sprintf(msg, "Unknown timing function: %d", c);
            raise_exception_string(taskData, EXC_Fail, msg);
			return 0;
        }
    }
}

/* This function is called at the beginning and end of garbage
   collection to record the time used.
   To speed up garbage collection we decide to do this only if
   there is a timer running.
*/
void record_gc_time(bool isEnd)
{
#ifdef WINDOWS_PC
    FILETIME kt, ut;
    FILETIME ct, et;
    if (! GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
    {
        /* GetProcessTimes failed, assume because this is not NT.
           Have to use real time. */
		GetSystemTimeAsFileTime(&ut);
        kt.dwHighDateTime = kt.dwLowDateTime = 0;
    }
    LARGE_INTEGER liU, liS;
    liU.LowPart = ut.dwLowDateTime;
    liU.HighPart = ut.dwHighDateTime;
    liS.LowPart = kt.dwLowDateTime;
    liS.HighPart = kt.dwHighDateTime;
    /* If this is the start we subtract the current time from
       the accumulator.  At the end we add it so that the
       result is right. Since the user program can't call in to get
       the GC time while we're actually collecting that's quite safe. */
    if (isEnd) {
        gcUTime.QuadPart += liU.QuadPart;
        gcSTime.QuadPart += liS.QuadPart;
    }
    else {
        gcUTime.QuadPart -= liU.QuadPart;
        gcSTime.QuadPart -= liS.QuadPart;
    }
#else
    struct rusage rusage;
    if (proper_getrusage(RUSAGE_SELF, &rusage) != 0) return;
    if (isEnd) {
        addTimes(&gcUTime, &rusage.ru_utime);
        addTimes(&gcSTime, &rusage.ru_stime);
    }
    else {
        subTimes(&gcUTime, &rusage.ru_utime);
        subTimes(&gcSTime, &rusage.ru_stime);
    }
#endif
}

#ifndef WINDOWS_PC
static void addTimes(struct timeval *result, struct timeval *x)
{
    long uSecs = result->tv_usec + x->tv_usec;
    result->tv_sec += x->tv_sec;
    if (uSecs >= 1000000) { result->tv_sec++; uSecs -= 1000000; }
    result->tv_usec = uSecs;
}

static void subTimes(struct timeval *result, struct timeval *x)
{
    long uSecs = result->tv_usec - x->tv_usec;
    result->tv_sec -= x->tv_sec;
    if (uSecs < 0) { result->tv_sec--; uSecs += 1000000; }
    result->tv_usec = uSecs;
}
#endif

class Timing: public RtsModule
{
public:
    virtual void Init(void);
};

// Declare this.  It will be automatically added to the table.
static Timing timingModule;

void Timing::Init(void)
{
#ifdef WINDOWS_PC
	/* Record an initial time of day to use as the basis of
	   real timing and also CPU timing if GetProcessTimes doesn't work. */
	GetSystemTimeAsFileTime(&startTime);
#else
    gettimeofday(&startTime, NULL);
#endif
}
