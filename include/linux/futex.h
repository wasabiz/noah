/*	$NetBSD: linux_futex.h,v 1.2 2005/12/11 12:20:19 christos Exp $ */

/*-
 * Copyright (c) 2016 Yuichi Nishiwaki
 * Copyright (c) 2005 Emmanuel Dreyfus, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Emmanuel Dreyfus
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#define LINUX_FUTEX_WAIT		 0
#define LINUX_FUTEX_WAKE		 1
#define LINUX_FUTEX_FD			 2	/* unused */
#define LINUX_FUTEX_REQUEUE		 3
#define LINUX_FUTEX_CMP_REQUEUE		 4
#define LINUX_FUTEX_WAKE_OP		 5
#define LINUX_FUTEX_LOCK_PI		 6
#define LINUX_FUTEX_UNLOCK_PI		 7
#define LINUX_FUTEX_TRYLOCK_PI		 8
#define LINUX_FUTEX_WAIT_BITSET		 9
#define LINUX_FUTEX_WAKE_BITSET		10
#define LINUX_FUTEX_WAIT_REQUEUE_PI	11
#define LINUX_FUTEX_CMP_REQUEUE_PI	12

#define LINUX_FUTEX_PRIVATE_FLAG	128
#define LINUX_FUTEX_CLOCK_REALTIME	256

#define LINUX_FUTEX_CMD_MASK (128 - 1)

#define LINUX_FUTEX_GETOP(v) (((v) >> 28) & 0xf)
#define LINUX_FUTEX_GETCMP(v) (((v) >> 24) & 0xf)
#define LINUX_FUTEX_GETOPARG(v) ((v >> 12) & 0xfff)
#define LINUX_FUTEX_GETCMPARG(v) ((int)((v) & 0xfff))

#define FUTEX_OP_SET            0	/* *(int *)UADDR2 = OPARG; */
#define FUTEX_OP_ADD            1	/* *(int *)UADDR2 += OPARG; */
#define FUTEX_OP_OR             2	/* *(int *)UADDR2 |= OPARG; */
#define FUTEX_OP_ANDN           3	/* *(int *)UADDR2 &= ~OPARG; */
#define FUTEX_OP_XOR            4	/* *(int *)UADDR2 ^= OPARG; */

#define FUTEX_OP_OPARG_SHIFT    8	/* Use (1 << OPARG) instead of OPARG.  */

#define FUTEX_OP_CMP_EQ         0	/* if (oldval == CMPARG) wake */
#define FUTEX_OP_CMP_NE         1	/* if (oldval != CMPARG) wake */
#define FUTEX_OP_CMP_LT         2	/* if (oldval < CMPARG) wake */
#define FUTEX_OP_CMP_LE         3	/* if (oldval <= CMPARG) wake */
#define FUTEX_OP_CMP_GT         4	/* if (oldval > CMPARG) wake */
#define FUTEX_OP_CMP_GE         5	/* if (oldval >= CMPARG) wake */

#define	FUTEX_WAITERS		0x80000000
#define	FUTEX_OWNER_DIED	0x40000000
#define	FUTEX_TID_MASK		0x3fffffff
#define	FUTEX_BITSET_MATCH_ANY	0xffffffff

/* robust futexes */
struct linux_robust_list {
  l_uintptr_t			next;
};

struct linux_robust_list_head {
  struct linux_robust_list	list;
  l_long			futex_offset;
  l_uintptr_t			pending_list;
};

#define LINUX_PR_SET_PDEATHSIG                 1
#define LINUX_PR_GET_PDEATHSIG                 2
#define LINUX_PR_GET_DUMPABLE                  3
#define LINUX_PR_SET_DUMPABLE                  4
#define LINUX_PR_GET_UNALIGN                   5
#define LINUX_PR_SET_UNALIGN                   6
#define LINUX_PR_GET_KEEPCAPS                  7
#define LINUX_PR_SET_KEEPCAPS                  8
#define LINUX_PR_GET_FPEMU                     9
#define LINUX_PR_SET_FPEMU                    10
#define LINUX_PR_GET_FPEXC                    11
#define LINUX_PR_SET_FPEXC                    12
#define LINUX_PR_GET_TIMING                   13
#define LINUX_PR_SET_TIMING                   14
#define LINUX_PR_SET_NAME                     15
#define LINUX_PR_GET_NAME                     16

#define LINIX_PR_GET_ENDIAN                   19
#define LINIX_PR_SET_ENDIAN                   20
#define LINIX_PR_GET_SECCOMP                  21
#define LINIX_PR_SET_SECCOMP                  22
#define LINIX_PR_CAPBSET_READ                 23
#define LINIX_PR_CAPBSET_DROP                 24
#define LINIX_PR_GET_TSC                      25
#define LINIX_PR_SET_TSC                      26
#define LINIX_PR_GET_SECUREBITS               27
#define LINIX_PR_SET_SECUREBITS               28
#define LINIX_PR_SET_TIMERSLACK               29
#define LINIX_PR_GET_TIMERSLACK               30
#define LINIX_PR_TASK_PERF_EVENTS_DISABLE     31
#define LINIX_PR_TASK_PERF_EVENTS_ENABLE      32
#define LINIX_PR_MCE_KILL                     33
#define LINIX_PR_MCE_KILL_GET                 34
#define LINIX_PR_SET_MM                       35
#define LINIX_PR_SET_CHILD_SUBREAPER          36
#define LINIX_PR_GET_CHILD_SUBREAPER          37
#define LINIX_PR_SET_NO_NEW_PRIVS             38
#define LINIX_PR_GET_NO_NEW_PRIVS             39
#define LINIX_PR_GET_TID_ADDRESS              40
#define LINIX_PR_SET_THP_DISABLE              41
#define LINIX_PR_GET_THP_DISABLE              42
#define LINIX_PR_MPX_ENABLE_MANAGEMENT        43
#define LINIX_PR_MPX_DISABLE_MANAGEMENT       44
#define LINIX_PR_SET_FP_MODE                  45
#define LINIX_PR_GET_FP_MODE                  46
#define LINIX_PR_CAP_AMBIENT                  47

#endif	/* !_LINUX_FUTEX_H */
