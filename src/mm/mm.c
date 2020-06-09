#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <strings.h>

#include "common.h"
#include "util/list.h"
#include "mm.h"
#include "vmm.h"
#include "noah.h"

#include "linux/mman.h"

#include "x86/vm.h"
#include "x86/specialreg.h"

/* 
 * Manage kernel memory space allocated by kmap.
 * Some members related to user memory space such as start_brk are meaningless in vkern_mm.
 */
struct mm vkern_mm;

void init_mmap(struct mm *mm);

const gaddr_t user_addr_max = 0x0000007fc0000000ULL;

gaddr_t
kmap(void *ptr, size_t size, hv_memory_flags_t flags)
{
  static uint64_t noah_kern_brk = user_addr_max;

  assert((size & 0xfff) == 0);
  assert(((uint64_t) ptr & 0xfff) == 0);

  pthread_rwlock_wrlock(&vkern_mm.alloc_lock);

  record_region(&vkern_mm, ptr, noah_kern_brk, size, hv_mflag_to_linux_mprot(flags), -1, -1, 0);
  vmm_mmap(noah_kern_brk, size, flags, ptr);
  noah_kern_brk += size;

  pthread_rwlock_unlock(&vkern_mm.alloc_lock);

  return noah_kern_brk - size;
}

uint64_t pml4[NR_PAGE_ENTRY] __page_aligned = {
  [0] = PTE_U | PTE_W | PTE_P,
};
gaddr_t pml4_ptr;

uint64_t pdp[NR_PAGE_ENTRY] __page_aligned = {
  /* straight mapping */
#include "pdp.h"
};

void
init_page()
{
  pml4_ptr = kmap(pml4, 0x1000, HV_MEMORY_READ | HV_MEMORY_WRITE);
  pml4[0] |= kmap(pdp, 0x1000, HV_MEMORY_READ | HV_MEMORY_WRITE) & 0x000ffffffffff000ul;

  vmm_write_vmcs(VMCS_GUEST_CR0, CR0_PG | CR0_PE | CR0_NE);
  vmm_write_vmcs(VMCS_GUEST_CR3, pml4_ptr);
}

uint64_t gdt[3] __page_aligned = {
  [SEG_NULL] = 0,                  // NULL SEL
  [SEG_CODE] = 0x0020980000000000, // CODE SEL
  [SEG_DATA] = 0x0000900000000000, // DATA SEL
};
gaddr_t gdt_ptr;

void
init_segment()
{
  kmap(gdt, 0x1000, HV_MEMORY_READ | HV_MEMORY_WRITE);

  vmm_write_vmcs(VMCS_GUEST_GDTR_BASE, gdt_ptr);
  vmm_write_vmcs(VMCS_GUEST_GDTR_LIMIT, 3 * 8 - 1);

  vmm_write_vmcs(VMCS_GUEST_TR_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_TR_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_TR_AR, 0x0000008b);

  vmm_write_vmcs(VMCS_GUEST_LDTR_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_LDTR_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_LDTR_AR, DESC_UNUSABLE);

  vmm_write_vmcs(VMCS_GUEST_IDTR_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_IDTR_LIMIT, 0xffff);

  uint32_t codeseg_ar = 0x0000209B;
  uint32_t dataseg_ar = 0x00000093;
  vmm_write_vmcs(VMCS_GUEST_CS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_CS_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_CS_AR, codeseg_ar);

  vmm_write_vmcs(VMCS_GUEST_DS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_DS_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_DS_AR, dataseg_ar);

  vmm_write_vmcs(VMCS_GUEST_ES, 0);
  vmm_write_vmcs(VMCS_GUEST_ES_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_ES_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_ES_AR, dataseg_ar);

  vmm_write_vmcs(VMCS_GUEST_FS, 0);
  vmm_write_vmcs(VMCS_GUEST_FS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_FS_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_FS_AR, dataseg_ar);

  vmm_write_vmcs(VMCS_GUEST_GS, 0);
  vmm_write_vmcs(VMCS_GUEST_GS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_GS_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_GS_AR, dataseg_ar);

  vmm_write_vmcs(VMCS_GUEST_SS, 0);
  vmm_write_vmcs(VMCS_GUEST_SS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_SS_LIMIT, 0);
  vmm_write_vmcs(VMCS_GUEST_SS_AR, dataseg_ar);

  vmm_write_register(HV_X86_CS, GSEL(SEG_CODE, 0));
  vmm_write_register(HV_X86_DS, GSEL(SEG_DATA, 0));
  vmm_write_register(HV_X86_ES, GSEL(SEG_DATA, 0));
  vmm_write_register(HV_X86_FS, GSEL(SEG_DATA, 0));
  vmm_write_register(HV_X86_GS, GSEL(SEG_DATA, 0));
  vmm_write_register(HV_X86_SS, GSEL(SEG_DATA, 0));
  vmm_write_register(HV_X86_TR, 0);
  vmm_write_register(HV_X86_LDTR, 0);
}

void
init_mm(struct mm *mm)
{
  bzero(mm, sizeof(struct mm));
  init_mmap(mm);

  INIT_LIST_HEAD(&mm->mm_regions);
  RB_INIT(&mm->mm_region_tree);
  pthread_rwlock_init(&mm->alloc_lock, NULL);
}

void *
guest_to_host(gaddr_t gaddr)
{
  struct mm_region *region = find_region(gaddr, proc.mm);
  if (!region) {
    region = find_region(gaddr, &vkern_mm);
  }
  if (!region) {
    return NULL;
  }
  return region->haddr + gaddr - region->gaddr;
}


int
region_compare(struct mm_region *r1, struct mm_region *r2)
{
  if (r1->gaddr >= r2->gaddr + r2->size) {
    return 1;
  }
  if (r1->gaddr + r1->size <= r2->gaddr) {
    return -1;
  }
  
  return 0;
}

RB_GENERATE(mm_region_tree, mm_region, tree, region_compare);

struct mm_region*
/* Look up the mm_region which gaddr in [mm_region->gaddr, +size) */
find_region(gaddr_t gaddr, struct mm *mm)
{
  struct mm_region find = {.gaddr = gaddr, .size = 1};
  return RB_FIND(mm_region_tree, &mm->mm_region_tree, &find);
}

struct mm_region*
/* Look up the lowest mm_region that overlaps with the region */
find_region_range(gaddr_t gaddr, size_t size, struct mm *mm)
{
  struct mm_region find = {.gaddr = gaddr, .size = size};
  struct mm_region *leftmost = RB_FIND(mm_region_tree, &mm->mm_region_tree, &find);
  if (leftmost == NULL)
    return NULL;
  while (RB_LEFT(leftmost, tree) != NULL && region_compare(&find, RB_LEFT(leftmost, tree)) == 0)
    leftmost = RB_LEFT(leftmost, tree);
  return leftmost;
}

void
split_region(struct mm *mm, struct mm_region *region, gaddr_t gaddr)
{
  assert(is_page_aligned((void*)gaddr, PAGE_4KB));

  struct mm_region *tail = malloc(sizeof(struct mm_region));
  int offset = gaddr - region->gaddr;
  tail->haddr = region->haddr + offset;
  tail->gaddr = gaddr;
  tail->size = region->size - offset;
  tail->prot = region->prot;
  tail->mm_flags = region->mm_flags;
  tail->mm_fd = region->mm_fd;
  tail->pgoff = region->pgoff;

  region->size = offset;
  list_add(&tail->list, &region->list);
  RB_INSERT(mm_region_tree, &mm->mm_region_tree, tail);
}

struct mm_region*
record_region(struct mm *mm, void *haddr, gaddr_t gaddr, size_t size, int prot, int mm_flags, int mm_fd, int pgoff)
{
  assert(gaddr != 0);

  struct mm_region *region = malloc(sizeof *region);
  *region = (struct mm_region) {
    .haddr = haddr,
    .gaddr = gaddr,
    .size = size,
    .prot = prot,
    .mm_flags = mm_flags,
    .mm_fd = mm_fd,
    .pgoff = pgoff
  };

  if (RB_INSERT(mm_region_tree, &mm->mm_region_tree, region) != NULL) {
    panic("recording overlapping regions\n");
  }
  struct mm_region *prev = RB_PREV(mm_region_tree, &mm->mm_region_tree, region);
  if (prev == NULL) {
    list_add(&region->list, &mm->mm_regions);
  } else {
    list_add(&region->list, &prev->list);
  }

  return region;
}

bool
is_region_private(struct mm_region *region)
{
  return !(region->mm_flags & LINUX_MAP_SHARED) && region->mm_fd == -1;
}

void
destroy_mm(struct mm *mm)
{
  struct list_head *list, *t;
  list_for_each_safe (list, t, &mm->mm_regions) {
    struct mm_region *r = list_entry(list, struct mm_region, list);
    munmap(r->haddr, r->size);
    vmm_munmap(r->gaddr, r->size);
    free(r);
  }
  RB_INIT(&mm->mm_region_tree);
  INIT_LIST_HEAD(&mm->mm_regions);
}

DEFINE_SYSCALL(madvise, gaddr_t, addr, size_t, length, int, advice)
{
  printk("madvise is not implemented\n");
  return 0;

}

DEFINE_SYSCALL(mlock, gaddr_t, addr, size_t, length)
{
  printk("mlock is not implemented\n");
  return 0;
}

DEFINE_SYSCALL(munlock, gaddr_t, addr, size_t, length)
{
  printk("munlock is not implemented\n");
  return 0;
}

DEFINE_SYSCALL(brk, unsigned long, brk)
{
  uint64_t ret;
  brk = roundup(brk, PAGE_SIZEOF(PAGE_4KB));

  pthread_rwlock_wrlock(&proc.mm->alloc_lock);
  if (brk < proc.mm->start_brk) {
    ret = proc.mm->start_brk;
    goto out;
  }

  if (brk < proc.mm->current_brk) {
    do_munmap(brk, proc.mm->current_brk - brk);
    ret = proc.mm->current_brk = brk;
  } else {
    do_mmap(proc.mm->current_brk, brk - proc.mm->current_brk, PROT_READ | PROT_WRITE, LINUX_PROT_READ | LINUX_PROT_WRITE, LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS, -1, 0);
    ret = proc.mm->current_brk = brk;
  }

out:
  pthread_rwlock_unlock(&proc.mm->alloc_lock);

  return ret;
}

DEFINE_SYSCALL(get_mempolicy, gaddr_t, policy, gaddr_t, nmask, unsigned long, maxnode, unsigned long, addr, unsigned long, flags)
{
  maxnode = roundup(maxnode, sizeof(unsigned long));
  if (flags != 0) {
    printk("get_mempolicy: unsupported flags: 0x%lx\n", flags);
    return -LINUX_ENOSYS;
  }
  assert(addr == 0);
  int policy_val = LINUX_MPOL_DEFAULT;
  if (copy_to_user(policy, &policy_val, sizeof policy_val))
    return -LINUX_EFAULT;
  size_t size = maxnode / 64;
  unsigned long mask[size];
  memset(mask, 0, sizeof mask);
  if (copy_to_user(nmask, mask, sizeof mask))
    return -LINUX_EFAULT;
  return 0;
}

DEFINE_SYSCALL(msync, gaddr_t, addr, size_t, len, int, flags)
{
  struct mm_region *region = find_region(addr, proc.mm);
  if (!region || addr - region->gaddr >= len || len + addr - region->gaddr > region->size) {
    return -LINUX_ENOMEM;
  }
  
  if (flags & ~(LINUX_MS_ASYNC | LINUX_MS_SYNC | LINUX_MS_INVALIDATE)) {
    return -LINUX_EINVAL;
  }
  int dflags = 0;
  if (flags & LINUX_MS_ASYNC) dflags |= MS_ASYNC;
  if (flags & LINUX_MS_SYNC) dflags |= MS_SYNC;
  if (flags & LINUX_MS_INVALIDATE) dflags |= MS_INVALIDATE;
  
  return syswrap(msync(addr - region->gaddr + region->haddr, len, dflags));
}
