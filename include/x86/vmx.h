#ifndef NOAH_X86_VMX_H
#define NOAH_X86_VMX_H

#define elementsof(array) ( sizeof(array) / sizeof((array)[0]) )
#define NR_VMCS_FIELD (elementsof(vmcs_field_list) - 1)
#define NR_VMCS_FIELD_MASKED (elementsof(vmcs_field_masked_list) - 1)

#define VMCS_FIELD_ENTRIES                            \
  MASK( VMCS_FIELD(VMCS_VPID) )                       \
  VMCS_FIELD(VMCS_CTRL_POSTED_INT_N_VECTOR)           \
  VMCS_FIELD(VMCS_CTRL_EPTP_INDEX)                    \
  VMCS_FIELD(VMCS_GUEST_ES)                           \
  VMCS_FIELD(VMCS_GUEST_CS)                           \
  VMCS_FIELD(VMCS_GUEST_SS)                           \
  VMCS_FIELD(VMCS_GUEST_DS)                           \
  VMCS_FIELD(VMCS_GUEST_FS)                           \
  VMCS_FIELD(VMCS_GUEST_GS)                           \
  VMCS_FIELD(VMCS_GUEST_LDTR)                         \
  VMCS_FIELD(VMCS_GUEST_TR)                           \
  VMCS_FIELD(VMCS_GUEST_INT_STATUS)                   \
  MASK( VMCS_FIELD(VMCS_HOST_ES) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_CS) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_SS) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_DS) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_FS) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_GS) )                    \
  MASK( VMCS_FIELD(VMCS_HOST_TR) )                    \
  VMCS_FIELD(VMCS_CTRL_IO_BITMAP_A)                   \
  VMCS_FIELD(VMCS_CTRL_IO_BITMAP_B)                   \
  VMCS_FIELD(VMCS_CTRL_MSR_BITMAPS)                   \
  VMCS_FIELD(VMCS_CTRL_VMEXIT_MSR_STORE_ADDR)         \
  VMCS_FIELD(VMCS_CTRL_VMEXIT_MSR_LOAD_ADDR)          \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_MSR_LOAD_ADDR)         \
  VMCS_FIELD(VMCS_CTRL_EXECUTIVE_VMCS_PTR)            \
  VMCS_FIELD(VMCS_CTRL_TSC_OFFSET)                    \
  VMCS_FIELD(VMCS_CTRL_VIRTUAL_APIC)                  \
  VMCS_FIELD(VMCS_CTRL_APIC_ACCESS)                   \
  VMCS_FIELD(VMCS_CTRL_POSTED_INT_DESC_ADDR)          \
  VMCS_FIELD(VMCS_CTRL_VMFUNC_CTRL)                   \
  VMCS_FIELD(VMCS_CTRL_EPTP)                          \
  VMCS_FIELD(VMCS_CTRL_EOI_EXIT_BITMAP_0)             \
  VMCS_FIELD(VMCS_CTRL_EOI_EXIT_BITMAP_1)             \
  VMCS_FIELD(VMCS_CTRL_EOI_EXIT_BITMAP_2)             \
  VMCS_FIELD(VMCS_CTRL_EOI_EXIT_BITMAP_3)             \
  VMCS_FIELD(VMCS_CTRL_EPTP_LIST_ADDR)                \
  VMCS_FIELD(VMCS_CTRL_VMREAD_BITMAP_ADDR)            \
  VMCS_FIELD(VMCS_CTRL_VMWRITE_BITMAP_ADDR)           \
  VMCS_FIELD(VMCS_CTRL_VIRT_EXC_INFO_ADDR)            \
  VMCS_FIELD(VMCS_CTRL_XSS_EXITING_BITMAP)            \
  MASK( VMCS_FIELD(VMCS_GUEST_PHYSICAL_ADDRESS) )     \
  VMCS_FIELD(VMCS_GUEST_LINK_POINTER)                 \
  VMCS_FIELD(VMCS_GUEST_IA32_DEBUGCTL)                \
  VMCS_FIELD(VMCS_GUEST_IA32_PAT)                     \
  VMCS_FIELD(VMCS_GUEST_IA32_EFER)                    \
  VMCS_FIELD(VMCS_GUEST_IA32_PERF_GLOBAL_CTRL)        \
  VMCS_FIELD(VMCS_GUEST_PDPTE0)                       \
  VMCS_FIELD(VMCS_GUEST_PDPTE1)                       \
  VMCS_FIELD(VMCS_GUEST_PDPTE2)                       \
  VMCS_FIELD(VMCS_GUEST_PDPTE3)                       \
  MASK( VMCS_FIELD(VMCS_HOST_IA32_PAT) )              \
  MASK( VMCS_FIELD(VMCS_HOST_IA32_EFER) )             \
  MASK( VMCS_FIELD(VMCS_HOST_IA32_PERF_GLOBAL_CTRL) ) \
  VMCS_FIELD(VMCS_CTRL_PIN_BASED)                     \
  VMCS_FIELD(VMCS_CTRL_CPU_BASED)                     \
  VMCS_FIELD(VMCS_CTRL_EXC_BITMAP)                    \
  VMCS_FIELD(VMCS_CTRL_PF_ERROR_MASK)                 \
  VMCS_FIELD(VMCS_CTRL_PF_ERROR_MATCH)                \
  VMCS_FIELD(VMCS_CTRL_CR3_COUNT)                     \
  VMCS_FIELD(VMCS_CTRL_VMEXIT_CONTROLS)               \
  VMCS_FIELD(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT)        \
  VMCS_FIELD(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT)         \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_CONTROLS)              \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT)        \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_IRQ_INFO)              \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_EXC_ERROR)             \
  VMCS_FIELD(VMCS_CTRL_VMENTRY_INSTR_LEN)             \
  VMCS_FIELD(VMCS_CTRL_TPR_THRESHOLD)                 \
  VMCS_FIELD(VMCS_CTRL_CPU_BASED2)                    \
  VMCS_FIELD(VMCS_CTRL_PLE_GAP)                       \
  VMCS_FIELD(VMCS_CTRL_PLE_WINDOW)                    \
  MASK( VMCS_FIELD(VMCS_RO_INSTR_ERROR) )             \
  MASK( VMCS_FIELD(VMCS_RO_EXIT_REASON) )             \
  MASK( VMCS_FIELD(VMCS_RO_VMEXIT_IRQ_INFO) )         \
  MASK( VMCS_FIELD(VMCS_RO_VMEXIT_IRQ_ERROR) )        \
  MASK( VMCS_FIELD(VMCS_RO_IDT_VECTOR_INFO) )         \
  MASK( VMCS_FIELD(VMCS_RO_IDT_VECTOR_ERROR) )        \
  MASK( VMCS_FIELD(VMCS_RO_VMEXIT_INSTR_LEN) )        \
  MASK( VMCS_FIELD(VMCS_RO_VMX_INSTR_INFO) )          \
  VMCS_FIELD(VMCS_GUEST_ES_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_CS_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_SS_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_DS_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_FS_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_GS_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_LDTR_LIMIT)                   \
  VMCS_FIELD(VMCS_GUEST_TR_LIMIT)                     \
  VMCS_FIELD(VMCS_GUEST_GDTR_LIMIT)                   \
  VMCS_FIELD(VMCS_GUEST_IDTR_LIMIT)                   \
  VMCS_FIELD(VMCS_GUEST_ES_AR)                        \
  VMCS_FIELD(VMCS_GUEST_CS_AR)                        \
  VMCS_FIELD(VMCS_GUEST_SS_AR)                        \
  VMCS_FIELD(VMCS_GUEST_DS_AR)                        \
  VMCS_FIELD(VMCS_GUEST_FS_AR)                        \
  VMCS_FIELD(VMCS_GUEST_GS_AR)                        \
  VMCS_FIELD(VMCS_GUEST_LDTR_AR)                      \
  VMCS_FIELD(VMCS_GUEST_TR_AR)                        \
  VMCS_FIELD(VMCS_GUEST_IGNORE_IRQ)                   \
  VMCS_FIELD(VMCS_GUEST_ACTIVITY_STATE)               \
  VMCS_FIELD(VMCS_GUEST_SMBASE)                       \
  VMCS_FIELD(VMCS_GUEST_IA32_SYSENTER_CS)             \
  VMCS_FIELD(VMCS_GUEST_VMX_TIMER_VALUE)              \
  VMCS_FIELD(VMCS_HOST_IA32_SYSENTER_CS)              \
  VMCS_FIELD(VMCS_CTRL_CR0_MASK)                      \
  VMCS_FIELD(VMCS_CTRL_CR4_MASK)                      \
  VMCS_FIELD(VMCS_CTRL_CR0_SHADOW)                    \
  VMCS_FIELD(VMCS_CTRL_CR4_SHADOW)                    \
  VMCS_FIELD(VMCS_CTRL_CR3_VALUE0)                    \
  VMCS_FIELD(VMCS_CTRL_CR3_VALUE1)                    \
  VMCS_FIELD(VMCS_CTRL_CR3_VALUE2)                    \
  VMCS_FIELD(VMCS_CTRL_CR3_VALUE3)                    \
  MASK( VMCS_FIELD(VMCS_RO_EXIT_QUALIFIC) )           \
  MASK( VMCS_FIELD(VMCS_RO_IO_RCX) )                  \
  MASK( VMCS_FIELD(VMCS_RO_IO_RSI) )                  \
  MASK( VMCS_FIELD(VMCS_RO_IO_RDI) )                  \
  MASK( VMCS_FIELD(VMCS_RO_IO_RIP) )                  \
  MASK( VMCS_FIELD(VMCS_RO_GUEST_LIN_ADDR) )          \
  VMCS_FIELD(VMCS_GUEST_CR0)                          \
  VMCS_FIELD(VMCS_GUEST_CR3)                          \
  VMCS_FIELD(VMCS_GUEST_CR4)                          \
  VMCS_FIELD(VMCS_GUEST_ES_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_CS_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_SS_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_DS_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_FS_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_GS_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_LDTR_BASE)                    \
  VMCS_FIELD(VMCS_GUEST_TR_BASE)                      \
  VMCS_FIELD(VMCS_GUEST_GDTR_BASE)                    \
  VMCS_FIELD(VMCS_GUEST_IDTR_BASE)                    \
  VMCS_FIELD(VMCS_GUEST_DR7)                          \
  VMCS_FIELD(VMCS_GUEST_RSP)                          \
  VMCS_FIELD(VMCS_GUEST_RIP)                          \
  VMCS_FIELD(VMCS_GUEST_RFLAGS)                       \
  VMCS_FIELD(VMCS_GUEST_DEBUG_EXC)                    \
  VMCS_FIELD(VMCS_GUEST_SYSENTER_ESP)                 \
  VMCS_FIELD(VMCS_GUEST_SYSENTER_EIP)                 \
  MASK( VMCS_FIELD(VMCS_HOST_CR0) )                   \
  MASK( VMCS_FIELD(VMCS_HOST_CR3) )                   \
  MASK( VMCS_FIELD(VMCS_HOST_CR4) )                   \
  MASK( VMCS_FIELD(VMCS_HOST_FS_BASE) )               \
  MASK( VMCS_FIELD(VMCS_HOST_GS_BASE) )               \
  MASK( VMCS_FIELD(VMCS_HOST_TR_BASE) )               \
  MASK( VMCS_FIELD(VMCS_HOST_GDTR_BASE) )             \
  MASK( VMCS_FIELD(VMCS_HOST_IDTR_BASE) )             \
  MASK( VMCS_FIELD(VMCS_HOST_IA32_SYSENTER_ESP) )     \
  MASK( VMCS_FIELD(VMCS_HOST_IA32_SYSENTER_EIP) )     \
  MASK( VMCS_FIELD(VMCS_HOST_RSP) )                   \
  MASK( VMCS_FIELD(VMCS_HOST_RIP) )                   \
  VMCS_FIELD(VMCS_MAX)

static const uint32_t vmcs_field_list[] = {
#define MASK(x) x
#define VMCS_FIELD(x) x,
  VMCS_FIELD_ENTRIES
#undef VMCS_FIELD
#undef MASK
};

static const char *vmcs_field_str[] = {
#define MASK(x) x
#define VMCS_FIELD(x) #x,
  VMCS_FIELD_ENTRIES
#undef VMCS_FIELD
#undef MASK
};

static const uint32_t vmcs_field_masked_list[] = {
#define MASK(x)
#define VMCS_FIELD(x) x,
  VMCS_FIELD_ENTRIES
#undef VMCS_FIELD
#undef MASK
};

#define NR_X86_REG_LIST (sizeof(x86_reg_list) / sizeof(uint32_t) - 1)

#define X86_REG_ENTRIES                             \
  X86_REG(HV_X86_RIP)                               \
  X86_REG(HV_X86_RFLAGS)                            \
  X86_REG(HV_X86_RAX)                               \
  X86_REG(HV_X86_RCX)                               \
  X86_REG(HV_X86_RDX)                               \
  X86_REG(HV_X86_RBX)                               \
  X86_REG(HV_X86_RSI)                               \
  X86_REG(HV_X86_RDI)                               \
  X86_REG(HV_X86_RSP)                               \
  X86_REG(HV_X86_RBP)                               \
  X86_REG(HV_X86_R8)                                \
  X86_REG(HV_X86_R9)                                \
  X86_REG(HV_X86_R10)                               \
  X86_REG(HV_X86_R11)                               \
  X86_REG(HV_X86_R12)                               \
  X86_REG(HV_X86_R13)                               \
  X86_REG(HV_X86_R14)                               \
  X86_REG(HV_X86_R15)                               \
  X86_REG(HV_X86_CS)                                \
  X86_REG(HV_X86_SS)                                \
  X86_REG(HV_X86_DS)                                \
  X86_REG(HV_X86_ES)                                \
  X86_REG(HV_X86_FS)                                \
  X86_REG(HV_X86_GS)                                \
  X86_REG(HV_X86_IDT_BASE)                          \
  X86_REG(HV_X86_IDT_LIMIT)                         \
  X86_REG(HV_X86_GDT_BASE)                          \
  X86_REG(HV_X86_GDT_LIMIT)                         \
  X86_REG(HV_X86_LDTR)                              \
  X86_REG(HV_X86_LDT_BASE)                          \
  X86_REG(HV_X86_LDT_LIMIT)                         \
  X86_REG(HV_X86_LDT_AR)                            \
  X86_REG(HV_X86_TR)                                \
  X86_REG(HV_X86_TSS_BASE)                          \
  X86_REG(HV_X86_TSS_LIMIT)                         \
  X86_REG(HV_X86_TSS_AR)                            \
  X86_REG(HV_X86_CR0)                               \
  X86_REG(HV_X86_CR1)                               \
  X86_REG(HV_X86_CR2)                               \
  X86_REG(HV_X86_CR3)                               \
  X86_REG(HV_X86_CR4)                               \
  X86_REG(HV_X86_DR0)                               \
  X86_REG(HV_X86_DR1)                               \
  X86_REG(HV_X86_DR2)                               \
  X86_REG(HV_X86_DR3)                               \
  X86_REG(HV_X86_DR4)                               \
  X86_REG(HV_X86_DR5)                               \
  X86_REG(HV_X86_DR6)                               \
  X86_REG(HV_X86_DR7)                               \
  X86_REG(HV_X86_TPR)                               \
  X86_REG(HV_X86_XCR0)                              \
  X86_REG(HV_X86_REGISTERS_MAX)

static const uint32_t x86_reg_list[] = {
#define X86_REG(x) x,
  X86_REG_ENTRIES
#undef X86_REG
};

static const char *x86_reg_str[] = {
#define X86_REG(x) #x,
  X86_REG_ENTRIES
#undef X86_REG
};

#define VMCS_EXCTYPE_EXTERNAL_INTERRUPT 0
#define VMCS_EXCTYPE_NONMASKTABLE_INTERRUPT 2
#define VMCS_EXCTYPE_HARDWARE_EXCEPTION 3
#define VMCS_EXCTYPE_SOFTWARE_EXCEPTION 6


#endif
