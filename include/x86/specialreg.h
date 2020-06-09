#define CR0_PE          0x00000001      // Protection Enable
#define CR0_MP          0x00000002      // for FPU/SSE
#define CR0_EM          0x00000004      // fpu emulation
#define CR0_NE          0x00000020      // Numeric Error Enable
#define CR0_PG          0x80000000      // Paging
#define CR4_PSE         0x00000010      // Page size extension
#define CR4_PAE         0x00000020      
#define CR4_OSFXSR      0x00000200
#define CR4_OSXMMEXCPT  0x00000400
#define CR4_OSXSAVE     (1 << 18)
#define CR4_VMXE        0x00002000      

#define EFER_SCE        0x00000001      // syscall Extension
#define EFER_LME        0x00000100      // Lond Mode Enable
#define EFER_LMA        0x00000400      // Lond Mode Active

#define MSR_TIME_STAMP_COUNTER 0x00000010
#define MSR_KERNEL_GS_BASE     0xc0000102
#define MSR_TSC_AUX            0xc0000103

#define XCR0_SSE_STATE  0x00000002
#define XCR0_AVX_STATE  0x00000004
