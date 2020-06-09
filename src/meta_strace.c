#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>

#include "common.h"
#include "syscall.h"
#include "noah.h"

static FILE *strace_sink;
pthread_mutex_t strace_sync = PTHREAD_MUTEX_INITIALIZER;

void
init_meta_strace(const char *path)
{
  init_sink(path, &strace_sink, "strace");
}

typedef void meta_strace_hook(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret);

meta_strace_hook *strace_pre_hooks[NR_SYSCALLS];
meta_strace_hook *strace_post_hooks[NR_SYSCALLS];

void
print_gstr(gstr_t str, int maxlen)
{
  fprintf(strace_sink, "\"");
  for (int i = 0; i < maxlen; i++) {
    char c = *((char*)guest_to_host(str + i));
    if (c == '\0') {
      break;
    } else if (c == '\n') {
      fprintf(strace_sink, "\\n");
    } else if (!isprint(c)) {
      fprintf(strace_sink, "\\x%02x", c);
    } else {
      fprintf(strace_sink, "%c", c);
    }
  }
  fprintf(strace_sink, "\"");
}

void
print_arg(int syscall_num, int arg_idx, const char *arg_name, const char *type_name, uint64_t val)
{
    fprintf(strace_sink, "%s: ", arg_name);

    if (strcmp(type_name, "gstr_t") == 0) {
      print_gstr(val, 50);

    } else if (strcmp(type_name, "gaddr_t") == 0) {
      fprintf(strace_sink, "0x%016llx [host: 0x%016llx]", val, (uint64_t)guest_to_host(val));

    } else if (strcmp(type_name, "int") == 0) {
      fprintf(strace_sink, "%lld", val);

    } else {
      fprintf(strace_sink, "0x%llx", val);
    }
}

void
print_args(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
  }
}

void
print_ret(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  fprintf(strace_sink, "): ret = 0x%llx", ret);
  if ((int64_t)ret < 0) {
    fprintf(strace_sink, "[%s]", linux_errno_str(-ret));
  }
  fprintf(strace_sink, "\n");
}

void
do_meta_strace(int syscall_num, char *syscall_name, meta_strace_hook def, meta_strace_hook **hooks, uint64_t ret,  va_list ap)
{
  int argc = 0;
  char *argnames[6];
  char *typenames[6];
  uint64_t vals[6];
  for (int i = 0; i < 6; i++) {
    typenames[i] = va_arg(ap, char*);
    argnames[i] = va_arg(ap, char*);
    vals[i] = va_arg(ap, uint64_t);

    if (typenames[i][0] == '0') {
      break;
    }
    argc++;
  }

  if (strcmp(syscall_name, "unimplemented") == 0) {
    fprintf(strace_sink, "<unimplemented systemcall>");
    def(-1, argc, argnames, typenames, vals, ret);
    return;
  }

  if (hooks[syscall_num]) {
    hooks[syscall_num](syscall_num, argc, argnames, typenames, vals, ret);
  } else {
    def(syscall_num, argc, argnames, typenames, vals, ret);
  }
}

void
meta_strace_info(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  char *mes;

  vasprintf(&mes, fmt, ap);

  fprintf(strace_sink, "INFO: %s", mes);

  free(mes);
  va_end(ap);
}

/*
 * Called before systemcall.
 * Most systemcalls are traced here,
 * but result values stored in argument pointers cannot be traced. (such as read)
 */
void
meta_strace_pre(int syscall_num, char *syscall_name, ...)
{
  va_list ap;
  va_start(ap, syscall_name);

  if (!strace_sink) {
    va_end(ap);
    return;
  }

  uint64_t tid;
  pthread_threadid_np(NULL, &tid);

  pthread_mutex_lock(&strace_sync);
  fprintf(strace_sink, "[%d:%lld] %s(", getpid(), tid, syscall_name);

  do_meta_strace(syscall_num, syscall_name, print_args, strace_pre_hooks, 0, ap);

  fflush(strace_sink);
  pthread_mutex_unlock(&strace_sync);

  va_end(ap);
}

// Called after systemcall.
void
meta_strace_post(int syscall_num, char *syscall_name, uint64_t ret, ...)
{
  va_list ap;
  va_start(ap, ret);

  if (!strace_sink) {
    va_end(ap);
    return;
  }

  pthread_mutex_lock(&strace_sync);

  do_meta_strace(syscall_num, syscall_name, print_ret, strace_post_hooks, ret, ap);

  fflush(strace_sink);
  pthread_mutex_unlock(&strace_sync);

  va_end(ap);
}

void
meta_strace_sigdeliver(int signum)
{
  if (!strace_sink) {
    return;
  }

  uint64_t tid;
  pthread_threadid_np(NULL, &tid);

  pthread_mutex_lock(&strace_sync);

  fprintf(strace_sink, "[%d:%lld] --- %s ---\n", getpid(), tid, linux_signum_str(signum));

  fflush(strace_sink);
  pthread_mutex_unlock(&strace_sync);
}

void
trace_read_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  // Print nothing
}

/* Print buf after syscall in order to show what stinrg is actually read */
void
trace_read_post(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    if (i == 1) {
      fprintf(strace_sink, "%s: ", argnames[1]);
      print_gstr(vals[1], MIN(50, ret)); // Print buf as string
    } else {
      print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
    }
  }

  print_ret(syscall_num, argc, argnames, typenames, vals, ret);
}

void
trace_write_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    if (i == 1) {
      fprintf(strace_sink, "%s: ", argnames[1]);
      print_gstr(vals[1], MIN(50, vals[2])); // Print buf as string
    } else {
      print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
    }
  }
}

void
trace_recvfrom_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  // Print nothing
}

/* Print buf after syscall in order to show what stinrg is actually recvfrom */
void
trace_recvfrom_post(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    if (i == 1) {
      fprintf(strace_sink, "%s: ", argnames[1]);
      print_gstr(vals[1], MIN(50, ret)); // Print buf as string
    } else {
      print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
    }
  }

  print_ret(syscall_num, argc, argnames, typenames, vals, ret);
}

void
trace_sendto_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    if (i == 1) {
      fprintf(strace_sink, "%s: ", argnames[1]);
      print_gstr(vals[1], MIN(50, vals[2])); // Print buf as string
    } else {
      print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
    }
  }
}

void
trace_execve_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  int exec_argc = 0;
  gaddr_t gargv = vals[1];

  while (((gaddr_t*)guest_to_host(gargv))[exec_argc] != 0) exec_argc++;
  char *dargv[exec_argc];
  for (int i = 0; i < exec_argc; i++) {
    dargv[i] = (char*)guest_to_host(((gaddr_t*)guest_to_host(gargv))[i]);
  }

  print_arg(syscall_num, 0, argnames[0], typenames[0], vals[0]); // path
  /* argc */
  fprintf(strace_sink, ", [");
  for (int i = 0; i < exec_argc; i++) {
    fprintf(strace_sink, "\"%s\", ", dargv[i]);
  }
  fprintf(strace_sink, "], ");
  print_arg(syscall_num, 2, argnames[2], typenames[2], vals[2]); // envp
}

static void
print_sigset(l_sigset_t *sigset)
{
  fprintf(strace_sink, "[");
  for (int i = 1; i < LINUX_SIGRTMIN; i++) {
    if (LINUX_SIGISMEMBER(sigset, i)) {
      fprintf(strace_sink, "%s, ", linux_signum_str(i));
    }
  }
  fprintf(strace_sink, "]");
}

void
trace_rt_sigprocmask_pre(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
}

void
trace_rt_sigprocmask_post(int syscall_num, int argc, char *argnames[6], char *typenames[6], uint64_t vals[6], uint64_t ret)
{
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      fprintf(strace_sink, ", ");
    }
    if (i == 0) {
      // how
      fprintf(strace_sink, "%s: ", argnames[1]);
      switch  (vals[i]) {
        case LINUX_SIG_BLOCK:
          fprintf(strace_sink, "BLOCK");
          break;
        case LINUX_SIG_UNBLOCK:
          fprintf(strace_sink, "UNBLOCK");
          break;
        case LINUX_SIG_SETMASK:
          fprintf(strace_sink, "SETMASK");
          break;
        default:
          fprintf(strace_sink, "UNKNOWN_HOW");
          break;
      }
    } else if (i == 1 || i == 2) {
      if (vals[i] == 0) {
        fprintf(strace_sink, "NULL (");
        print_sigset(&task.sigmask);
        fprintf(strace_sink, ")");
        continue;
      }
      l_sigset_t lset;
      if (copy_from_user(&lset, vals[i], sizeof(l_sigset_t)))  {
        fprintf(strace_sink, "FAULT...");
        continue;
      }
      print_sigset(&lset);
    } else {
      print_arg(syscall_num, i, argnames[i], typenames[i], vals[i]);
    }
  }

  print_ret(syscall_num, argc, argnames, typenames, vals, ret);
}

meta_strace_hook *strace_pre_hooks[NR_SYSCALLS] = {
  [LSYS_read] = trace_read_pre,
  [LSYS_recvfrom] = trace_recvfrom_pre,
  [LSYS_write] = trace_write_pre,
  [LSYS_sendto] = trace_sendto_pre,
  [LSYS_execve] = trace_execve_pre,
  [LSYS_rt_sigprocmask] = trace_rt_sigprocmask_post,
};
meta_strace_hook *strace_post_hooks[NR_SYSCALLS] = {
  [LSYS_read] = trace_read_post,
  [LSYS_recvfrom] = trace_recvfrom_post,
  [LSYS_rt_sigprocmask] = trace_rt_sigprocmask_pre,
};

