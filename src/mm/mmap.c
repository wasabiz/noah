#include "common.h"

#include "noah.h"
#include "vmm.h"
#include "mm.h"
#include "x86/vm.h"

#include "linux/mman.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mach/vm_statistics.h>
#include <pthread.h>

#include <Hypervisor/hv.h>

void
init_mmap(struct mm *mm)
{
  mm->current_mmap_top = 0x00000000c0000000;
}

gaddr_t
alloc_region(size_t len)
{
  len = roundup(len, PAGE_SIZEOF(PAGE_4KB));
  proc.mm->current_mmap_top += len;
  return proc.mm->current_mmap_top - len;
}

int
do_munmap(gaddr_t gaddr, size_t size)
{
  if (!is_page_aligned((void*)gaddr, PAGE_4KB)) {
    return -LINUX_EINVAL;
  }
  size = roundup(size, PAGE_SIZEOF(PAGE_4KB)); // Linux kernel also does this

  struct mm_region *overlapping = find_region_range(gaddr, size, proc.mm);
  if (overlapping == NULL) {
    return -LINUX_ENOMEM;
  }

  struct mm_region key = {.gaddr = gaddr, .size = size};
  while (region_compare(&key, overlapping) == 0) {
    if (overlapping->gaddr < gaddr) {
      split_region(proc.mm, overlapping, gaddr);
      overlapping = list_entry(overlapping->list.next, struct mm_region, list);
    }
    if (overlapping->gaddr + overlapping->size > gaddr + size) {
      split_region(proc.mm, overlapping, gaddr + size);
    }
    struct list_head *next = overlapping->list.next;
    list_del(&overlapping->list);
    RB_REMOVE(mm_region_tree, &proc.mm->mm_region_tree, overlapping);
    vmm_munmap(overlapping->gaddr, overlapping->size);
    munmap(overlapping->haddr, overlapping->size);
    free(overlapping);
    if (next == &proc.mm->mm_regions)
      break;
    overlapping = list_entry(next, struct mm_region, list);
  }

  return 0;
}

static int
linux_to_darwin_mflags(int l_flags)
{
  int d_flags = 0;
  if (l_flags & LINUX_MAP_SHARED) d_flags |= MAP_SHARED;
  if (l_flags & LINUX_MAP_PRIVATE) d_flags |= MAP_PRIVATE;
  if (l_flags & LINUX_MAP_ANON) d_flags |= MAP_ANON;
  if (l_flags & LINUX_MAP_HUGETLB) d_flags |= VM_FLAGS_SUPERPAGE_SIZE_ANY;
  return d_flags;
}

gaddr_t
do_mmap(gaddr_t addr, size_t len, int d_prot, int l_prot, int l_flags, int fd, off_t offset)
{
  assert((addr & 0xfff) == 0);

  /* some l_flags are obsolete and just ignored */
  l_flags &= ~LINUX_MAP_DENYWRITE;
  l_flags &= ~LINUX_MAP_EXECUTABLE;

  /* We ignore these currenlty */
  l_flags &= ~LINUX_MAP_NORESERVE;

  /* the linux kernel does nothing for LINUX_MAP_STACK */
  l_flags &= ~LINUX_MAP_STACK;

  len = roundup(len, PAGE_SIZEOF(PAGE_4KB));

  if ((l_flags & ~(LINUX_MAP_SHARED | LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANON | LINUX_MAP_HUGETLB)) != 0) {
    warnk("unsupported mmap l_flags: 0x%x\n", l_flags);
    exit(1);
  }
  if (l_flags & LINUX_MAP_ANON) {
    fd = -1;
    offset = 0;
  }
  if ((l_flags & LINUX_MAP_FIXED) == 0) {
    addr = alloc_region(len);
  }

  void *ptr = mmap(0, len, d_prot, linux_to_darwin_mflags(l_flags), fd, offset);
  if (ptr == MAP_FAILED) {
    return -darwin_to_linux_errno(errno);
  }

  do_munmap(addr, len);
  record_region(proc.mm, ptr, addr, len, l_prot, l_flags, fd, offset);

  vmm_mmap(addr, len, linux_mprot_to_hv_mflag(l_prot), ptr);

  return addr;
}

DEFINE_SYSCALL(mmap, gaddr_t, addr, size_t, len, int, prot, int, flags, int, fd, off_t, offset)
{
  uint64_t ret;
  pthread_rwlock_wrlock(&proc.mm->alloc_lock);
  ret = do_mmap(addr, len, prot, prot, flags, fd, offset);
  pthread_rwlock_unlock(&proc.mm->alloc_lock);
  return  ret;
}

DEFINE_SYSCALL(mremap, gaddr_t, old_addr, size_t, old_size, size_t, new_size, int, flags, gaddr_t, new_addr)
{
  if (flags & ~(LINUX_MREMAP_FIXED | LINUX_MREMAP_MAYMOVE)) {
    return -LINUX_EINVAL;
  }
  if (flags & LINUX_MREMAP_FIXED && !(flags & LINUX_MREMAP_MAYMOVE))
    return -LINUX_EINVAL;
  if (!is_page_aligned((void*)old_addr, PAGE_4KB))
    return -LINUX_EINVAL;
  if (!(flags & LINUX_MREMAP_MAYMOVE)) {
    warnk("unsupported mremap flags: 0x%x\n", flags);
    return -LINUX_EINVAL;
  }

  if (new_size == 0)
    return -LINUX_EINVAL;

  /* Linux kernel also does these aligning */
  old_size = roundup(old_size, PAGE_SIZEOF(PAGE_4KB));
  new_size = roundup(new_size, PAGE_SIZEOF(PAGE_4KB));

  gaddr_t ret = old_addr;

  pthread_rwlock_wrlock(&proc.mm->alloc_lock);

  struct mm_region *region = find_region(old_addr, proc.mm);
  /* Linux requires old_addr is the exact address of start of vm_area  */
  if (!region || region->gaddr != old_addr) {
    ret = -LINUX_EFAULT;
    goto out;
  }
  /* The region must not be across multiple regions */
  if (region->size < old_size) {
    ret = -LINUX_EFAULT;
    goto out;
  }

  /* new_size <= old_size. We can just shrink */
  if (new_size <= old_size) {
    munmap(region->haddr + new_size, region->size - new_size);
    vmm_munmap(region->gaddr + new_size, region->size - new_size);
    region->size = new_size;
    goto out;
  }

  /* new_size > old_size */
  void *moved_to = mmap(0, new_size, PROT_NONE, linux_to_darwin_mflags(region->mm_flags), region->mm_fd, region->pgoff);
  if (moved_to == MAP_FAILED) {
    panic("mremap failed. old_addr :0x%llx, old_size: 0x%lux, new_size: 0x%lux, flags:0x%ux, new_addr: 0x%llx, mm_flags: 0x%ux, mm_fd: %d", old_addr, old_size, new_size, flags, new_addr, region->mm_flags, region->mm_fd);
  }
  if (!(region->mm_flags & LINUX_MAP_ANONYMOUS)) {
    /* A file is mapped to this region. We have to take the file permission into account */
    int d_prot = 0;
    if (region->prot & LINUX_PROT_READ) d_prot |= PROT_READ;
    if (region->prot & LINUX_PROT_WRITE) d_prot |= PROT_WRITE;
    mprotect(moved_to, new_size, d_prot);
  } else {
    /* Anonymous page. Copy contents to new area */
    mprotect(moved_to, new_size, PROT_READ | PROT_WRITE);
    memcpy(moved_to, region->haddr, old_size);
  }

  /* Unmap the old page */
  if (old_size < region->size) {
    split_region(proc.mm, region, region->gaddr + old_size);
  }
  list_del(&region->list);
  RB_REMOVE(mm_region_tree, &proc.mm->mm_region_tree, region);
  munmap(region->haddr, region->size);
  vmm_munmap(region->gaddr, region->size);

  /* Map new one */
  ret = alloc_region(new_size);
  struct mm_region *new = record_region(proc.mm, moved_to, ret, new_size, region->prot, region->mm_flags, region->mm_fd, region->pgoff);
  vmm_mmap(new->gaddr, new->size, new->prot, new->haddr);

  free(region);

out:
  pthread_rwlock_unlock(&proc.mm->alloc_lock);

  return ret;
}

DEFINE_SYSCALL(mprotect, gaddr_t, addr, size_t, len, int, prot)
{
  if (!is_page_aligned((void*)addr, PAGE_4KB) || len == 0) {
    return -LINUX_EINVAL;
  }
  // TODO check if user is permiited to access the addr

  len = roundup(len, PAGE_SIZEOF(PAGE_4KB));
  gaddr_t end = addr + len;

  hv_memory_flags_t hvprot = 0;
  if (prot & LINUX_PROT_READ) hvprot |= HV_MEMORY_READ;
  if (prot & LINUX_PROT_WRITE) hvprot |= HV_MEMORY_WRITE;
  if (prot & LINUX_PROT_EXEC) hvprot |= HV_MEMORY_EXEC;

  int ret = 0;

  pthread_rwlock_wrlock(&proc.mm->alloc_lock);

  struct mm_region *region = find_region(addr, proc.mm);
  if (!region) {
    ret = -LINUX_ENOMEM;
    goto out;
  }
  if (addr > region->gaddr) {
    split_region(proc.mm, region, addr);
    region = list_entry(region->list.next, struct mm_region, list);

    hv_vm_protect(region->gaddr, region->size, hvprot);
    mprotect(region->haddr, region->size, prot);
    region->prot = hvprot;
  }
  while (region->gaddr + region->size <= end) {
    hv_vm_protect(region->gaddr, region->size, hvprot);
    mprotect(region->haddr, region->size, prot);
    region->prot = hvprot;

    if (region->list.next == &proc.mm->mm_regions) {
      ret = -LINUX_ENOMEM;
      goto out;
    }
    struct mm_region *next = list_entry(region->list.next, struct mm_region, list);
    if (next->gaddr != region->gaddr + region->size) {
      ret = -LINUX_ENOMEM;
      goto out;
    }
    region = next;
  }
  if (region->gaddr < end) {
    split_region(proc.mm, region, end);
    hv_vm_protect(region->gaddr, region->size, hvprot);
    mprotect(region->haddr, region->size, prot);
    region->prot = hvprot;
  }

out:
  pthread_rwlock_unlock(&proc.mm->alloc_lock);

  return ret;
}

DEFINE_SYSCALL(munmap, gaddr_t, gaddr, size_t, size)
{
  uint64_t ret;
  pthread_rwlock_wrlock(&proc.mm->alloc_lock);
  ret = do_munmap(gaddr, size);
  pthread_rwlock_unlock(&proc.mm->alloc_lock);
  return ret;
}

int
hv_mflag_to_linux_mprot(hv_memory_flags_t mflag)
{
  int l_prot = 0;
  if (mflag & HV_MEMORY_READ) l_prot |= LINUX_PROT_READ;
  if (mflag & HV_MEMORY_WRITE) l_prot |= LINUX_PROT_WRITE;
  if (mflag & HV_MEMORY_EXEC) l_prot |= LINUX_PROT_EXEC;
  return l_prot;
}

hv_memory_flags_t
linux_mprot_to_hv_mflag(int mprot)
{
  hv_memory_flags_t mflag = 0;
  if (mprot & LINUX_PROT_READ) mflag |= HV_MEMORY_READ;
  if (mprot & LINUX_PROT_WRITE) mflag |= HV_MEMORY_WRITE;
  if (mprot & LINUX_PROT_EXEC) mflag |= HV_MEMORY_EXEC;
  return mflag;
}
