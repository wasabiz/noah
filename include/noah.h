#ifndef NOAH_H
#define NOAH_H

#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include "types.h"
#include "util/misc.h"
#include "util/list.h"
#include "util/khash.h"
#include "linux/mman.h"
#include "malloc.h"
#include "version.h"
#include <stdnoreturn.h>

#define __page_aligned __attribute__((aligned(0x1000)))

/* privilege management */

void drop_privilege(void);
void elevate_privilege(void);

/* interface to user memory */

void *guest_to_host(gaddr_t);

#define VERIFY_READ  LINUX_PROT_READ
#define VERIFY_WRITE LINUX_PROT_WRITE
#define VERIFY_EXEC  LINUX_PROT_EXEC
bool addr_ok(gaddr_t, int verify);

size_t copy_from_user(void *haddr, gaddr_t gaddr, size_t n); /* returns 0 on success */
ssize_t strncpy_from_user(void *haddr, gaddr_t gaddr, size_t n);
size_t copy_to_user(gaddr_t gaddr, const void *haddr, size_t n);
ssize_t strnlen_user(gaddr_t gaddr, size_t n);

/* linux emulation */

uint64_t do_gettid(void);
int do_exec(const char *elf_path, int argc, char *argv[], char **envp);
int do_faccessat(int l_dirfd, const char *l_path, int l_mode);
int do_access(const char *path, int l_mode);
int do_futex_wake(gaddr_t uaddr, int count);
int user_open(const char *path, int flags, int mode);
int vkern_open(const char *path, int flags, int mode);
int user_openat(int fd, const char *path, int flags, int mode);
int vkern_openat(int fd, const char *path, int flags, int mode);
int user_close(int fd);
int vkern_close(int fd);
void close_cloexec();
int register_fd(int fd, bool is_cloexec);
int vkern_dup_fd(int fd, bool is_cloexec);
gaddr_t alloc_region(size_t len);

noreturn void die_with_forcedsig(int sig);
void main_loop(int return_on_sigret);

/* signal */

#include "linux/signal.h"

typedef atomic_uint_least64_t atomic_sigbits_t;

#define INIT_SIGBIT(sigbit) (*(sigbit) = ATOMIC_VAR_INIT(0))
void handle_signal(void);
bool has_sigpending(void);
int send_signal(pid_t pid, int sig);

/* task related data */

struct task {
  struct list_head head;
  gaddr_t set_child_tid, clear_child_tid;
  uint64_t tid;
  gaddr_t robust_list;
  l_sigset_t sigmask;
  atomic_sigbits_t sigpending;
  l_stack_t sas;
};

struct fdtable {
  int start; // First fd number of this table
  int size;  // Current table size expressed in number of bits
  struct file **files;
  uint64_t *open_fds;
  uint64_t *cloexec_fds;
};

struct fileinfo {
  int rootfd;                      // FS root
  struct fdtable fdtable;          // File descriptors for the user space
  struct fdtable vkern_fdtable;    // File descriptors for the kernel space
  pthread_rwlock_t fdtable_lock;
};

// We manage uid and suid independently on Darwin since we cannot change those of Darwin's freely.
// Wa always hold 0 in Darwin's suid to emulate Linux suid behavior (Note: in the case where Noah has setuid bit).
struct cred {
  pthread_rwlock_t lock;
  l_uid_t uid;
  l_uid_t euid;
  l_uid_t suid;
};

/* for private futex */
struct pfutex_entry {
  struct list_head head;
  pthread_cond_t cond;
  gaddr_t uaddr;
  uint32_t bitset;
};

/* TODO: collect garbage entries */
KHASH_MAP_INIT_INT64(pfutex, struct list_head *)

struct proc {
  int nr_tasks;
  struct list_head tasks;
  pthread_rwlock_t lock;
  struct cred cred;
  struct mm *mm;
  struct {
    pthread_rwlock_t sig_lock;
    l_sigaction_t sigaction[LINUX_NSIG];
  };
  struct {
    pthread_mutex_t futex_mutex;
    khash_t(pfutex) *pfutex; /* TODO: modify khash and make this field being non-pointer */
  };
  struct fileinfo fileinfo;
};

extern struct proc proc;
_Thread_local extern struct task task;

void init_signal(void);
void reset_signal_state(void);
void init_fileinfo(int rootfd);

void init_fpu(void);

/* Linux kernel constants */

#define LINUX_RELEASE "4.6.4"
#define LINUX_VERSION "#1 SMP PREEMPT Mon Jul 11 19:12:32 CEST 2016" /* FIXME */

#define LINUX_PATH_MAX 4096         /* including null */

/* conversion */

struct stat;
struct l_newstat;
struct statfs;
struct termios;
struct linux_termios;
struct l_statfs;
struct winsize;
struct linux_winsize;
struct rlimit; struct l_rlimit;

int linux_to_darwin_at_flags(int flags);
int linux_to_darwin_o_flags(int l_flags);
int darwin_to_linux_o_flags(int r);
void stat_darwin_to_linux(struct stat *stat, struct l_newstat *lstat);
void statfs_darwin_to_linux(struct statfs *statfs, struct l_statfs *l_statfs);
void darwin_to_linux_termios(struct termios *bios, struct linux_termios *lios);
void linux_to_darwin_termios(struct linux_termios *lios, struct termios *bios);
void darwin_to_linux_winsize(struct winsize *ws, struct linux_winsize *lws);
void linux_to_darwin_winsize(struct winsize *ws, struct linux_winsize *lws);
int linux_to_darwin_signal(int signum);
int darwin_to_linux_signal(int signum);
void darwin_to_linux_rlimit(int resource, struct rlimit *darwin_rlimit, struct l_rlimit *linux_rlimit);
void darwin_to_linux_rlimit_nofile(struct rlimit *darwin_rlimit, struct l_rlimit *linux_rlimit);


/* debug */

#include "debug.h"

#endif
