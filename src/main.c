#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <cpuid.h>
#include <getopt.h>
#include <string.h>
#include <sys/syslimits.h>
#include <libgen.h>
#include <strings.h>
#include <fcntl.h>

#include "common.h"
#include "vmm.h"
#include "mm.h"
#include "noah.h"
#include "syscall.h"
#include "linux/errno.h"
#include "x86/irq_vectors.h"
#include "x86/specialreg.h"
#include "x86/vm.h"
#include "x86/vmx.h"

#include <mach-o/dyld.h>

struct vkern *vkern;

static bool
is_syscall(int instlen, uint64_t rip)
{
  static const ushort OP_SYSCALL = 0x050f;
  if (instlen != 2)
    return false;
  ushort op;
  if (copy_from_user(&op, rip, sizeof op))
    return false;
  return op == OP_SYSCALL;
}

static int
handle_syscall(void)
{
  uint64_t rax;
  vmm_read_register(HV_X86_RAX, &rax);
  if (rax >= NR_SYSCALLS) {
    warnk("unknown system call: %lld\n", rax);
    send_signal(getpid(), LINUX_SIGSYS);
  }
  uint64_t rdi, rsi, rdx, r10, r8, r9;
  vmm_read_register(HV_X86_RDI, &rdi);
  vmm_read_register(HV_X86_RSI, &rsi);
  vmm_read_register(HV_X86_RDX, &rdx);
  vmm_read_register(HV_X86_R10, &r10);
  vmm_read_register(HV_X86_R8, &r8);
  vmm_read_register(HV_X86_R9, &r9);
  uint64_t retval = sc_handler_table[rax](rdi, rsi, rdx, r10, r8, r9);
  vmm_write_register(HV_X86_RAX, retval);

  if (rax == NR_rt_sigreturn) {
    return -1;
  }
  return 0;
}

int
task_run()
{
  /* handle pending signals */
  if (has_sigpending()) {
    handle_signal();
  }
  return vmm_run();
}

void
main_loop(int return_on_sigret)
{
  /* main_loop returns only if return_on_sigret == 1 && rt_sigreturn is invoked.
     see also: rt_sigsuspend */

  while (task_run() == 0) {

    /* dump_instr(); */
    /* print_regs(); */

    uint64_t exit_reason;
    vmm_read_vmcs(VMCS_RO_EXIT_REASON, &exit_reason);

    switch (exit_reason) {
    case VMX_REASON_VMCALL:
      printk("reason: vmcall\n");
      assert(false);
      break;

    case VMX_REASON_EXC_NMI: {
      /* References:
       * - Intel SDM 27.2.2, Table 24-15: Information for VM Exits Due to Vectored Events
       */
      uint64_t exc_info;
      vmm_read_vmcs(VMCS_RO_VMEXIT_IRQ_INFO, &exc_info);

      int int_type = (exc_info & 0x700) >> 8;
      switch (int_type) {
      default:
        assert(false);
      case VMCS_EXCTYPE_EXTERNAL_INTERRUPT:
      case VMCS_EXCTYPE_NONMASKTABLE_INTERRUPT:
        /* nothing we can do, host os handles it */
        continue;
      case VMCS_EXCTYPE_HARDWARE_EXCEPTION: /* including invalid opcode */
      case VMCS_EXCTYPE_SOFTWARE_EXCEPTION: /* including break points, overflows */
        break;
      }

      int exc_vec = exc_info & 0xff;
      switch (exc_vec) {
      case X86_VEC_PF: {
        /* FIXME */
        uint64_t gladdr;
        vmm_read_vmcs(VMCS_RO_EXIT_QUALIFIC, &gladdr);
        printk("page fault: caused by guest linear address 0x%llx\n", gladdr);
        send_signal(getpid(), LINUX_SIGSEGV);
      }
      case X86_VEC_UD: {
        uint64_t instlen, rip;
        vmm_read_vmcs(VMCS_RO_VMEXIT_INSTR_LEN, &instlen);
        vmm_read_register(HV_X86_RIP, &rip);
        if (is_syscall(instlen, rip)) {
          int r = handle_syscall();
          vmm_read_register(HV_X86_RIP, &rip); /* reload rip for execve */
          vmm_write_register(HV_X86_RIP, rip + 2);
          if (return_on_sigret && r < 0) {
            return;
          }
          continue;
        }
        /* FIXME */
        warnk("invalid opcode! (rip = %p): ", (void *) rip);
        unsigned char inst[instlen];
        if (copy_from_user(inst, rip, instlen))
          assert(false);
        for (uint64_t i = 0; i < instlen; ++i)
          fprintf(stderr, "%02x ", inst[i] & 0xff);
        fprintf(stderr, "\n");
        send_signal(getpid(), LINUX_SIGILL);
      }
      case X86_VEC_DE:
      case X86_VEC_DB:
      case X86_VEC_BP:
      case X86_VEC_OF:
      case X86_VEC_BR:
      case X86_VEC_NM:
      case X86_VEC_DF:
      case X86_VEC_TS:
      case X86_VEC_NP:
      case X86_VEC_SS:
      case X86_VEC_GP:
      case X86_VEC_MF:
      case X86_VEC_AC:
      case X86_VEC_MC:
      case X86_VEC_XM:
      case X86_VEC_VE:
      case X86_VEC_SX:
      default:
        /* FIXME */
        warnk("exception thrown: %d\n", exc_vec);
        uint64_t instlen, rip;
        vmm_read_vmcs(VMCS_RO_VMEXIT_INSTR_LEN, &instlen);
        vmm_read_register(HV_X86_RIP, &rip);
        fprintf(stderr, "inst: \n");
        unsigned char inst[instlen];
        if (copy_from_user(inst, rip, instlen))
          assert(false);
        for (uint64_t i = 0; i < instlen; ++i)
          fprintf(stderr, "%02x ", inst[i] & 0xff);
        fprintf(stderr, "\n");
        exit(1);                /* TODO */
      }
      break;
    }

    case VMX_REASON_EPT_VIOLATION:
      printk("reason: ept_violation\n");

      uint64_t gpaddr;
      vmm_read_vmcs(VMCS_GUEST_PHYSICAL_ADDRESS, &gpaddr);
      printk("guest-physical address = 0x%llx\n", gpaddr);

      uint64_t qual;

      vmm_read_vmcs(VMCS_RO_EXIT_QUALIFIC, &qual);
      printk("exit qualification = 0x%llx\n", qual);

      if (qual & (1 << 7)) {
        uint64_t gladdr;
        vmm_read_vmcs(VMCS_RO_GUEST_LIN_ADDR, &gladdr);
        printk("guest linear address = 0x%llx\n", gladdr);

        int verify = 0;
        if (qual & (1 << 0)) {
          verify = VERIFY_READ;
        } else if (qual & (1 << 1)) {
          verify = VERIFY_WRITE;
        } else if (qual & (1 << 2)) {
          verify = VERIFY_EXEC;
        }

        if (!addr_ok(gladdr, verify)) {
          printk("page fault: caused by guest linear address 0x%llx\n", gladdr);
          send_signal(getpid(), LINUX_SIGSEGV);
        }
      } else {
        printk("guest linear address = (unavailable)\n");
      }
      break;

    case VMX_REASON_CPUID: {
      uint64_t rax;
      vmm_read_register(HV_X86_RAX, &rax);
      unsigned eax, ebx, ecx, edx;
      __get_cpuid(rax, &eax, &ebx, &ecx, &edx);

      vmm_write_register(HV_X86_RAX, eax);
      vmm_write_register(HV_X86_RBX, ebx);
      vmm_write_register(HV_X86_RCX, ecx);
      vmm_write_register(HV_X86_RDX, edx);

      uint64_t rip;
      vmm_read_register(HV_X86_RIP, &rip);
      vmm_write_register(HV_X86_RIP, rip + 2);

      break;
    }

    case VMX_REASON_IRQ: {
      break;
    }

    case VMX_REASON_HLT: {
      break;
    }

    default:
      printk("other reason: %llu\n", exit_reason);
    }
  }

  __builtin_unreachable();
}

void
init_vmcs()
{
  uint64_t vmx_cap_pinbased, vmx_cap_procbased, vmx_cap_procbased2, vmx_cap_entry, vmx_cap_exit;

  hv_vmx_read_capability(HV_VMX_CAP_PINBASED, &vmx_cap_pinbased);
  hv_vmx_read_capability(HV_VMX_CAP_PROCBASED, &vmx_cap_procbased);
  hv_vmx_read_capability(HV_VMX_CAP_PROCBASED2, &vmx_cap_procbased2);
  hv_vmx_read_capability(HV_VMX_CAP_ENTRY, &vmx_cap_entry);
  hv_vmx_read_capability(HV_VMX_CAP_EXIT, &vmx_cap_exit);

  /* set up vmcs misc */

#define cap2ctrl(cap,ctrl) (((ctrl) | ((cap) & 0xffffffff)) & ((cap) >> 32))

  vmm_write_vmcs(VMCS_CTRL_PIN_BASED, cap2ctrl(vmx_cap_pinbased, 0));
  vmm_write_vmcs(VMCS_CTRL_CPU_BASED, cap2ctrl(vmx_cap_procbased,
                                               CPU_BASED_HLT |
                                               CPU_BASED_CR8_LOAD |
                                               CPU_BASED_CR8_STORE));
  vmm_write_vmcs(VMCS_CTRL_CPU_BASED2, cap2ctrl(vmx_cap_procbased2, 0));
  vmm_write_vmcs(VMCS_CTRL_VMENTRY_CONTROLS,  cap2ctrl(vmx_cap_entry,
                                                       VMENTRY_LOAD_EFER |
                                                       VMENTRY_GUEST_IA32E));
  vmm_write_vmcs(VMCS_CTRL_VMEXIT_CONTROLS, cap2ctrl(vmx_cap_exit, VMEXIT_LOAD_EFER));
  vmm_write_vmcs(VMCS_CTRL_EXC_BITMAP, 0xffffffff);
  vmm_write_vmcs(VMCS_CTRL_CR0_SHADOW, 0);
  vmm_write_vmcs(VMCS_CTRL_CR4_MASK, 0);
  vmm_write_vmcs(VMCS_CTRL_CR4_SHADOW, 0);
}

void
init_special_regs()
{
  uint64_t cr0;
  vmm_read_vmcs(VMCS_GUEST_CR0, &cr0);
  vmm_write_vmcs(VMCS_GUEST_CR0, (cr0 & ~CR0_EM) | CR0_MP);

  uint64_t cr4;
  vmm_read_vmcs(VMCS_GUEST_CR4, &cr4);
  vmm_write_vmcs(VMCS_GUEST_CR4, cr4 | CR4_PAE | CR4_OSFXSR | CR4_OSXMMEXCPT | CR4_VMXE | CR4_OSXSAVE);

  uint64_t efer;
  vmm_read_vmcs(VMCS_GUEST_IA32_EFER, &efer);
  vmm_write_vmcs(VMCS_GUEST_IA32_EFER, efer | EFER_LME | EFER_LMA);
}

struct gate_desc idt[256] __page_aligned;
gaddr_t idt_ptr;

void
init_idt()
{
  idt_ptr = kmap(idt, 0x1000, HV_MEMORY_READ | HV_MEMORY_WRITE);

  vmm_write_vmcs(VMCS_GUEST_IDTR_BASE, idt_ptr);
  vmm_write_vmcs(VMCS_GUEST_IDTR_LIMIT, sizeof idt);
}

void
init_regs()
{
  /* set up cpu regs */
  vmm_write_register(HV_X86_RFLAGS, 0x2);
}

void
init_msr()
{
  vmm_enable_native_msr(MSR_TIME_STAMP_COUNTER, 1);
  vmm_enable_native_msr(MSR_TSC_AUX, 1);
  vmm_enable_native_msr(MSR_KERNEL_GS_BASE, 1);
}

void
init_fpu()
{
  struct fxregs_state {
    uint16_t cwd; /* Control Word                    */
    uint16_t swd; /* Status Word                     */
    uint16_t twd; /* Tag Word                        */
    uint16_t fop; /* Last Instruction Opcode         */
    union {
      struct {
        uint64_t rip; /* Instruction Pointer             */
        uint64_t rdp; /* Data Pointer                    */
      };
      struct {
        uint32_t fip; /* FPU IP Offset                   */
        uint32_t fcs; /* FPU IP Selector                 */
        uint32_t foo; /* FPU Operand Offset              */
        uint32_t fos; /* FPU Operand Selector            */
      };
    };
    uint32_t mxcsr;       /* MXCSR Register State */
    uint32_t mxcsr_mask;  /* MXCSR Mask           */
    uint32_t st_space[32]; /* 8*16 bytes for each FP-reg = 128 bytes */
    uint32_t xmm_space[64]; /* 16*16 bytes for each XMM-reg = 256 bytes */
    uint32_t __padding[12];
    union {
      uint32_t __padding1[12];
      uint32_t sw_reserved[12];
    };
  } __attribute__((aligned(16))) fx;

  /* emulate 'fninit'
   * - http://www.felixcloutier.com/x86/FINIT:FNINIT.html
   */
  fx.cwd = 0x037f;
  fx.swd = 0;
  fx.twd = 0xffff;
  fx.fop = 0;
  fx.rip = 0;
  fx.rdp = 0;

  /* default configuration for the SIMD core */
  fx.mxcsr = 0x1f80;
  fx.mxcsr_mask = 0;

  vmm_write_fpstate(&fx, sizeof fx);
}

static void
init_vkernel()
{
  init_mm(&vkern_mm);
  init_shm_malloc();
  init_vmcs();
  init_msr();
  init_page();
  init_special_regs();
  init_segment();
  init_idt();
  init_regs();
  init_fpu();

  vkern = shm_malloc(sizeof *vkern);
  init_vfs();
}

static void
init_first_proc(int rootfd)
{
  struct rlimit rl;
  getrlimit(RLIMIT_NOFILE, &rl);

  proc = (struct proc) {
    .nr_tasks = 1,
    .lock = PTHREAD_RWLOCK_INITIALIZER,
    .mm = malloc(sizeof(struct mm)),
    .vfs_root = rootfd,
    .vfs_lock = PTHREAD_RWLOCK_INITIALIZER,
    .fdtab = calloc(sizeof(struct file *), rl.rlim_cur),
    .nr_fdtab = rl.rlim_cur,
  };
  INIT_LIST_HEAD(&proc.tasks);
  list_add(&task.tasks, &proc.tasks);
  init_mm(proc.mm);
  init_signal();
  vfs_expose_darwinfs_fd(0);
  vfs_expose_darwinfs_fd(1);
  vfs_expose_darwinfs_fd(2);
}

void
drop_privilege(void)
{
  if (seteuid(getuid()) != 0) {
    abort();
  }
}

void
elevate_privilege(void)
{
  if (seteuid(0) != 0) {
    abort();
  }
}

void
die_with_forcedsig(int sig)
{
  // TODO: Termination processing
  
  /* Force default signal action */
  int dsig = linux_to_darwin_signal(sig);
  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, dsig);
  sigprocmask(SIG_SETMASK, &mask, NULL);
  struct sigaction act;
  act.sa_handler = SIG_DFL;
  act.sa_flags = 0;
  sigaction(dsig, &act, NULL);
  raise(dsig);
  assert(false); // sig should be one that can terminate procs
}

int
main(int argc, char *argv[], char **envp)
{
  drop_privilege();

  static struct option long_options[] = {
    { "output", required_argument, NULL, 'o' },
    { "strace", required_argument, NULL, 's' },
    { "mnt", required_argument, NULL, 'm' },
    { 0, 0, 0, 0 }
  };
  int c, option_index = 0;

  char root[PATH_MAX] = {0};

  while ((c = getopt_long(argc, argv, "+o:w:s:m:", long_options, &option_index)) != -1) {
    switch (c) {
    case 'o':
      init_printk(optarg);
      break;
    case 'w':
      init_warnk(optarg);
      break;
    case 's':
      init_meta_strace(optarg);
      break;
    case 'm':
      if (realpath(optarg, root) == NULL) {
        perror("Invalid --mnt flag: ");
        exit(1);
      }
      argv[optind - 1] = root;
      break;
    }
  }

  argc -= optind;
  argv += optind;

  if (argc == 0) {
    abort();
  }

  vmm_create();
  init_vkernel();

  int rootfd = open(root, O_RDONLY | O_DIRECTORY);
  if (rootfd < 0) {
    perror("could not open initial root directory");
    exit(1);
  }
  init_first_proc(rootfd);

  int err;
  if ((err = do_exec(argv[0], argc, argv, envp)) < 0) {
    errno = linux_to_darwin_errno(-err);
    perror("Error");
    exit(1);
  }

  main_loop(0);

  vmm_destroy();

  return 0;
}
