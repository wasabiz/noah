#include "common.h"
#include "noah.h"

#include "linux/common.h"
#include "linux/misc.h"
#include "linux/time.h"
#include "linux/fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <ctype.h>

#include <time.h>
#include <sys/time.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <mach/clock.h>
#include <mach/mach.h>

DEFINE_SYSCALL(time, gaddr_t, tloc_ptr)
{
  time_t t;
  int r = syswrap(time(&t));
  if (r < 0) {
    return r;
  }
  l_time_t lt = t;
  if (tloc_ptr != 0 && copy_to_user(tloc_ptr, &lt, sizeof lt))
    return -LINUX_EFAULT;
  return r;
}

DEFINE_SYSCALL(gettimeofday, gaddr_t, tp_ptr, gaddr_t, tzp_ptr)
{
  struct timeval tp;
  struct timezone tzp;
  gettimeofday(&tp, &tzp);
  if (tp_ptr != 0) {
    struct l_timeval l_tp;
    l_tp.tv_sec = tp.tv_sec;
    l_tp.tv_usec = tp.tv_usec;
    if (copy_to_user(tp_ptr, &l_tp, sizeof l_tp))
      return -LINUX_EINVAL;
  }
  if (tzp_ptr != 0) {
    struct l_timezone l_tzp;
    l_tzp.tz_minuteswest = tzp.tz_minuteswest;
    l_tzp.tz_dsttime = tzp.tz_dsttime;
    if (copy_to_user(tzp_ptr, &l_tzp, sizeof l_tzp))
      return -LINUX_EINVAL;
  }
  return 0;
}

DEFINE_SYSCALL(nanosleep, gaddr_t, rqtp_ptr, gaddr_t, rmtp_ptr)
{
  struct l_timespec rqtp, rmtp;
  if (copy_from_user(&rqtp, rqtp_ptr, sizeof rqtp))
    return -LINUX_EFAULT;
  struct timespec rq, rm;
  rq.tv_sec = rqtp.tv_sec;
  rq.tv_nsec = rqtp.tv_nsec;
  int r = syswrap(nanosleep(&rq, &rm));
  if (r < 0) {
    return r;
  }
  if (rmtp_ptr != 0) {
    rmtp.tv_sec = rm.tv_sec;
    rmtp.tv_nsec = rm.tv_nsec;
    if (copy_to_user(rmtp_ptr, &rmtp, sizeof rmtp))
      return -LINUX_EFAULT;
  }
  return r;
}

DEFINE_SYSCALL(utimensat, int, dirfd, gaddr_t, filename, gaddr_t, times_ptr, int, flags)
{
  int fd;
  if (filename == 0) {
    fd = dirfd; // Linux allows filename to be NULL and treat dirfd as fd
  } else {
    char name[LINUX_PATH_MAX];
    if (strncpy_from_user(name, filename, sizeof name) < 0)
      return -LINUX_EFAULT;
    int lflags = 0;
    if (flags & LINUX_AT_SYMLINK_NOFOLLOW) {
      lflags |= LINUX_O_NOFOLLOW | LINUX_O_PATH;
    }
    fd = openat_darwinfs(dirfd, name, lflags);
    if (fd < 0)
      return fd;
  }
  // HFS+ suppots only second precision timestamp
  struct timeval times[2], *tp = NULL;
  int r;
  if (times_ptr != 0) {
    struct l_timespec l_times[2];
    if (copy_from_user(l_times, times_ptr, sizeof l_times)) {
      r = -LINUX_EFAULT;
      goto out;
    }
    times[0].tv_sec = l_times[0].tv_sec;
    times[0].tv_usec = l_times[0].tv_nsec / 1000;
    times[1].tv_sec = l_times[1].tv_sec;
    times[1].tv_usec = l_times[1].tv_nsec / 1000;
    if (l_times[0].tv_nsec == LINUX_UTIME_NOW || l_times[1].tv_nsec == LINUX_UTIME_NOW) {
      struct timeval t;
      gettimeofday(&t, NULL);
      if (l_times[0].tv_nsec == LINUX_UTIME_NOW)
        times[0] = t;
      if (l_times[1].tv_nsec == LINUX_UTIME_NOW)
        times[1] = t;
    }
    if (l_times[0].tv_nsec == LINUX_UTIME_OMIT || l_times[1].tv_nsec == LINUX_UTIME_OMIT) {
      struct stat st;
      if (fstat(fd, &st) < 0) {
        r = -LINUX_EPERM;
        goto out;
      }
      if (l_times[0].tv_nsec == LINUX_UTIME_OMIT) {
        times[0].tv_sec = st.st_atimespec.tv_sec;
        times[0].tv_usec = st.st_atimespec.tv_nsec / 1000;
      }
      if (l_times[1].tv_nsec == LINUX_UTIME_OMIT) {
        times[1].tv_sec = st.st_mtimespec.tv_sec;
        times[1].tv_usec = st.st_mtimespec.tv_nsec / 1000;
      }
    }
    tp = times;
  }
  r = syswrap(futimes(fd, tp));
 out:
  if (fd != dirfd) {
    close(fd);
  }
  return r;
}

DEFINE_SYSCALL(utimes, gaddr_t, filename, gaddr_t, times_ptr)
{
  return sys_utimensat(LINUX_AT_FDCWD, filename, times_ptr, 0);
}

clockid_t
linux_to_darwin_clockid(l_clockid_t id)
{
  switch (id) {
  case LINUX_CLOCK_REALTIME:
    return CLOCK_REALTIME;
  case LINUX_CLOCK_MONOTONIC:
  case LINUX_CLOCK_MONOTONIC_COARSE:
    return CLOCK_MONOTONIC;
  case LINUX_CLOCK_PROCESS_CPUTIME_ID:
    return CLOCK_PROCESS_CPUTIME_ID;
  case LINUX_CLOCK_THREAD_CPUTIME_ID:
    return CLOCK_THREAD_CPUTIME_ID;
  default:
    printk("unsupported clockid\n");
    abort();
  }
}

DEFINE_SYSCALL(clock_gettime, l_clockid_t, id, gaddr_t, spec_ptr)
{
  struct timespec ts;
  int r = syswrap(clock_gettime(linux_to_darwin_clockid(id), &ts));
  if (r < 0) {
    return r;
  }
  if (spec_ptr != 0 && copy_to_user(spec_ptr, &ts, sizeof ts)) {
    return -LINUX_EFAULT;
  }
  return 0;
}
