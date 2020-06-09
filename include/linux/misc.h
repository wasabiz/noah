/*-
 * Copyright (c) 2016 Yuichi Nishiwaki, Takaya Saeki
 * Copyright (c) 2015 Dmitry Chagin
 * Copyright (c) 2013 Dmitry Chagin
 * Copyright (c) 2002 Doug Rabson
 * Copyright (c) 1994-1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _AMD64_LINUX_H_
#define	_AMD64_LINUX_H_

typedef struct {
  l_time_t	tv_sec;
  l_suseconds_t	tv_usec;
} l_timeval;

/*
 * Miscellaneous
 */
#define LINUX_CTL_MAXNAME	10

#define LINUX_MAX_ARG_STRLEN    (0x1000 * 32)
#define LINUX_MAX_ARG_STRINGS   0x7FFFFFFF

#define LINUX_AT_COUNT		19	/* Count of used aux entry types. */

struct l___sysctl_args
{
  l_uintptr_t	name;
  l_int		nlen;
  l_uintptr_t	oldval;
  l_uintptr_t	oldlenp;
  l_uintptr_t	newval;
  l_size_t	newlen;
  l_ulong	__spare[4];
};

/* Scheduling policies */
#define	LINUX_SCHED_OTHER	0
#define	LINUX_SCHED_FIFO	1
#define	LINUX_SCHED_RR		2

/* Resource limits */
#define	LINUX_RLIMIT_CPU	0
#define	LINUX_RLIMIT_FSIZE	1
#define	LINUX_RLIMIT_DATA	2
#define	LINUX_RLIMIT_STACK	3
#define	LINUX_RLIMIT_CORE	4
#define	LINUX_RLIMIT_RSS	5
#define	LINUX_RLIMIT_NPROC	6
#define	LINUX_RLIMIT_NOFILE	7
#define	LINUX_RLIMIT_MEMLOCK	8
#define	LINUX_RLIMIT_AS		9	/* Address space limit */

#define	LINUX_RLIM_NLIMITS	10

#define	LINUX_RLIM_INFINITY	(~0UL)

struct l_rlimit {
  l_ulong		rlim_cur;
  l_ulong		rlim_max;
};

struct l_rusage {
  l_timeval	ru_utime;
  l_timeval	ru_stime;
  l_long	ru_maxrss;
  l_long	ru_ixrss;
  l_long	ru_idrss;
  l_long	ru_isrss;
  l_long	ru_minflt;
  l_long	ru_majflt;
  l_long	ru_nswap;
  l_long	ru_inblock;
  l_long	ru_oublock;
  l_long	ru_msgsnd;
  l_long	ru_msgrcv;
  l_long	ru_nsignals;
  l_long	ru_nvcsw;
  l_long	ru_nivcsw;
} __attribute__ ((packed));

/*
 * poll()
 */
#define	LINUX_POLLIN		0x0001
#define	LINUX_POLLPRI		0x0002
#define	LINUX_POLLOUT		0x0004
#define	LINUX_POLLERR		0x0008
#define	LINUX_POLLHUP		0x0010
#define	LINUX_POLLNVAL		0x0020
#define	LINUX_POLLRDNORM	0x0040
#define	LINUX_POLLRDBAND	0x0080
#define	LINUX_POLLWRNORM	0x0100
#define	LINUX_POLLWRBAND	0x0200
#define	LINUX_POLLMSG		0x0400

struct l_pollfd {
  l_int		fd;
  l_short	events;
  l_short	revents;
};


#define	LINUX_CLONE_VM		   0x00000100
#define	LINUX_CLONE_FS		   0x00000200
#define	LINUX_CLONE_FILES	   0x00000400
#define	LINUX_CLONE_SIGHAND	   0x00000800
#define	LINUX_CLONE_PID		   0x00001000 /* No longer exist in Linux */
#define LINUX_CLONE_PTRACE         0x00002000
#define	LINUX_CLONE_VFORK	   0x00004000
#define	LINUX_CLONE_PARENT	   0x00008000
#define	LINUX_CLONE_THREAD	   0x00010000
#define LINUX_CLONE_NEWNS          0x00020000
#define LINUX_CLONE_SYSVSEM        0x00040000
#define	LINUX_CLONE_SETTLS	   0x00080000
#define	LINUX_CLONE_PARENT_SETTID  0x00100000
#define	LINUX_CLONE_CHILD_CLEARTID 0x00200000
#define	LINUX_CLONE_DETACHED	   0x00400000
#define	LINUX_CLONE_UNTRACED       0x00800000
#define	LINUX_CLONE_CHILD_SETTID   0x01000000
#define LINUX_CLONE_NEWUTS         0x04000000
#define LINUX_CLONE_NEWIPC         0x08000000
#define LINUX_CLONE_NEWUSER        0x10000000
#define LINUX_CLONE_NEWPID         0x20000000
#define LINUX_CLONE_NEWNET         0x40000000
#define LINUX_CLONE_IO             0x80000000

#define LINUX_ARCH_SET_GS		0x1001
#define LINUX_ARCH_SET_FS		0x1002
#define LINUX_ARCH_GET_FS		0x1003
#define LINUX_ARCH_GET_GS		0x1004

/* linux sysinfo */
struct l_sysinfo {
  l_long		uptime;		/* Seconds since boot */
  l_ulong		loads[3];	/* 1, 5, and 15 minute load averages */
#define LINUX_SYSINFO_LOADS_SCALE 65536
  l_ulong		totalram;	/* Total usable main memory size */
  l_ulong		freeram;	/* Available memory size */
  l_ulong		sharedram;	/* Amount of shared memory */
  l_ulong		bufferram;	/* Memory used by buffers */
  l_ulong		totalswap;	/* Total swap space size */
  l_ulong		freeswap;	/* swap space still available */
  l_ushort		procs;		/* Number of current processes */
  l_ushort		pads;
  l_ulong		totalhigh;
  l_ulong		freehigh;
  l_uint		mem_unit;
  char			_f[20-2*sizeof(l_long)-sizeof(l_int)];	/* padding */
};

#define	LINUX_WNOHANG		0x00000001
#define	LINUX_WUNTRACED		0x00000002
#define	LINUX_WSTOPPED		LINUX_WUNTRACED
#define	LINUX_WEXITED		0x00000004
#define	LINUX_WCONTINUED	0x00000008
#define	LINUX_WNOWAIT		0x01000000

#endif /* !_AMD64_LINUX_H_ */
