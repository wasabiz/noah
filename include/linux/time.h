/*-
 * Copyright (c) 2016 Takaya Saeki
 * Copyright (c) 2007 Roman Divacky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NOAH_TIME_H
#define _NOAH_TIME_H

#define LINUX_UTIME_NOW			0x3FFFFFFF
#define LINUX_UTIME_OMIT		0x3FFFFFFE

struct l_timespec {
  l_time_t tv_sec;
  l_long   tv_nsec;
};

struct l_timeval {
  l_time_t      tv_sec;
  l_suseconds_t tv_usec;
};

struct l_timezone {
  int tz_minuteswest;
  int tz_dsttime;
};

typedef uint32_t l_clockid_t;

struct l_itimerval {
  struct l_timeval it_interval;
  struct l_timeval it_value;
};

#define LINUX_CLOCK_REALTIME             0
#define LINUX_CLOCK_MONOTONIC            1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID   2
#define LINUX_CLOCK_THREAD_CPUTIME_ID    3
#define LINUX_CLOCK_REALTIME_HR          4
#define LINUX_CLOCK_MONOTONIC_HR         5
#define LINUX_CLOCK_MONOTONIC_COARSE     6
#define LINUX_CLOCK_BOOTTIME             7
#define LINUX_CLOCK_REALTIME_ALARM       8
#define LINUX_CLOCK_BOOTTIME_ALARM       9
#define LINUX_CLOCK_SGI_CYCLE            10
#define LINUX_CLOCK_TAI                  11

#endif
