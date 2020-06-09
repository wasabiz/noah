#ifndef NOAH_VMM_H
#define NOAH_VMM_H

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <Hypervisor/hv_arch_vmx.h>

#include "types.h"
#include "noah.h"
#include "x86/vmx.h"

struct vcpu_snapshot {
  uint64_t vcpu_reg[NR_X86_REG_LIST];
  uint64_t vmcs[NR_VMCS_FIELD_MASKED];
  char fpu_states[2496] __attribute__((aligned(16)));
};

struct vmm_snapshot {
  struct vcpu_snapshot first_vcpu_snapshot;
};

void vmm_create(void);
void vmm_destroy(void);
void vmm_snapshot(struct vmm_snapshot*);
void vmm_reentry(struct vmm_snapshot*);
void vmm_snapshot_vcpu(struct vcpu_snapshot*);
void vmm_restore_vcpu(struct vcpu_snapshot*);

void vmm_create_vcpu(struct vcpu_snapshot *);
void vmm_destroy_vcpu(void);

int vmm_run(void);

void vmm_read_register(hv_x86_reg_t, uint64_t *);
void vmm_write_register(hv_x86_reg_t, uint64_t);
void vmm_read_msr(uint32_t, uint64_t *);
void vmm_write_msr(uint32_t, uint64_t);
void vmm_read_vmcs(uint32_t, uint64_t *);
void vmm_write_vmcs(uint32_t, uint64_t);

void vmm_write_fpstate(void *, size_t);

void vmm_enable_native_msr(uint32_t, bool);

/* prot is obtained by or'ing HV_MEMORY_READ, HV_MEMORY_EXEC, HV_MEMORY_WRITE */
void vmm_mmap(gaddr_t addr, size_t len, int prot, void *ptr);
void vmm_munmap(gaddr_t addr, size_t len);

#endif
