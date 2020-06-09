#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <libgen.h>
#include <sys/syslimits.h>

#include "vmm.h"
#include "mm.h"
#include "util/list.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <Hypervisor/hv_arch_vmx.h>

#include "x86/vm.h"
#include "x86/vmx.h"

struct vcpu {
  struct list_head list;
  hv_vcpuid_t vcpuid;
};

struct list_head vcpus;
int nr_vcpus;
pthread_rwlock_t alloc_lock;

_Thread_local static struct vcpu *vcpu;

void
vmm_mmap(gaddr_t gaddr, size_t size, int prot, void *haddr)
{
  assert(is_page_aligned(haddr, PAGE_4KB));
  assert(is_page_aligned((void *) gaddr, PAGE_4KB));
  assert(is_page_aligned((void *) size, PAGE_4KB));

  hv_vm_unmap(gaddr, size);
  if (hv_vm_map(haddr, gaddr, size, prot) != HV_SUCCESS) {
    panic("hv_vm_map failed\n");
  }
}

void
vmm_munmap(gaddr_t gaddr, size_t size)
{
  assert(is_page_aligned((void *) size, PAGE_4KB));
  hv_vm_unmap(gaddr, size);
}

void
vmm_write_fpstate(void *buffer, size_t size)
{
  if (hv_vcpu_write_fpstate(vcpu->vcpuid, buffer, size) != HV_SUCCESS) {
    abort();
  }
}

void
vmm_enable_native_msr(uint32_t msr, bool enable)
{
  if (hv_vcpu_enable_native_msr(vcpu->vcpuid, msr, enable) != HV_SUCCESS) {
    abort();
  }
}

void
vmm_create()
{
  hv_return_t ret;

  /* initialize global variables */
  pthread_rwlock_init(&alloc_lock, NULL);
  INIT_LIST_HEAD(&vcpus);
  nr_vcpus = 0;

  /* create the VM */
  ret = hv_vm_create(HV_VM_DEFAULT);
  if (ret != HV_SUCCESS) {
    panic("could not create the vm: error code %x", ret);
    return;
  }

  printk("successfully created the vm\n");

  vmm_create_vcpu(NULL);

  printk("successfully created a vcpu\n");
}

void
vmm_destroy()
{
  hv_return_t ret;

  struct vcpu *vcpu;
  list_for_each_entry (vcpu, &vcpus, list) {
    ret = hv_vcpu_destroy(vcpu->vcpuid);
    if (ret != HV_SUCCESS) {
      panic("could not destroy the vcpu: error code %x", ret);
      exit(1);
    }
  }

  printk("successfully destroyed the vcpu\n");

  ret = hv_vm_destroy();
  if (ret != HV_SUCCESS) {
    panic("could not destroy the vm: error code %x", ret);
    exit(1);
  }

  printk("successfully destroyed the vm\n");
}

void
vmm_create_vcpu(struct vcpu_snapshot *snapshot)
{
  hv_return_t ret;
  hv_vcpuid_t vcpuid;

  ret = hv_vcpu_create(&vcpuid, HV_VCPU_DEFAULT);
  if (ret != HV_SUCCESS) {
    panic("could not create a vcpu: error code %x", ret);
    return;
  }

  assert(vcpu == NULL);

  vcpu = calloc(sizeof(struct vcpu), 1);
  vcpu->vcpuid = vcpuid;

  if (snapshot) {
    vmm_restore_vcpu(snapshot);
  }

  pthread_rwlock_wrlock(&alloc_lock);
  list_add(&vcpu->list, &vcpus);
  nr_vcpus++;
  pthread_rwlock_unlock(&alloc_lock);
}

void
vmm_destroy_vcpu(void)
{
  pthread_rwlock_wrlock(&alloc_lock);
  list_del(&vcpu->list);
  nr_vcpus--;
  hv_vcpu_destroy(vcpu->vcpuid);
  free(vcpu);
  vcpu = NULL;
  pthread_rwlock_unlock(&alloc_lock);
}

void
print_regs()
{
  uint64_t value;

  vmm_read_register(HV_X86_RIP, &value);
  printk("\trip = 0x%llx\n", value);
  vmm_read_register(HV_X86_RAX, &value);
  printk("\trax = 0x%llx\n", value);
  vmm_read_register(HV_X86_RBX, &value);
  printk("\trbx = 0x%llx\n", value);
  vmm_read_register(HV_X86_RCX, &value);
  printk("\trcx = 0x%llx\n", value);
  vmm_read_register(HV_X86_RDX, &value);
  printk("\trdx = 0x%llx\n", value);
  vmm_read_register(HV_X86_RDI, &value);
  printk("\trdi = 0x%llx\n", value);
  vmm_read_register(HV_X86_RSI, &value);
  printk("\trsi = 0x%llx\n", value);
  vmm_read_register(HV_X86_RBP, &value);
  printk("\trbp = 0x%llx\n", value);
}

void
dump_instr()
{
  uint64_t instlen, rip;
  vmm_read_vmcs(VMCS_RO_VMEXIT_INSTR_LEN, &instlen);
  vmm_read_register(HV_X86_RIP, &rip);
  char inst_str[instlen * 3 + 1];
  for (size_t i = 0; i < instlen; i ++) {
    unsigned char *ip = guest_to_host(rip);
    if (ip) {
      sprintf(inst_str + 3 * i, "%02x ", ip[i]);
    } else {
      printk("rip is in invalid user address: 0x%016llx\n", rip);
      send_signal(getpid(), LINUX_SIGSEGV);
      return;
    }
  }
  inst_str[instlen * 3] = '\0';
  printk("len: %lld, instruction: %s\n", instlen, inst_str);
}

void
vmm_snapshot_vcpu(struct vcpu_snapshot *snapshot)
{
  /* snapshot registers */
  for (uint64_t i = 0; i < NR_X86_REG_LIST; i++) {
    vmm_read_register(x86_reg_list[i], &snapshot->vcpu_reg[i]);
  }
  /* snapshot vmcs */
  for (uint64_t i = 0; i < NR_VMCS_FIELD_MASKED; i++) {
    vmm_read_vmcs(vmcs_field_masked_list[i], &snapshot->vmcs[i]);
  }
  hv_vcpu_read_fpstate(vcpu->vcpuid, snapshot->fpu_states, sizeof snapshot->fpu_states);
}

void
vmm_snapshot(struct vmm_snapshot *snapshot)
{
  printk("vmm_snapshot\n");

  pthread_rwlock_rdlock(&alloc_lock);

  if (nr_vcpus > 1) {
    fprintf(stderr, "multi-threaded fork is not implemented yet.\n");
    exit(1);
  }

  vmm_snapshot_vcpu(&snapshot->first_vcpu_snapshot);

  pthread_rwlock_unlock(&alloc_lock);
}

void init_msr(); // TODO: save and resotre MSR. just call init_msr in main.c now

void
vmm_restore_vcpu(struct vcpu_snapshot *snapshot)
{
  /* restore vmcs */
  for (uint64_t i = 0; i < NR_VMCS_FIELD_MASKED; i++) {
    vmm_write_vmcs(vmcs_field_masked_list[i], snapshot->vmcs[i]);
  }

  /* restore registers */
  for (uint64_t i = 0; i < NR_X86_REG_LIST; i++) {
    vmm_write_register(x86_reg_list[i], snapshot->vcpu_reg[i]);
  }

  /* restore fpu states */
  hv_vcpu_write_fpstate(vcpu->vcpuid, snapshot->fpu_states, sizeof snapshot->fpu_states);

  /* restore MSRs. Initializing them is enough now */
  init_msr();
}

bool
restore_ept()
{
  struct list_head *list;

  list_for_each (list, &vkern_mm.mm_regions) {
    struct mm_region *p = list_entry(list, struct mm_region, list);
    if (hv_vm_map(p->haddr, p->gaddr, p->size, linux_mprot_to_hv_mflag(p->prot)) != HV_SUCCESS)
      return false;
  }
  list_for_each (list, &proc.mm->mm_regions) {
    struct mm_region *p = list_entry(list, struct mm_region, list);
    if (hv_vm_map(p->haddr, p->gaddr, p->size, linux_mprot_to_hv_mflag(p->prot)) != HV_SUCCESS)
      return false;
  }
  return true;
}

void
vmm_reentry(struct vmm_snapshot *snapshot)
{
  hv_return_t ret;

  printk("vmm_restore\n");
  bool retried = false;
retry:
  ret = hv_vm_create(HV_VM_DEFAULT);
  if (ret != HV_SUCCESS) {
    if (!retried && ret == HV_NO_DEVICE) {
      sleep(0);
      retried = true;
      printk("retried\n");
      goto retry;
    }
    panic("could not create the vm: error code %x", ret);
    return;
  }
  printk("successfully created vm\n");

  pthread_rwlock_rdlock(&alloc_lock);

  if (nr_vcpus > 1) {
    fprintf(stderr, "multi-threaded fork is not implemented yet.\n");
    exit(1);
  }

  ret = hv_vcpu_create(&vcpu->vcpuid, HV_VCPU_DEFAULT);
  if (ret != HV_SUCCESS) {
    panic("could not create a vcpu: error code %x", ret);
    return;
  }
  vmm_restore_vcpu(&snapshot->first_vcpu_snapshot);

  pthread_rwlock_unlock(&alloc_lock);
  printk("vcpu_restore done\n");

  restore_ept();
  printk("ept_restore done\n");

}

void
vmm_read_register(hv_x86_reg_t reg, uint64_t *val)
{
  if (hv_vcpu_read_register(vcpu->vcpuid, reg, val) != HV_SUCCESS) {
    fprintf(stderr, "read_register failed\n");
    abort();
  }
}

void
vmm_write_register(hv_x86_reg_t reg, uint64_t val) {
  if (hv_vcpu_write_register(vcpu->vcpuid, reg, val) != HV_SUCCESS) {
    fprintf(stderr, "write_register failed\n");
    abort();
  }
}

void
vmm_read_msr(hv_x86_reg_t reg, uint64_t *val)
{
  if (hv_vcpu_read_msr(vcpu->vcpuid, reg, val) != HV_SUCCESS) {
    fprintf(stderr, "read_msr failed\n");
    abort();
  }
}

void
vmm_write_msr(hv_x86_reg_t reg, uint64_t val) {
  if (hv_vcpu_write_msr(vcpu->vcpuid, reg, val) != HV_SUCCESS) {
    fprintf(stderr, "write_msr failed\n");
    abort();
  }
}

void
vmm_read_vmcs(hv_x86_reg_t field, uint64_t *val)
{
  if (hv_vmx_vcpu_read_vmcs(vcpu->vcpuid, field, val) != HV_SUCCESS) {
    fprintf(stderr, "read_vmcs failed\n");
    abort();
  }
}

void
vmm_write_vmcs(hv_x86_reg_t field, uint64_t val) {
  if (hv_vmx_vcpu_write_vmcs(vcpu->vcpuid, field, val) != HV_SUCCESS) {
    /* FIXME! it fails for the VMCS_CTRL_TSC_OFFSET field on some platforms */
    //fprintf(stderr, "write_vmcs failed: %s\n", vmcs_field_to_str(field));
    //    abort();
  }
}

int
vmm_run()
{
  if (hv_vcpu_run(vcpu->vcpuid) == HV_SUCCESS) {
    return 0;
  }
  return -1;
}
