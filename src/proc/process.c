#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pthread.h>
#include <assert.h>

#include "common.h"
#include "noah.h"
#include "vmm.h"
#include "mm.h"

#include "linux/common.h"
#include "linux/misc.h"
#include "linux/errno.h"
#include "linux/futex.h"

#include <sys/sysctl.h>

#define _GNU_SOURCE
#include <sys/syscall.h>

struct proc proc;
_Thread_local struct task task;

DEFINE_SYSCALL(sched_yield)
{
  sleep(0);
  return 0;
}

DEFINE_SYSCALL(getpid)
{
  return syswrap(syscall(SYS_getpid));
}

DEFINE_SYSCALL(getuid)
{
  pthread_rwlock_rdlock(&proc.cred.lock);
  int ret = proc.cred.uid;
  pthread_rwlock_unlock(&proc.cred.lock);
  return ret;
}

DEFINE_SYSCALL(getgid)
{
  return syswrap(getgid());
}

static inline int
darwin_getsuid()
{
  struct kinfo_proc kinfo;
  size_t len = sizeof kinfo;
  int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  int ret  = sysctl(name, 4, &kinfo, &len, NULL, 0);
  if (ret < 0) {
    return ret;
  }
  return kinfo.kp_eproc.e_pcred.p_svuid;
}

static inline bool
can_setresxid(l_uid_t id)
{
  return proc.cred.euid == 0 || proc.cred.euid == id || proc.cred.uid == id || proc.cred.suid == id;
}

int
do_setresuid(l_uid_t ruid, l_uid_t euid, l_uid_t suid)
{
  if (ruid != (l_uid_t) -1) {
    if (!can_setresxid(ruid)) {
      return -LINUX_EPERM;
    }
    if (ruid != 0 && proc.cred.euid != 0) {
      // The app is setting ruid to NON-root while euid is also NON-root.
      // To keep saved set-user-ID as 0, we have to set euid to 0 once due to bahavior of Darwin's setruid and setreuid.
      // To implement it, we need some kind of global lock for security. We haven't implemented that and just panic now.
      panic("Noah cannot setruid to non-root while euid is non-root currently");

      /* Implementation would be like this...
       acquire_global_lock_to_stop_other_threads();
       if (syswrap(seteuid(0)) < 0) {
       goto cred_management_err;
       }
       ret = syswrap(setruid(new_ruid));
       seteuid(previous_euid);
       unlock_it();
       */
    }
    proc.cred.uid = ruid;
    if (syswrap(setruid(ruid)) < 0) {
      goto cred_management_err;
    }
  }
  if (euid != (l_uid_t) -1) {
    if (!can_setresxid(euid)) {
      return -LINUX_EPERM;
    }
    if (proc.cred.euid != 0 && euid != proc.cred.uid && proc.cred.suid != 0) {
      assert(euid == proc.cred.suid);
      // The app is getting back from NON-root euid to NON-root saved user ID.
      // We have to get root privilege once for it.
      // To implement it, we need some kind of global lock for security. We haven't implement that and just panic now.
      panic("Noah cannot setuid from non-root euid to non-root saved set-user-ID currently");
    }
    proc.cred.euid = euid;
    if (syswrap(seteuid(euid)) < 0) {
      goto cred_management_err;
    }
  }
  if (suid != (l_uid_t) -1) {
    if (!can_setresxid(suid)) {
      return -LINUX_EPERM;
    }
    proc.cred.suid = suid;
  }
  
  return 0;
  
cred_management_err:
  panic("Cannot setresuid [%d, %d, %d] -> [%d, %d, %d]. Credential management bug of Noah. Host cred is [%d, %d, %d]",
          proc.cred.uid, proc.cred.euid, proc.cred.suid, ruid, euid, suid, getuid(), geteuid(), darwin_getsuid());
}


DEFINE_SYSCALL(setresuid, l_uid_t, ruid, l_uid_t, euid, l_uid_t, suid)
{
  pthread_rwlock_wrlock(&proc.cred.lock);
  int ret = do_setresuid(ruid, euid, suid);
  pthread_rwlock_unlock(&proc.cred.lock);
  return ret;
}

DEFINE_SYSCALL(getresuid, gaddr_t, ruid, gaddr_t, euid, gaddr_t, suid)
{
  int ret = 0;
  pthread_rwlock_rdlock(&proc.cred.lock);
  if (copy_to_user(ruid, &proc.cred.uid, sizeof proc.cred.uid)) {
    ret = -LINUX_EFAULT;
    goto out;
  }
  if (copy_to_user(euid, &proc.cred.euid, sizeof proc.cred.euid)) {
    ret = -LINUX_EFAULT;
    goto out;
  }
  if (copy_to_user(suid, &proc.cred.suid, sizeof proc.cred.suid)) {
    ret = -LINUX_EFAULT;
    goto out;
  }
  
out:
  pthread_rwlock_unlock(&proc.cred.lock);
  return ret;
}

// Linux's setuid is not compatible with POSIX.1-2001.
// It does not set real uid and saved-uid unless the current uid is capable of CAP_SETUID (that means root in Noah now)
DEFINE_SYSCALL(setuid, l_uid_t, uid)
{
  if (uid == (l_uid_t) -1) {
    return -LINUX_EINVAL;
  }
  int ret = 0;
  pthread_rwlock_wrlock(&proc.cred.lock);
  l_uid_t new_ruid = -1, new_euid = -1, new_suid = -1;
  if (proc.cred.euid == 0) {
    new_suid = new_ruid = uid;
  } else if (proc.cred.uid != uid && proc.cred.suid != uid) {
    ret = -LINUX_EPERM;
    goto out;
  }
  new_euid = uid;
  ret = do_setresuid(new_ruid, new_euid, new_suid);
out:
  pthread_rwlock_unlock(&proc.cred.lock);
  return ret;
}

DEFINE_SYSCALL(setgid, l_gid_t, gid)
{
  return syswrap(setgid(gid));
}

DEFINE_SYSCALL(geteuid)
{
  pthread_rwlock_rdlock(&proc.cred.lock);
  int ret = proc.cred.euid;
  pthread_rwlock_unlock(&proc.cred.lock);
  return ret;
}

DEFINE_SYSCALL(getegid)
{
  return syswrap(getegid());
}

DEFINE_SYSCALL(setresgid, l_gid_t, rgid, l_gid_t, egid, l_gid_t, sgid)
{
  return syswrap(setregid(rgid, egid));
}

DEFINE_SYSCALL(getresgid, gaddr_t, rgid, gaddr_t, egid, gaddr_t, sgid)
{
  int n;
  n = getgid();
  if (copy_to_user(rgid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = getegid();
  if (copy_to_user(egid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  n = getgid();
  if (copy_to_user(sgid, &n, sizeof n)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(setsid)
{
  return syswrap(setsid());
}

DEFINE_SYSCALL(setpgid, pid_t, pid, pid_t, pgid)
{
  return syswrap(setpgid(pid, pgid));
}

DEFINE_SYSCALL(getppid)
{
  return syswrap(getppid());
}

DEFINE_SYSCALL(getpgrp)
{
  return syswrap(getpgrp());
}

DEFINE_SYSCALL(getpgid, l_pid_t, pid)
{
  return syswrap(getpgid(pid));
}

DEFINE_SYSCALL(getsid, l_pid_t, pid)
{
  return syswrap(getsid(pid));
}

DEFINE_SYSCALL(getgroups, int, gidsetsize, gaddr_t, grouplist_ptr)
{
  gid_t *gl = malloc(gidsetsize * sizeof(gid_t));
  int r = syswrap(getgroups(gidsetsize, gidsetsize == 0 ? NULL : gl));
  if (r < 0) {
    goto out;
  }
  if (copy_to_user(grouplist_ptr, gl, gidsetsize * sizeof(gid_t))) {
    r = -LINUX_EFAULT;
  }
out:
  free(gl);
  return r;
}

DEFINE_SYSCALL(setgroups, int, gidsetsize, gaddr_t, grouplist_ptr)
{
  int r;
  gid_t *gl = malloc(gidsetsize * sizeof(gid_t));
  if (copy_from_user(gl, grouplist_ptr, gidsetsize * sizeof(gid_t))) {
    r = -LINUX_EFAULT;
    goto out;
  }
  r = syswrap(setgroups(gidsetsize, gl));
out:
  free(gl);
  return r;
}

uint64_t do_gettid()
{
  return task.tid;
}

DEFINE_SYSCALL(gettid)
{
  return do_gettid();
}

int linux_to_darwin_rlimopts(int l_resource) {
  int resource = 0;
  switch (l_resource) {
  case LINUX_RLIMIT_CPU: resource = RLIMIT_CPU; break;
  case LINUX_RLIMIT_FSIZE: resource = RLIMIT_FSIZE; break;
  case LINUX_RLIMIT_DATA: resource = RLIMIT_DATA; break;
  case LINUX_RLIMIT_STACK: resource = RLIMIT_STACK; break;
  case LINUX_RLIMIT_CORE: resource = RLIMIT_CORE; break;
  case LINUX_RLIMIT_RSS: resource = RLIMIT_RSS; break;
  case LINUX_RLIMIT_NPROC: resource = RLIMIT_NPROC; break;
  case LINUX_RLIMIT_NOFILE: resource = RLIMIT_NOFILE; break;
  case LINUX_RLIMIT_MEMLOCK: resource = RLIMIT_MEMLOCK; break;
  case LINUX_RLIMIT_AS: resource = RLIMIT_AS; break;
  }
  return resource;
}

DEFINE_SYSCALL(getrlimit, int, l_resource, gaddr_t, rl_ptr)
{
  struct rlimit rl;
  struct l_rlimit l_rl;

  if (l_resource >= LINUX_RLIM_NLIMITS) {
    return -LINUX_EINVAL;
  }

  int resource = linux_to_darwin_rlimopts(l_resource);

  int r = syswrap(getrlimit(resource, &rl));
  if (r < 0)
    return r;

  darwin_to_linux_rlimit(resource, &rl, &l_rl);
  if (copy_to_user(rl_ptr, &l_rl, sizeof l_rl))
    return -LINUX_EFAULT;

  return r;
}

DEFINE_SYSCALL(setrlimit, unsigned int, resource, gaddr_t, rlim)
{
  warnk("setrlimit is not implemented\n");
  return -LINUX_ENOSYS;
}

DEFINE_SYSCALL(exit, int, reason)
{
  if (task.clear_child_tid) {
    int zero = 0;
    if (copy_to_user(task.clear_child_tid, &zero, sizeof zero))
      return -LINUX_EFAULT;
    do_futex_wake(task.clear_child_tid, 1);
  }
  vmm_destroy_vcpu();
  pthread_rwlock_wrlock(&proc.lock);
  if (proc.nr_tasks == 1) {
    _exit(reason);
  } else {
    proc.nr_tasks--;
    list_del(&task.head);
    pthread_rwlock_unlock(&proc.lock);
    pthread_exit(&reason);
  }
}

DEFINE_SYSCALL(exit_group, int, reason)
{
  if (task.clear_child_tid) {
    int zero = 0;
    if (copy_to_user(task.clear_child_tid, &zero, sizeof zero))
      return -LINUX_EFAULT;
    do_futex_wake(task.clear_child_tid, 1);
  }
  _exit(reason);
}

DEFINE_SYSCALL(tgkill)
{
  printk("unimplemented syscall: tgkill\n");
  return -LINUX_ENOSYS;
}

DEFINE_SYSCALL(capget, gaddr_t, header_ptr, gaddr_t, data_ptr)
{
  printk("capget is unimplemented\n");
  return 0;
}

struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

DEFINE_SYSCALL(uname, gaddr_t, buf_ptr)
{
  struct utsname buf;

  strncpy(buf.sysname, "Linux", sizeof buf.sysname - 1);
  strncpy(buf.release, LINUX_RELEASE, sizeof buf.release - 1);
  strncpy(buf.version, LINUX_VERSION, sizeof buf.version - 1);
  strncpy(buf.machine, "x86_64", sizeof buf.machine - 1);
  strncpy(buf.domainname, "GNU/Linux", sizeof buf.domainname - 1);

  int err = syswrap(gethostname(buf.nodename, sizeof buf.nodename - 1));
  if (err < 0) {
    return err;
  }

  if (copy_to_user(buf_ptr, &buf, sizeof(struct utsname))) {
    return -LINUX_EFAULT;
  }

  return 0;
}

DEFINE_SYSCALL(prctl, int, option, unsigned long, arg1, unsigned long, arg2, unsigned long, arg3, unsigned long, arg4, unsigned long, arg5)
{
  switch (option) {
  case LINUX_PR_SET_NAME: {
    char buf[16];
    if (copy_from_user(buf, (gaddr_t)arg1, sizeof(buf))) {
      return -LINUX_EFAULT;
    }
    // trancate if the legnth of arg1 exceeds 16byte.
    buf[15] = '\0';
    pthread_setname_np(buf);
    return 0;
  }
  default:
    warnk("unkown prctl cmd: %d\n", option);
    return -LINUX_EINVAL;
  }
}

DEFINE_SYSCALL(arch_prctl, int, code, gaddr_t, addr)
{
  uint64_t t;

  switch (code) {
  case LINUX_ARCH_SET_GS:
    vmm_write_vmcs(VMCS_GUEST_GS_BASE, addr);
    return 0;
  case LINUX_ARCH_SET_FS:
    vmm_write_vmcs(VMCS_GUEST_FS_BASE, addr);
    return 0;
  case LINUX_ARCH_GET_FS:
    vmm_read_vmcs(VMCS_GUEST_FS_BASE, &t);
    if (copy_to_user(addr, &t, sizeof t))
      return -LINUX_EFAULT;
    return 0;
  case LINUX_ARCH_GET_GS:
    vmm_read_vmcs(VMCS_GUEST_GS_BASE, &t);
    if (copy_to_user(addr, &t, sizeof t))
      return -LINUX_EFAULT;
    return 0;
  default:
    return -LINUX_EINVAL;
  }
}

DEFINE_SYSCALL(set_tid_address, gaddr_t, tidptr)
{
  task.clear_child_tid = tidptr;
  return do_gettid();
}

DEFINE_SYSCALL(set_robust_list, gaddr_t, head, size_t, len)
{
  if (len != sizeof(struct linux_robust_list_head))
    return -EINVAL;
  task.robust_list = head;
  return 0;
}

int linux_to_darwin_waitopts(int options)
{
  int opts = 0;
  if (options & LINUX_WNOHANG) {
    opts |= WNOHANG;
    options &= ~LINUX_WNOHANG;
  }
  if (options & LINUX_WUNTRACED) {
    opts |= WUNTRACED;
    options &= ~LINUX_WUNTRACED;
  }
  if (options & LINUX_WCONTINUED) {
    opts |= WCONTINUED;
    options &= ~LINUX_WCONTINUED;
  }
  if (options & LINUX_WEXITED) {
    opts |= WEXITED;
    options &= ~LINUX_WEXITED;
  }
  if (options != 0) {
    warnk("unknown options given to wait4: 0x%x\n", options);
  }
  return opts;
}

DEFINE_SYSCALL(wait4, int, pid, gaddr_t, status_ptr, int, options, gaddr_t, rusage_ptr)
{
  int status;
  struct rusage rusage;

  int ret = syswrap(wait4(pid, &status, linux_to_darwin_waitopts(options), &rusage));
  if (ret < 0) {
    return ret;
  }

  if (rusage_ptr != 0) {
    if (copy_to_user(rusage_ptr, &rusage, sizeof rusage)) {
      return -LINUX_EFAULT;
    }
  }

  if (status_ptr != 0) {
    /*
     *  SSSS SSSS -000 0000 -> exited. S is return status
     *  .... .... CYYY YYYY -> signaled if Y != 0. Y is the signal number
     *  SSSS SSSS 0111 1111 -> stopped. S is the sigal number
     */
    int st = 0;
    if (WIFEXITED(status)) {
      st = WEXITSTATUS(status) << 8;
    } else if (WIFSIGNALED(status)) {
      st = darwin_to_linux_signal(WTERMSIG(status));
      if (WCOREDUMP(status))
        st |= 0x80;
    } else if (WIFSTOPPED(status)) {
      st = (darwin_to_linux_signal(WSTOPSIG(status)) << 8) | 0x7f;
    }
    if (copy_to_user(status_ptr, &st, sizeof st)) {
      return -LINUX_EFAULT;
    }
  }

  return ret;
}

static void
darwin_to_linux_rusage(struct rusage *ru, struct l_rusage *lru)
{
  lru->ru_utime.tv_sec = ru->ru_utime.tv_sec;
  lru->ru_utime.tv_usec = ru->ru_utime.tv_usec;
  lru->ru_stime.tv_sec = ru->ru_stime.tv_sec;
  lru->ru_stime.tv_usec = ru->ru_stime.tv_usec;
  lru->ru_maxrss = ru->ru_maxrss;
  lru->ru_ixrss = ru->ru_ixrss;
  lru->ru_idrss = ru->ru_idrss;
  lru->ru_isrss = ru->ru_isrss;
  lru->ru_minflt = ru->ru_minflt;
  lru->ru_majflt = ru->ru_majflt;
  lru->ru_nswap = ru->ru_nswap;
  lru->ru_inblock = ru->ru_inblock;
  lru->ru_oublock = ru->ru_oublock;
  lru->ru_msgsnd = ru->ru_msgsnd;
  lru->ru_msgrcv = ru->ru_msgrcv;
  lru->ru_nsignals = ru->ru_nsignals;
  lru->ru_nvcsw = ru->ru_nvcsw;
  lru->ru_nivcsw = ru->ru_nivcsw;
}

DEFINE_SYSCALL(getrusage, int, who, gaddr_t, rusage_ptr)
{
  struct rusage h_rusage;
  int r;
  if ((r = syswrap(getrusage(who, &h_rusage))) < 0) {
    return r;
  }

  struct l_rusage l_rusage;
  darwin_to_linux_rusage(&h_rusage, &l_rusage);

  if (copy_to_user(rusage_ptr, &l_rusage, sizeof l_rusage)) {
    return -LINUX_EFAULT;
  }
  return 0;
}

DEFINE_SYSCALL(getpriority, int, which, int, who)
{
  return syswrap(getpriority(which, who));
}

DEFINE_SYSCALL(setpriority, int, which, int, who, int, niceval)
{
  return syswrap(setpriority(which, who, niceval));
}

DEFINE_SYSCALL(sched_getaffinity, l_pid_t, pid, unsigned int, len, gaddr_t, user_mask_ptr)
{
  static const unsigned sizeof_cpumask_t = 32; /* FIXME */
  if (len < sizeof_cpumask_t)
    return -LINUX_EINVAL;
  unsigned char buf[sizeof_cpumask_t] = {0};
  buf[0] = 0x1;
  if (copy_to_user(user_mask_ptr, buf, sizeof buf))
    return -LINUX_EFAULT;
  return sizeof_cpumask_t;
}
