#include "common.h"
#include "noah.h"

#include "linux/common.h"
#include "linux/misc.h"
#include "linux/random.h"

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/sysctl.h>

DEFINE_SYSCALL(sysinfo, gaddr_t, info_ptr)
{
  struct l_sysinfo info;
  size_t len;

  struct timeval boottime;
  len = sizeof boottime;
  if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) < 0) exit(1);
  info.uptime = boottime.tv_sec;

  double loadavg[3];
  if (getloadavg(loadavg, sizeof loadavg / sizeof loadavg[0]) < 0) {
    panic("sysinfo");
  }

  info.loads[0] = loadavg[0] * LINUX_SYSINFO_LOADS_SCALE;
  info.loads[1] = loadavg[1] * LINUX_SYSINFO_LOADS_SCALE;
  info.loads[2] = loadavg[2] * LINUX_SYSINFO_LOADS_SCALE;

  int64_t memsize;
  len = sizeof memsize;
  if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) < 0){
    perror("sysinfo:");
    exit(1);
  }
  info.totalram = memsize;

  int64_t freepages;
  len = sizeof freepages;
  if (sysctlbyname("vm.page_free_count", &freepages, &len, NULL, 0) < 0){
    perror("sysinfo:");
    exit(1);
  }
  info.freeram = freepages * 0x1000;

  /*
   * sysctlbyname() changed in macos 15. Any older os will leave swapinfo[4] as 0.
   */
  
  uint64_t swapinfo[4] = {0};
  len = sizeof swapinfo;
  if (sysctlbyname("vm.swapusage", &swapinfo, &len, NULL, 0) < 0){
    perror("sysinfo:");
    exit(1);
  }
  info.totalswap = swapinfo[0];
  
  if(swapinfo[3] == 0)
    info.freeswap = swapinfo[2];
  else
    info.freeswap = swapinfo[1];

  /* TODO */
  info.sharedram = 0;
  info.bufferram = 0;
  info.procs = 100;
  info.totalhigh = 0;
  info.freehigh = 0;

  info.mem_unit = 1;

  if (copy_to_user(info_ptr, &info, sizeof info)) {
    return -LINUX_EFAULT;
  }

  return 0;
}

DEFINE_SYSCALL(getrandom, gaddr_t, buf_ptr, size_t, count, unsigned, flags)
{
  if (flags & ~(GRND_RANDOM | GRND_NONBLOCK)) {
    return -LINUX_EINVAL;
  }
  const char *source;
  source = flags & GRND_RANDOM ? "/dev/random" : "/dev/urandom";
  int oflags = O_RDONLY;
  oflags |= flags & GRND_NONBLOCK ? O_NONBLOCK : 0;
  int fd = open(source, oflags);
  if (fd < 0) {
    printk("getrandom: logic flaw\n");
    return -darwin_to_linux_errno(errno);
  }
  char *buf = malloc(count);
  int r = syswrap(read(fd, buf, count));
  if (r < 0) {
    goto out;
  }
  if (copy_to_user(buf_ptr, buf, count)) {
    r = -LINUX_EFAULT;
  }
out:
  free(buf);
  return r;
}
