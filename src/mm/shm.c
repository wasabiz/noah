#include "common.h"
#include "noah.h"
#include "vmm.h"
#include "mm.h"
#include "linux/common.h"
#include "linux/futex.h"
#include "linux/time.h"
#include "linux/ipc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

DEFINE_SYSCALL(shmget, l_key_t, l_key, size_t, size, int, l_shmflg)
{
  int shmflg = 0;
  if (l_shmflg & LINUX_IPC_EXCL)
    shmflg |= IPC_EXCL;
  if (l_shmflg & LINUX_IPC_CREAT)
    shmflg |= IPC_CREAT;
  key_t key = IPC_PRIVATE;
  if (l_key != LINUX_IPC_PRIVATE) {
    warnk("shmget: unknown key is given: %d\n", l_key);
    key = l_key;
  }
  return syswrap(shmget(key, size, shmflg));
}

DEFINE_SYSCALL(shmat, int, shmid, gaddr_t, addr, int, shmflg)
{
  if (addr != 0) {
    warnk("shmat: non-zero address is given: 0x%llx", addr);
    return -LINUX_ENOSYS;
  }
  assert(shmflg == 0);
  void *ptr = shmat(shmid, NULL, shmflg);
  if (ptr == (void *) -1) {
    return -linux_to_darwin_errno(errno);
  }
  struct shmid_ds ds;
  shmctl(shmid, IPC_STAT, &ds);
  size_t len = ds.shm_segsz;
  pthread_rwlock_wrlock(&proc.mm->alloc_lock);
  addr = alloc_region(len);
  do_munmap(addr, len);
  record_region(proc.mm, ptr, addr, len, LINUX_PROT_READ | LINUX_PROT_WRITE, LINUX_MAP_PRIVATE | LINUX_MAP_FIXED, -1, 0);
  vmm_mmap(addr, len, HV_MEMORY_READ | HV_MEMORY_WRITE, ptr);
  pthread_rwlock_unlock(&proc.mm->alloc_lock);
  return (uint64_t) addr;
}

DEFINE_SYSCALL(shmctl, int, shmid, int, cmd, gaddr_t, buf_ptr)
{
  switch (cmd) {
  case LINUX_IPC_RMID:
    return syswrap(shmctl(shmid, IPC_RMID, NULL));
  default:
    warnk("shmctl: unimplemented cmd = %d\n", cmd);
    return -LINUX_ENOSYS;
  }
}
