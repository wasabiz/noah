#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>

#include "common.h"
#include "noah.h"
#include "vmm.h"

#include "linux/common.h"
#include "linux/misc.h"
#include "linux/signal.h"

static void
init_task(unsigned long clone_flags, gaddr_t child_tid, gaddr_t tls)
{
  task.set_child_tid = task.clear_child_tid = 0;
  if (clone_flags & LINUX_CLONE_CHILD_SETTID) {
    task.set_child_tid = child_tid;
  }
  if (clone_flags & LINUX_CLONE_CHILD_CLEARTID) {
    task.clear_child_tid = child_tid;
  }

  if (task.set_child_tid != 0) {
    int pid = getpid();
    if (copy_to_user(task.set_child_tid, &pid, sizeof pid)) {
      printk("fixme");
      assert(false);
    }
  }

  LINUX_SIGEMPTYSET(&task.sigmask);
  sigbits_emptyset(&task.sigpending);

  if (clone_flags & LINUX_CLONE_SETTLS) {
    vmm_write_vmcs(VMCS_GUEST_FS_BASE, tls);
  }
}

int
__do_clone_process(unsigned long clone_flags, unsigned long newsp, gaddr_t parent_tid, gaddr_t child_tid, gaddr_t tls)
{
  // Because Apple Hypervisor Framwork won't let us use multiple VMs,
  // we destroy the current vm and restore it later
  struct vmm_snapshot snapshot;
  vmm_snapshot(&snapshot);
  vmm_destroy();

  pthread_rwlock_wrlock(&proc.vfs_lock);
  for (uint i = 0; i < proc.nr_fdtab; ++i) {
    if (proc.fdtab[i] != NULL)
      file_incref(proc.fdtab[i]);
  }
  int ret = syswrap(fork());
  if (ret < 0) {
    for (uint i = 0; i < proc.nr_fdtab; ++i) {
      if (proc.fdtab[i] != NULL)
        file_decref(proc.fdtab[i]);
    }
  }
  pthread_rwlock_unlock(&proc.vfs_lock);

  vmm_reentry(&snapshot);

  if (ret < 0) {
    return ret;
  }

  if (ret == 0) {
    init_task(clone_flags, child_tid, tls);
  } else {
    if (clone_flags & LINUX_CLONE_PARENT_SETTID) {
      if (copy_to_user(parent_tid, &ret, sizeof ret)) {
        return -LINUX_EFAULT;
      }
    }
  }

  return ret;
}

struct clone_thread_arg {
  unsigned long clone_flags;
  unsigned long newsp;
  gaddr_t parent_tid;
  gaddr_t child_tid;
  gaddr_t tls;
  struct vcpu_snapshot *vcpu_snapshot;
};

static void*
__start_thread(struct clone_thread_arg *arg)
{
  uint64_t rip;

  printk("__start_thread\n");

  vmm_create_vcpu(arg->vcpu_snapshot);
  vmm_write_register(HV_X86_RAX, 0);
  vmm_write_register(HV_X86_RSP, arg->newsp);
  vmm_read_register(HV_X86_RIP, &rip);
  vmm_write_register(HV_X86_RIP, rip + 2);

  pthread_rwlock_wrlock(&proc.lock);
  proc.nr_tasks++;
  list_add(&task.tasks, &proc.tasks);
  pthread_rwlock_unlock(&proc.lock);

  sigbits_emptyset(&task.sigpending);

  init_task(arg->clone_flags, arg->child_tid, arg->tls);

  free(arg->vcpu_snapshot);
  free(arg);

  main_loop(0);

  return NULL; // hv_vcpu_run failed for some reason
}

int
__do_clone_thread(unsigned long clone_flags, unsigned long newsp, gaddr_t parent_tid, gaddr_t child_tid, gaddr_t tls)
{
  printk("clone_thread\n");
  uint64_t tid;
  pthread_t threadid;

  struct vcpu_snapshot *snapshot = malloc(sizeof(struct vcpu_snapshot));
  struct clone_thread_arg *arg = malloc(sizeof(struct clone_thread_arg));
  *arg = (struct clone_thread_arg){clone_flags, newsp, parent_tid, child_tid, tls, snapshot};
  vmm_snapshot_vcpu(snapshot);
  pthread_create(&threadid, NULL, (void *)__start_thread, arg);
  pthread_threadid_np(threadid, &tid);

  return tid;
}

int
do_clone(unsigned long clone_flags, unsigned long newsp, gaddr_t parent_tid, gaddr_t child_tid, gaddr_t tls)
{
  int sigtype = clone_flags & 0xff;
  assert(sigtype == LINUX_SIGCHLD || sigtype == 0);

  clone_flags &= -0x100;
  unsigned long implemented = LINUX_CLONE_THREAD | LINUX_CLONE_DETACHED | LINUX_CLONE_SETTLS | LINUX_CLONE_CHILD_SETTID | LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_PARENT_SETTID;
  unsigned long needed = 0;
  if (clone_flags & LINUX_CLONE_THREAD) {
    int needed = LINUX_CLONE_VM | LINUX_CLONE_FS | LINUX_CLONE_FILES | LINUX_CLONE_SIGHAND | LINUX_CLONE_SYSVSEM;
    implemented |= needed;
  }
  if ((clone_flags & ~implemented) || (clone_flags & needed) != needed) {
    warnk("unsupported clone_flags: %lx\n", clone_flags);
    return -LINUX_EINVAL;
  }


  if (clone_flags & LINUX_CLONE_THREAD) {
    return __do_clone_thread(clone_flags, newsp, parent_tid, child_tid, tls);
  } else {
    return __do_clone_process(clone_flags, newsp, parent_tid, child_tid, tls);
  }
}

DEFINE_SYSCALL(clone, unsigned long, clone_flags, unsigned long, newsp, gaddr_t, parent_tid, gaddr_t, child_tid, gaddr_t, tls)
{
  return do_clone(clone_flags, newsp, parent_tid, child_tid, tls);
}

DEFINE_SYSCALL(fork)
{
  return do_clone(LINUX_SIGCHLD, 0, 0, 0, 0);
}

DEFINE_SYSCALL(vfork)
{
  return do_clone(LINUX_SIGCHLD, 0, 0, 0, 0);
}
