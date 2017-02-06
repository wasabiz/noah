#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "noah.h"
#include "vmm.h"
#include "mm.h"
#include "x86/vm.h"
#include "elf.h"

#include "linux/common.h"
#include "linux/mman.h"
#include "linux/misc.h"
#include "linux/time.h"
#include "linux/fs.h"

void init_userstack(int argc, char *argv[], char **envp, uint64_t exe_base, const Elf64_Ehdr *ehdr, uint64_t global_offset, uint64_t interp_base);

int
load_elf_interp(const char *path, ulong load_addr)
{
  char *data;
  Elf64_Ehdr *h;
  uint64_t map_top = 0;
  int fd;
  struct stat st;

  if ((fd = openat_darwinfs(LINUX_AT_FDCWD, path, LINUX_O_RDONLY)) < 0) {
    warnk("load_elf_interp, could not open file: %s\n", path);
    return fd;
  }

  fstat(fd, &st);

  data = mmap(0, st.st_size, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);

  close(fd);

  h = (Elf64_Ehdr *)data;

  assert(IS_ELF(*h));

  if (! (h->e_type == ET_EXEC || h->e_type == ET_DYN)) {
    return -LINUX_ENOEXEC;
  }
  if (h->e_machine != EM_X86_64) {
    return -LINUX_ENOEXEC;
  }

  Elf64_Phdr *p = (Elf64_Phdr *)(data + h->e_phoff);

  for (int i = 0; i < h->e_phnum; i++) {
    if (p[i].p_type != PT_LOAD) {
      continue;
    }

    ulong p_vaddr = p[i].p_vaddr + load_addr;

    ulong mask = PAGE_SIZE(PAGE_4KB) - 1;
    ulong vaddr = p_vaddr & ~mask;
    ulong offset = p_vaddr & mask;
    ulong size = roundup(p[i].p_memsz + offset, PAGE_SIZE(PAGE_4KB));

    int prot = 0;
    if (p[i].p_flags & PF_X) prot |= LINUX_PROT_EXEC;
    if (p[i].p_flags & PF_W) prot |= LINUX_PROT_WRITE;
    if (p[i].p_flags & PF_R) prot |= LINUX_PROT_READ;

    assert(vaddr != 0);
    do_mmap(vaddr, size, PROT_READ | PROT_WRITE, prot, LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS, -1, 0);

    copy_to_user(vaddr + offset, data + p[i].p_offset, p[i].p_filesz);

    map_top = MAX(map_top, roundup(vaddr + size, PAGE_SIZE(PAGE_4KB)));
  }

  vmm_write_vmcs(VMCS_GUEST_RIP, load_addr + h->e_entry);
  proc.mm->start_brk = map_top;

  munmap(data, st.st_size);

  return 0;
}

int
load_elf(Elf64_Ehdr *ehdr, int argc, char *argv[], char **envp)
{
  uint64_t map_top = 0;

  assert(IS_ELF(*ehdr));

  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    fprintf(stderr, "not an executable file");
    fflush(stderr);
    return -LINUX_ENOEXEC;
  }
  if (ehdr->e_machine != EM_X86_64) {
    fprintf(stderr, "not an x64 executable");
    fflush(stderr);
    return -LINUX_ENOEXEC;
  }

  Elf64_Phdr *p = (Elf64_Phdr *)((char *)ehdr + ehdr->e_phoff);

  uint64_t load_base = 0;
  bool load_base_set = false;

  ulong global_offset = 0;
  if (ehdr->e_type == ET_DYN) {
    /* NB: Program headers in elf files of ET_DYN can have 0 as their own p_vaddr. */
    global_offset = 0x400000;   /* default base address */
  }

  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (p[i].p_type != PT_LOAD) {
      continue;
    }

    ulong p_vaddr = p[i].p_vaddr + global_offset;

    ulong mask = PAGE_SIZE(PAGE_4KB) - 1;
    ulong vaddr = p_vaddr & ~mask;
    ulong offset = p_vaddr & mask;
    ulong size = roundup(p[i].p_memsz + offset, PAGE_SIZE(PAGE_4KB));

    int prot = 0;
    if (p[i].p_flags & PF_X) prot |= LINUX_PROT_EXEC;
    if (p[i].p_flags & PF_W) prot |= LINUX_PROT_WRITE;
    if (p[i].p_flags & PF_R) prot |= LINUX_PROT_READ;

    assert(vaddr != 0);
    do_mmap(vaddr, size, PROT_READ | PROT_WRITE, prot, LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS, -1, 0);

    copy_to_user(vaddr + offset, (char *)ehdr + p[i].p_offset, p[i].p_filesz);

    if (! load_base_set) {
      load_base = p[i].p_vaddr - p[i].p_offset + global_offset;
      load_base_set = true;
    }
    map_top = MAX(map_top, roundup(vaddr + size, PAGE_SIZE(PAGE_4KB)));
  }

  assert(load_base_set);

  int i;
  bool interp = false;
  for (i = 0; i < ehdr->e_phnum; i++) {
    if (p[i].p_type == PT_INTERP) {
      interp = true;
      break;
    }
  }
  if (interp) {
    char interp_path[p[i].p_filesz + 1];
    memcpy(interp_path, (char *)ehdr + p[i].p_offset, p[i].p_filesz);
    interp_path[p[i].p_filesz] = 0;

    int err;
    if ((err = load_elf_interp(interp_path, map_top)) < 0) {
      return err;
    }
  }
  else {
    vmm_write_vmcs(VMCS_GUEST_RIP, ehdr->e_entry + global_offset);
    proc.mm->start_brk = map_top;
  }

  init_userstack(argc, argv, envp, load_base, ehdr, global_offset, interp ? map_top : 0);

  return 0;
}

#define SB_ARGC_MAX 2

int
load_script(const char *script, size_t len, const char *elf_path, int argc, char *argv[], char **envp)
{
  const char *script_end = script + len;
  char sb_argv[SB_ARGC_MAX][LINUX_PATH_MAX];
  int sb_argc;
  size_t n;

  script += 2;                  /* skip shebang */

  for (sb_argc = 0; sb_argc < SB_ARGC_MAX; ++sb_argc) {
    while (isspace(*script) && *script != '\n') {
      if (script == script_end)
        goto parse_end;
      script++;
    }

    for (n = 0; ! isspace(script[n]); ++n) {
      if (script + n == script_end)
        goto parse_end;
    }
    if (n == 0) {
      goto parse_end;
    }
    if (n > LINUX_PATH_MAX - 1) {
      return -LINUX_ENAMETOOLONG;
    }
    strncpy(sb_argv[sb_argc], script, n);
    sb_argv[sb_argc][n] = 0;

    script += n;                /* skip interp */
  }

 parse_end:
  if (sb_argc == 0) {
    return -LINUX_EFAULT;
  }

  int newargc = sb_argc + argc;
  char *newargv[newargc];
  for (int i = 0; i < sb_argc; ++i) {
    newargv[i] = sb_argv[i];
  }
  newargv[sb_argc] = elf_path;
  memcpy(newargv + sb_argc + 1, argv + 1, (argc - 1) * sizeof(char *));

  do_exec(newargv[0], newargc, newargv, envp);

  return 0;
}

uint64_t
push(const void *data, size_t n)
{
  uint64_t size = roundup(n, 8);
  uint64_t rsp;

  assert(data != 0);

  vmm_read_register(HV_X86_RSP, &rsp);
  rsp -= size;
  vmm_write_register(HV_X86_RSP, rsp);

  copy_to_user(rsp, data, n);

  return rsp;
}

void
init_userstack(int argc, char *argv[], char **envp, uint64_t exe_base, const Elf64_Ehdr *ehdr, uint64_t global_offset, uint64_t interp_base)
{
  static const uint64_t zero = 0;

  do_mmap(STACK_TOP - STACK_SIZE, STACK_SIZE, PROT_READ | PROT_WRITE, LINUX_PROT_READ | LINUX_PROT_WRITE, LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS, -1, 0);

  vmm_write_register(HV_X86_RSP, STACK_TOP);
  vmm_write_register(HV_X86_RBP, STACK_TOP);

  char random[16];

  uint64_t rand_ptr = push(random, sizeof random);

  char **renvp;
  for (renvp = envp; *renvp; ++renvp)
    ;

  uint64_t total = 0, args_total = 0;

  for (int i = 0; i < argc; ++i) {
    total += strlen(argv[i]) + 1;
  }
  args_total = total;
  for (char **e = envp; *e; ++e) {
    total += strlen(*e) + 1;
  }

  char buf[total];

  uint64_t off = 0;

  for (int i = 0; i < argc; ++i) {
    size_t len = strlen(argv[i]);
    memcpy(buf + off, argv[i], len + 1);
    off += len + 1;
  }
  for (char **e = envp; *e; ++e) {
    size_t len = strlen(*e);
    memcpy(buf + off, *e, len + 1);
    off += len + 1;
  }

  uint64_t args_start = push(buf, total);
  uint64_t args_end = args_start + args_total, env_end = args_start + total;

  Elf64_Auxv aux[] = {
    { AT_BASE, interp_base },
    { AT_ENTRY, ehdr->e_entry + global_offset },
    { AT_PHDR, exe_base + ehdr->e_phoff },
    { AT_PHENT, ehdr->e_phentsize },
    { AT_PHNUM, ehdr->e_phnum },
    { AT_PAGESZ, PAGE_SIZE(PAGE_4KB) },
    { AT_RANDOM, rand_ptr },
    { AT_NULL, 0 },
  };

  push(aux, sizeof aux);

  push(&zero, sizeof zero);

  uint64_t ptr = env_end;
  for (char **e = renvp - 1; e >= envp; --e) {
    ptr -= strlen(*e) + 1;
    push(&ptr, sizeof ptr);
    assert(strcmp(buf + (ptr - args_start), *e) == 0);
  }

  push(&zero, sizeof zero);

  ptr = args_end;
  for (int i = argc - 1; i >= 0; --i) {
    ptr -= strlen(argv[i]) + 1;
    push(&ptr, sizeof ptr);
    assert(strcmp(buf + (ptr - args_start), argv[i]) == 0);
  }

  uint64_t argc64 = argc;
  push(&argc64, sizeof argc64);
}

static void
init_reg_state(void)
{
  vmm_write_register(HV_X86_RAX, 0);
  vmm_write_register(HV_X86_RBX, 0);
  vmm_write_register(HV_X86_RCX, 0);
  vmm_write_register(HV_X86_RDX, 0);
  vmm_write_register(HV_X86_RSI, 0);
  vmm_write_register(HV_X86_RDI, 0);
  vmm_write_register(HV_X86_R8, 0);
  vmm_write_register(HV_X86_R9, 0);
  vmm_write_register(HV_X86_R10, 0);
  vmm_write_register(HV_X86_R11, 0);
  vmm_write_register(HV_X86_R12, 0);
  vmm_write_register(HV_X86_R13, 0);
  vmm_write_register(HV_X86_R14, 0);
  vmm_write_register(HV_X86_R15, 0);

  vmm_write_vmcs(VMCS_GUEST_FS, 0);
  vmm_write_vmcs(VMCS_GUEST_ES, 0);
  vmm_write_vmcs(VMCS_GUEST_GS, 0);
  vmm_write_vmcs(VMCS_GUEST_DS, 0);
  vmm_write_vmcs(VMCS_GUEST_CS, GSEL(SEG_CODE, 0));
  vmm_write_vmcs(VMCS_GUEST_DS, GSEL(SEG_DATA, 0));

  vmm_write_vmcs(VMCS_GUEST_FS_BASE, 0);
  vmm_write_vmcs(VMCS_GUEST_GS_BASE, 0);

  vmm_write_vmcs(VMCS_GUEST_LDTR, 0);

  init_fpu();
}

static void
reset_process_state(void)
{
  /* Reinitialize proc and task structures */
  /* Not handling locks seriously now because multi-thread execve is not implemented yet */
  proc.nr_tasks = 1;
  destroy_mm(proc.mm); // munlock is also done by unmapping mm
  init_mm(proc.mm);
  init_reg_state();
  reset_signal_state();
  // TODO: destroy LDT if it is implemented

  task.clear_child_tid = task.set_child_tid = 0;

  // TODO: use a smarter algorithm than bruteforce...
  for (size_t i = 0; i < proc.nr_fdtab; i++) {
    int flag = fcntl(i, F_GETFD);
    if (flag < 0) {
      continue;
    }
    if (flag & FD_CLOEXEC) {
      do_close(i);
    }
  }
}

int
do_exec(const char *elf_path, int argc, char *argv[], char **envp)
{
  int err;
  int fd;
  struct stat st;
  char *data;

  if (proc.nr_tasks > 1) {
    warnk("Multi-thread execve is not implemented yet\n");
    return -LINUX_EINVAL;
  }

  if ((fd = openat_darwinfs(LINUX_AT_FDCWD, elf_path, LINUX_O_RDONLY)) < 0) {
    return fd;
  }
  fstat(fd, &st);
  if ((st.st_mode & S_IXUSR) == 0) {
    warnk("FIXME: more strict permission check\n");
    return -LINUX_EACCES;
  }

  reset_process_state();

  data = mmap(0, st.st_size, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);

  close(fd);

  drop_privilege();

  if (4 <= st.st_size && memcmp(data, ELFMAG, 4) == 0) {
    if ((err = load_elf((Elf64_Ehdr *) data, argc, argv, envp)) < 0)
      return err;
    if (st.st_mode & 04000) {
      elevate_privilege();
    }
  }
  else if (2 <= st.st_size && data[0] == '#' && data[1] == '!') {
    if ((err = load_script(data, st.st_size, elf_path, argc, argv, envp)) < 0)
      return err;
  }
  else if (4 <= st.st_size && memcmp(data, "\xcf\xfa\xed\xfe", 4) == 0) {
    /* Mach-O */
    return syswrap(execve(elf_path, argv, envp));
  }
  else {
    return -LINUX_ENOEXEC;                  /* unsupported file type */
  }

  munmap(data, st.st_size);
  proc.mm->current_brk = proc.mm->start_brk;

  return 0;
}

DEFINE_SYSCALL(execve, gstr_t, gelf_path, gaddr_t, gargv, gaddr_t, genvp)
{
  int err;
  char elf_path[LINUX_PATH_MAX];
  strncpy_from_user(elf_path, gelf_path, sizeof elf_path);

  size_t argv_rsrv = 1024;
  char **argv = malloc(sizeof(char *) * argv_rsrv);
  size_t argc = 0;
  while (true) {
    size_t i = argc;
    gaddr_t addr;
    if (copy_from_user(&addr, gargv + sizeof(gaddr_t) * i, sizeof addr)) {
      err = -LINUX_EFAULT;
      goto argv_out;
    }
    if (addr == 0)
      break;
    argc++;
    if (argc + 1 > LINUX_MAX_ARG_STRINGS) {
      err = -LINUX_E2BIG;
      goto argv_out;
    }
    if (argc + 1 > argv_rsrv) {
      argv_rsrv *= 2;
      argv = realloc(argv, sizeof(char *) * argv_rsrv);
    }
    int size = strnlen_user(addr, LINUX_MAX_ARG_STRLEN);
    if (size == 0) {
      err = -LINUX_EFAULT;
      goto argv_out;
    }
    if (size > LINUX_MAX_ARG_STRLEN) {
      err = -LINUX_E2BIG;
      goto argv_out;
    }
    argv[i] = alloca(size);
    copy_from_user(argv[i], addr, size); /* always success */
  }
  argv[argc] = NULL;

  size_t envp_rsrv = 1024;
  char **envp = malloc(sizeof(char *) * envp_rsrv);
  size_t envc = 0;
  while (true) {
    size_t i = envc;
    gaddr_t addr;
    if (copy_from_user(&addr, genvp + sizeof(gaddr_t) * i, sizeof addr)) {
      err = -LINUX_EFAULT;
      goto envp_out;
    }
    if (addr == 0)
      break;
    envc++;
    if (envc + 1 > LINUX_MAX_ARG_STRINGS) {
      err = -LINUX_E2BIG;
      goto envp_out;
    }
    if (envc + 1 > envp_rsrv) {
      envp_rsrv *= 2;
      envp = realloc(envp, sizeof(char *) * envp_rsrv);
    }
    int size = strnlen_user(addr, LINUX_MAX_ARG_STRLEN);
    if (size == 0) {
      err = -LINUX_EFAULT;
      goto envp_out;
    }
    if (size > LINUX_MAX_ARG_STRLEN) {
      err = -LINUX_E2BIG;
      goto envp_out;
    }
    envp[i] = alloca(size);
    copy_from_user(envp[i], addr, size); /* always success */
  }
  envp[envc] = NULL;

  err = do_exec(elf_path, argc, argv, envp);
  if (err < 0) {
    return err;
  }

  uint64_t entry;
  vmm_read_register(HV_X86_RIP, &entry);
  vmm_write_register(HV_X86_RIP, entry - 2); // because syscall handler adds 2 to current rip when returning to vmm_run

  return 0;

 envp_out:
  free(envp);
 argv_out:
  free(argv);
  return err;
}
