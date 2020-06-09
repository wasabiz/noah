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

#include <utime.h>
#include <sys/time.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <mach/clock.h>
#include <mach/mach.h>

#if defined(MACOS_PRE_16)
#include <mach/mach_time.h>
#define TIMER_ABSTIME -1
#define CLOCK_REALTIME CALENDAR_CLOCK
#define CLOCK_MONOTONIC SYSTEM_CLOCK
#define CLOCK_PROCESS_CPUTIME_ID       2
#define CLOCK_THREAD_CPUTIME_ID        3

typedef int clockid_t;

int clock_gettime(clockid_t clk_id, struct timespec *t){
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t time;
    time = mach_absolute_time();
    double nseconds = ((double)time * (double)timebase.numer)/((double)timebase.denom);
    double seconds = ((double)time * (double)timebase.numer)/((double)timebase.denom * 1e9);
    t->tv_sec = seconds;
    t->tv_nsec = nseconds;
    return 0;
}

int clock_getres(clockid_t clk_id, struct timespec *t){
    return 0;
}
#else
#include <time.h>
#endif


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

int
do_utimensat(int dirfd, const char *name, const struct l_timespec *l_times, int flags)
{
  int fd;
  if (name == 0) {
    fd = dirfd; // Linux allows filename to be NULL and treat dirfd as fd
  } else {
    int lflags = 0;
    if (flags & LINUX_AT_SYMLINK_NOFOLLOW) {
      lflags |= LINUX_O_NOFOLLOW | LINUX_O_PATH;
    }
    fd = vkern_openat(dirfd, name, lflags, 0);
    if (fd < 0)
      return fd;
  }
  // HFS+ suppots only second precision timestamp
  struct timeval times[2], *tp = NULL;
  int r;
  if (l_times != NULL) {
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
    vkern_close(fd);
  }
  return r;
}

DEFINE_SYSCALL(utimensat, int, dirfd, gaddr_t, filename, gaddr_t, times_ptr, int, flags)
{
  char name[LINUX_PATH_MAX];
  if (filename != 0) {
    if (strncpy_from_user(name, filename, sizeof name) < 0)
      return -LINUX_EFAULT;
  }
  
  struct l_timespec l_times[2];
  if (times_ptr != 0) {
    if (copy_from_user(l_times, times_ptr, sizeof l_times)) {
      return -LINUX_EFAULT;
    }
  }
  
  return do_utimensat(dirfd, filename == 0 ? NULL : name, times_ptr == 0 ? NULL : l_times, flags);
}

DEFINE_SYSCALL(utimes, gaddr_t, filename, gaddr_t, times_ptr)
{
  return sys_utimensat(LINUX_AT_FDCWD, filename, times_ptr, 0);
}

DEFINE_SYSCALL(utime, gaddr_t, path, gaddr_t, times)
{
  char name[LINUX_PATH_MAX];
  if (strncpy_from_user(name, path, sizeof name) < 0)
    return -LINUX_EFAULT;
  
  struct l_timespec l_time[2];
  struct utimbuf buf;
  if (times != 0) {
    if (copy_from_user(&buf, times, sizeof buf)) {
      return -LINUX_EFAULT;
    }
    l_time[0].tv_sec = buf.actime;
    l_time[0].tv_nsec = 0;
    l_time[1].tv_sec = buf.modtime;
    l_time[1].tv_nsec = 0;
  }
  return do_utimensat(LINUX_AT_FDCWD, name, times == 0 ? NULL : l_time, 0);
}

clockid_t
linux_to_darwin_clockid(l_clockid_t id)
{
  switch (id) {
  case LINUX_CLOCK_REALTIME:
  case LINUX_CLOCK_REALTIME_HR:
    return CLOCK_REALTIME;
  case LINUX_CLOCK_MONOTONIC:
  case LINUX_CLOCK_MONOTONIC_COARSE:
    return CLOCK_MONOTONIC;
  case LINUX_CLOCK_PROCESS_CPUTIME_ID:
    return CLOCK_PROCESS_CPUTIME_ID;
  case LINUX_CLOCK_THREAD_CPUTIME_ID:
    return CLOCK_THREAD_CPUTIME_ID;
  default:
    panic("linux_to_darwin_clockid: %d", id);
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

DEFINE_SYSCALL(clock_getres, l_clockid_t, id, gaddr_t, res_ptr)
{
  struct timespec ts;
  int r = syswrap(clock_getres(linux_to_darwin_clockid(id), &ts));
  if (r < 0) {
    return r;
  }
  if (res_ptr != 0 && copy_to_user(res_ptr, &ts, sizeof ts)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(fdatasync, int, fildes)
{
  return syswrap(fsync(fildes));
}

void
darwin_to_linux_itimerval(const struct itimerval* d_val, struct l_itimerval* l_val) {
  l_val->it_interval.tv_sec  = d_val->it_interval.tv_sec;
  l_val->it_interval.tv_usec = d_val->it_interval.tv_usec;
  l_val->it_value.tv_sec     = d_val->it_value.tv_sec;
  l_val->it_value.tv_usec    = d_val->it_value.tv_usec;
}

void
linux_to_darwin_itimerval(const struct l_itimerval* l_val, struct itimerval* d_val) {
  d_val->it_interval.tv_sec  = l_val->it_interval.tv_sec;
  d_val->it_interval.tv_usec = l_val->it_interval.tv_usec;
  d_val->it_value.tv_sec     = l_val->it_value.tv_sec;
  d_val->it_value.tv_usec    = l_val->it_value.tv_usec;
}

DEFINE_SYSCALL(getitimer, int, which, gaddr_t, ret_ptr) {
  struct itimerval value;
  // Darwin's which is compatible with that of Linux
  int r = syswrap(getitimer(which, &value));
  if (r < 0) {
    return r;
  }
  struct l_itimerval l_itimerval;
  darwin_to_linux_itimerval(&value, &l_itimerval);
  if (copy_to_user(ret_ptr, &l_itimerval, sizeof l_itimerval)) {
    return -LINUX_EFAULT;
  }
  return r;
}


DEFINE_SYSCALL(setitimer, int, which, gaddr_t, new_ptr, gaddr_t, old_ptr) {
  struct l_itimerval l_newvalue, l_oldvalue;
  struct itimerval newvalue, oldvalue;
  if (new_ptr == 0) {
    return sys_getitimer(which, old_ptr);
  }
  if (copy_from_user(&l_newvalue, new_ptr, sizeof l_newvalue)) {
    return -LINUX_EFAULT;
  }
  linux_to_darwin_itimerval(&l_newvalue, &newvalue);
  // Darwin's which is compatible with that of Linux
  int r = syswrap(setitimer(which, &newvalue, &oldvalue));
  if (r < 0) {
    return r;
  }
  if (old_ptr != 0) {
    darwin_to_linux_itimerval(&oldvalue, &l_oldvalue);
    if (copy_to_user(old_ptr, &l_oldvalue, sizeof l_oldvalue)) {
      return -LINUX_EFAULT;
    }
  }
  return r;
}

