#include "common.h"
#include "noah.h"

#include "linux/common.h"
#include "linux/time.h"
#include "linux/fs.h"
#include "linux/misc.h"
#include "linux/errno.h"
#include "linux/ioctl.h"
#include "linux/termios.h"
#include "linux/signal.h"

#include <unistd.h>
#include <sys/stat.h>
#include <termios.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/mount.h>
#include <sys/syslimits.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>

int
linux_to_darwin_at_flags(int flags)
{
  int ret = 0;
  if (flags == LINUX_AT_FDCWD) {
    return AT_FDCWD;
  }
  if (flags & LINUX_AT_SYMLINK_NOFOLLOW) {
    ret |= AT_SYMLINK_NOFOLLOW;
    flags &= ~LINUX_AT_SYMLINK_NOFOLLOW;
  }
  /* You must treat E_ACCESS as E_REMOVEDIR in unlinkat */\
  if (flags & LINUX_AT_EACCESS) {
    ret |= AT_EACCESS;
    flags &= ~LINUX_AT_EACCESS;
  }
  if (flags & LINUX_AT_SYMLINK_FOLLOW) {
    ret |= AT_SYMLINK_FOLLOW;
    flags &= ~LINUX_AT_SYMLINK_FOLLOW;
  }

  if (flags) {
    warnk("unsupported at flags:0x%x\n", flags);
  }

  return ret;
}

int
linux_to_darwin_o_flags(int l_flags)
{
  int ret = 0;
  if (l_flags & LINUX_O_PATH) {
    if (l_flags & LINUX_O_CLOEXEC)
      ret |= O_CLOEXEC;
    if (l_flags & LINUX_O_NOFOLLOW)
      ret |= O_SYMLINK;
    if (l_flags & LINUX_O_DIRECTORY)
      ret |= O_DIRECTORY;
    return ret;
  }
  switch (l_flags & LINUX_O_ACCMODE) {
  case LINUX_O_WRONLY:
    ret |= O_WRONLY;
    break;
  case LINUX_O_RDWR:
    ret |= O_RDWR;
    break;
  default:                      /* Note: LINUX_O_RDONLY == 0 */
    ret |= O_RDONLY;
  }
  if (l_flags & LINUX_O_NDELAY)
    ret |= O_NONBLOCK;
  if (l_flags & LINUX_O_APPEND)
    ret |= O_APPEND;
  if (l_flags & LINUX_O_SYNC)
    ret |= O_FSYNC;
  if (l_flags & LINUX_O_NONBLOCK)
    ret |= O_NONBLOCK;
  if (l_flags & LINUX_FASYNC)
    ret |= O_ASYNC;
  if (l_flags & LINUX_O_CREAT)
    ret |= O_CREAT;
  if (l_flags & LINUX_O_TRUNC)
    ret |= O_TRUNC;
  if (l_flags & LINUX_O_EXCL)
    ret |= O_EXCL;
  if (l_flags & LINUX_O_NOCTTY)
    ret |= O_NOCTTY;
  /* if (l_flags & LINUX_O_DIRECT) */
  /*   ret |= O_DIRECT; */
  if (l_flags & LINUX_O_NOFOLLOW)
    ret |= O_NOFOLLOW;
  if (l_flags & LINUX_O_DIRECTORY)
    ret |= O_DIRECTORY;

  return ret;
}

int
darwin_to_linux_o_flags(int r)
{
  int flags = 0;
#define TEST(X,Y) do { if (r & X) { flags |= Y; r &= ~X; } } while (0)
  TEST(O_RDONLY, LINUX_O_RDONLY);
  TEST(O_WRONLY, LINUX_O_WRONLY);
  TEST(O_RDWR, LINUX_O_RDWR);
  TEST(O_APPEND, LINUX_O_APPEND);
  TEST(O_NONBLOCK, LINUX_O_NONBLOCK);
  TEST(O_FSYNC, LINUX_FASYNC);
  TEST(O_CREAT, LINUX_O_CREAT);
  TEST(O_TRUNC, LINUX_O_TRUNC);
  TEST(O_EXCL, LINUX_O_EXCL);
  TEST(O_NOCTTY, LINUX_O_NOCTTY);
  TEST(O_NOFOLLOW, LINUX_O_NOFOLLOW);
  TEST(O_DIRECTORY, LINUX_O_DIRECTORY);
  TEST(O_CLOEXEC, LINUX_O_CLOEXEC);
  assert(r == 0);
  return flags;
}

void
stat_darwin_to_linux(struct stat *stat, struct l_newstat *lstat)
{
  lstat->st_dev = minor(stat->st_dev) | (major(stat->st_dev) << 8);
  lstat->st_ino = stat->st_ino;
  lstat->st_mode = stat->st_mode;
  lstat->st_nlink = stat->st_nlink;
  lstat->st_uid = stat->st_uid;
  lstat->st_gid = stat->st_gid;
  lstat->st_rdev = stat->st_rdev;
  lstat->st_size = stat->st_size;
  lstat->st_atim.tv_sec = stat->st_atimespec.tv_sec;
  lstat->st_atim.tv_nsec = stat->st_atimespec.tv_nsec;
  lstat->st_mtim.tv_sec = stat->st_mtimespec.tv_sec;
  lstat->st_mtim.tv_nsec = stat->st_mtimespec.tv_nsec;
  lstat->st_ctim.tv_sec = stat->st_ctimespec.tv_sec;
  lstat->st_ctim.tv_nsec = stat->st_ctimespec.tv_nsec;
  lstat->st_blksize = stat->st_blksize;
  lstat->st_blocks = stat->st_blocks;
}

void
statfs_darwin_to_linux(struct statfs *statfs, struct l_statfs *l_statfs)
{
#define HFS_SUPER_MAGIC 0x4244
  l_statfs->f_type = HFS_SUPER_MAGIC;
  l_statfs->f_bsize = statfs->f_bsize;
  l_statfs->f_blocks = statfs->f_blocks;
  l_statfs->f_bfree = statfs->f_bfree;
  l_statfs->f_bavail = statfs->f_bavail;
  l_statfs->f_ffree = statfs->f_ffree;
  l_statfs->f_files = statfs->f_files;
  l_statfs->f_fsid.val[0] = statfs->f_fsid.val[0];
  l_statfs->f_fsid.val[1] = statfs->f_fsid.val[1];
  l_statfs->f_namelen = NAME_MAX;
  l_statfs->f_frsize = statfs->f_bsize;

  l_statfs->f_flags = LINUX_ST_VALID;
  if (statfs->f_flags & MNT_RDONLY)
    l_statfs->f_flags |= LINUX_ST_RDONLY;
  if (statfs->f_flags & MNT_NOSUID)
    l_statfs->f_flags |= LINUX_ST_NOSUID;
  if (statfs->f_flags & MNT_NODEV)
    l_statfs->f_flags |= LINUX_ST_NODEV;
  if (statfs->f_flags & MNT_NOEXEC)
    l_statfs->f_flags |= LINUX_ST_NOEXEC;
  if (statfs->f_flags & MNT_SYNCHRONOUS)
    l_statfs->f_flags |= LINUX_ST_SYNCHRONOUS;

  /* TODO: ST_MANDLOCK, ST_NOATIME, ST_NODIRATIME, ST_RELATIME */
}

static struct speedtab {
	int sp_speed;			/* Speed. */
	int sp_code;			/* Code. */
} sptab[] = {
	{ B0, LINUX_B0 }, { B50, LINUX_B50 },
	{ B75, LINUX_B75 }, { B110, LINUX_B110 },
	{ B134, LINUX_B134 }, { B150, LINUX_B150 },
	{ B200, LINUX_B200 }, { B300, LINUX_B300 },
	{ B600, LINUX_B600 }, { B1200, LINUX_B1200 },
	{ B1800, LINUX_B1800 }, { B2400, LINUX_B2400 },
	{ B4800, LINUX_B4800 }, { B9600, LINUX_B9600 },
	{ B19200, LINUX_B19200 }, { B38400, LINUX_B38400 },
	{ B57600, LINUX_B57600 }, { B115200, LINUX_B115200 },
	{-1, -1 }
};

static int
darwin_to_linux_speed(int speed, struct speedtab *table)
{
	for ( ; table->sp_speed != -1; table++)
		if (table->sp_speed == speed)
			return (table->sp_code);
	return -1;
}

static int
linux_to_darwin_speed(int code, struct speedtab *table)
{
	for ( ; table->sp_code != -1; table++)
		if (table->sp_code == code)
			return (table->sp_speed);
	return -1;
}

void
darwin_to_linux_termios(struct termios *bios, struct linux_termios *lios)
{
	int i;

	lios->c_iflag = 0;
	if (bios->c_iflag & IGNBRK)
		lios->c_iflag |= LINUX_IGNBRK;
	if (bios->c_iflag & BRKINT)
		lios->c_iflag |= LINUX_BRKINT;
	if (bios->c_iflag & IGNPAR)
		lios->c_iflag |= LINUX_IGNPAR;
	if (bios->c_iflag & PARMRK)
		lios->c_iflag |= LINUX_PARMRK;
	if (bios->c_iflag & INPCK)
		lios->c_iflag |= LINUX_INPCK;
	if (bios->c_iflag & ISTRIP)
		lios->c_iflag |= LINUX_ISTRIP;
	if (bios->c_iflag & INLCR)
		lios->c_iflag |= LINUX_INLCR;
	if (bios->c_iflag & IGNCR)
		lios->c_iflag |= LINUX_IGNCR;
	if (bios->c_iflag & ICRNL)
		lios->c_iflag |= LINUX_ICRNL;
	if (bios->c_iflag & IXON)
		lios->c_iflag |= LINUX_IXON;
	if (bios->c_iflag & IXANY)
		lios->c_iflag |= LINUX_IXANY;
	if (bios->c_iflag & IXOFF)
		lios->c_iflag |= LINUX_IXOFF;
	if (bios->c_iflag & IMAXBEL)
		lios->c_iflag |= LINUX_IMAXBEL;

	lios->c_oflag = 0;
	if (bios->c_oflag & OPOST)
		lios->c_oflag |= LINUX_OPOST;
	if (bios->c_oflag & ONLCR)
		lios->c_oflag |= LINUX_ONLCR;
	if (bios->c_oflag & TAB3)
		lios->c_oflag |= LINUX_XTABS;

	lios->c_cflag = darwin_to_linux_speed(bios->c_ispeed, sptab);
	lios->c_cflag |= (bios->c_cflag & CSIZE) >> 4;
	if (bios->c_cflag & CSTOPB)
		lios->c_cflag |= LINUX_CSTOPB;
	if (bios->c_cflag & CREAD)
		lios->c_cflag |= LINUX_CREAD;
	if (bios->c_cflag & PARENB)
		lios->c_cflag |= LINUX_PARENB;
	if (bios->c_cflag & PARODD)
		lios->c_cflag |= LINUX_PARODD;
	if (bios->c_cflag & HUPCL)
		lios->c_cflag |= LINUX_HUPCL;
	if (bios->c_cflag & CLOCAL)
		lios->c_cflag |= LINUX_CLOCAL;
	if (bios->c_cflag & CRTSCTS)
		lios->c_cflag |= LINUX_CRTSCTS;

	lios->c_lflag = 0;
	if (bios->c_lflag & ISIG)
		lios->c_lflag |= LINUX_ISIG;
	if (bios->c_lflag & ICANON)
		lios->c_lflag |= LINUX_ICANON;
	if (bios->c_lflag & ECHO)
		lios->c_lflag |= LINUX_ECHO;
	if (bios->c_lflag & ECHOE)
		lios->c_lflag |= LINUX_ECHOE;
	if (bios->c_lflag & ECHOK)
		lios->c_lflag |= LINUX_ECHOK;
	if (bios->c_lflag & ECHONL)
		lios->c_lflag |= LINUX_ECHONL;
	if (bios->c_lflag & NOFLSH)
		lios->c_lflag |= LINUX_NOFLSH;
	if (bios->c_lflag & TOSTOP)
		lios->c_lflag |= LINUX_TOSTOP;
	if (bios->c_lflag & ECHOCTL)
		lios->c_lflag |= LINUX_ECHOCTL;
	if (bios->c_lflag & ECHOPRT)
		lios->c_lflag |= LINUX_ECHOPRT;
	if (bios->c_lflag & ECHOKE)
		lios->c_lflag |= LINUX_ECHOKE;
	if (bios->c_lflag & FLUSHO)
		lios->c_lflag |= LINUX_FLUSHO;
	if (bios->c_lflag & PENDIN)
		lios->c_lflag |= LINUX_PENDIN;
	if (bios->c_lflag & IEXTEN)
		lios->c_lflag |= LINUX_IEXTEN;

	for (i=0; i<LINUX_NCCS; i++)
		lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	lios->c_cc[LINUX_VINTR] = bios->c_cc[VINTR];
	lios->c_cc[LINUX_VQUIT] = bios->c_cc[VQUIT];
	lios->c_cc[LINUX_VERASE] = bios->c_cc[VERASE];
	lios->c_cc[LINUX_VKILL] = bios->c_cc[VKILL];
	lios->c_cc[LINUX_VEOF] = bios->c_cc[VEOF];
	lios->c_cc[LINUX_VEOL] = bios->c_cc[VEOL];
	lios->c_cc[LINUX_VMIN] = bios->c_cc[VMIN];
	lios->c_cc[LINUX_VTIME] = bios->c_cc[VTIME];
	lios->c_cc[LINUX_VEOL2] = bios->c_cc[VEOL2];
	lios->c_cc[LINUX_VSUSP] = bios->c_cc[VSUSP];
	lios->c_cc[LINUX_VSTART] = bios->c_cc[VSTART];
	lios->c_cc[LINUX_VSTOP] = bios->c_cc[VSTOP];
	lios->c_cc[LINUX_VREPRINT] = bios->c_cc[VREPRINT];
	lios->c_cc[LINUX_VDISCARD] = bios->c_cc[VDISCARD];
	lios->c_cc[LINUX_VWERASE] = bios->c_cc[VWERASE];
	lios->c_cc[LINUX_VLNEXT] = bios->c_cc[VLNEXT];

	for (i=0; i<LINUX_NCCS; i++) {
		if (i != LINUX_VMIN && i != LINUX_VTIME &&
		    lios->c_cc[i] == _POSIX_VDISABLE)
			lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	}
	lios->c_line = 0;
}

void
linux_to_darwin_termios(struct linux_termios *lios, struct termios *bios)
{
	int i;

	bios->c_iflag = 0;
	if (lios->c_iflag & LINUX_IGNBRK)
		bios->c_iflag |= IGNBRK;
	if (lios->c_iflag & LINUX_BRKINT)
		bios->c_iflag |= BRKINT;
	if (lios->c_iflag & LINUX_IGNPAR)
		bios->c_iflag |= IGNPAR;
	if (lios->c_iflag & LINUX_PARMRK)
		bios->c_iflag |= PARMRK;
	if (lios->c_iflag & LINUX_INPCK)
		bios->c_iflag |= INPCK;
	if (lios->c_iflag & LINUX_ISTRIP)
		bios->c_iflag |= ISTRIP;
	if (lios->c_iflag & LINUX_INLCR)
		bios->c_iflag |= INLCR;
	if (lios->c_iflag & LINUX_IGNCR)
		bios->c_iflag |= IGNCR;
	if (lios->c_iflag & LINUX_ICRNL)
		bios->c_iflag |= ICRNL;
	if (lios->c_iflag & LINUX_IXON)
		bios->c_iflag |= IXON;
	if (lios->c_iflag & LINUX_IXANY)
		bios->c_iflag |= IXANY;
	if (lios->c_iflag & LINUX_IXOFF)
		bios->c_iflag |= IXOFF;
	if (lios->c_iflag & LINUX_IMAXBEL)
		bios->c_iflag |= IMAXBEL;

	bios->c_oflag = 0;
	if (lios->c_oflag & LINUX_OPOST)
		bios->c_oflag |= OPOST;
	if (lios->c_oflag & LINUX_ONLCR)
		bios->c_oflag |= ONLCR;
	if (lios->c_oflag & LINUX_XTABS)
		bios->c_oflag |= TAB3;

	bios->c_cflag = (lios->c_cflag & LINUX_CSIZE) << 4;
	if (lios->c_cflag & LINUX_CSTOPB)
		bios->c_cflag |= CSTOPB;
	if (lios->c_cflag & LINUX_CREAD)
		bios->c_cflag |= CREAD;
	if (lios->c_cflag & LINUX_PARENB)
		bios->c_cflag |= PARENB;
	if (lios->c_cflag & LINUX_PARODD)
		bios->c_cflag |= PARODD;
	if (lios->c_cflag & LINUX_HUPCL)
		bios->c_cflag |= HUPCL;
	if (lios->c_cflag & LINUX_CLOCAL)
		bios->c_cflag |= CLOCAL;
	if (lios->c_cflag & LINUX_CRTSCTS)
		bios->c_cflag |= CRTSCTS;

	bios->c_lflag = 0;
	if (lios->c_lflag & LINUX_ISIG)
		bios->c_lflag |= ISIG;
	if (lios->c_lflag & LINUX_ICANON)
		bios->c_lflag |= ICANON;
	if (lios->c_lflag & LINUX_ECHO)
		bios->c_lflag |= ECHO;
	if (lios->c_lflag & LINUX_ECHOE)
		bios->c_lflag |= ECHOE;
	if (lios->c_lflag & LINUX_ECHOK)
		bios->c_lflag |= ECHOK;
	if (lios->c_lflag & LINUX_ECHONL)
		bios->c_lflag |= ECHONL;
	if (lios->c_lflag & LINUX_NOFLSH)
		bios->c_lflag |= NOFLSH;
	if (lios->c_lflag & LINUX_TOSTOP)
		bios->c_lflag |= TOSTOP;
	if (lios->c_lflag & LINUX_ECHOCTL)
		bios->c_lflag |= ECHOCTL;
	if (lios->c_lflag & LINUX_ECHOPRT)
		bios->c_lflag |= ECHOPRT;
	if (lios->c_lflag & LINUX_ECHOKE)
		bios->c_lflag |= ECHOKE;
	if (lios->c_lflag & LINUX_FLUSHO)
		bios->c_lflag |= FLUSHO;
	if (lios->c_lflag & LINUX_PENDIN)
		bios->c_lflag |= PENDIN;
	if (lios->c_lflag & LINUX_IEXTEN)
		bios->c_lflag |= IEXTEN;

	for (i=0; i<NCCS; i++)
		bios->c_cc[i] = _POSIX_VDISABLE;
	bios->c_cc[VINTR] = lios->c_cc[LINUX_VINTR];
	bios->c_cc[VQUIT] = lios->c_cc[LINUX_VQUIT];
	bios->c_cc[VERASE] = lios->c_cc[LINUX_VERASE];
	bios->c_cc[VKILL] = lios->c_cc[LINUX_VKILL];
	bios->c_cc[VEOF] = lios->c_cc[LINUX_VEOF];
	bios->c_cc[VEOL] = lios->c_cc[LINUX_VEOL];
	bios->c_cc[VMIN] = lios->c_cc[LINUX_VMIN];
	bios->c_cc[VTIME] = lios->c_cc[LINUX_VTIME];
	bios->c_cc[VEOL2] = lios->c_cc[LINUX_VEOL2];
	bios->c_cc[VSUSP] = lios->c_cc[LINUX_VSUSP];
	bios->c_cc[VSTART] = lios->c_cc[LINUX_VSTART];
	bios->c_cc[VSTOP] = lios->c_cc[LINUX_VSTOP];
	bios->c_cc[VREPRINT] = lios->c_cc[LINUX_VREPRINT];
	bios->c_cc[VDISCARD] = lios->c_cc[LINUX_VDISCARD];
	bios->c_cc[VWERASE] = lios->c_cc[LINUX_VWERASE];
	bios->c_cc[VLNEXT] = lios->c_cc[LINUX_VLNEXT];

	for (i=0; i<NCCS; i++) {
		if (i != VMIN && i != VTIME &&
		    bios->c_cc[i] == LINUX_POSIX_VDISABLE)
			bios->c_cc[i] = _POSIX_VDISABLE;
	}

	bios->c_ispeed = bios->c_ospeed =
	    linux_to_darwin_speed(lios->c_cflag & LINUX_CBAUD, sptab);
}

void
darwin_to_linux_winsize(struct winsize *ws, struct linux_winsize *lws)
{
  lws->ws_row = ws->ws_row;
  lws->ws_col = ws->ws_col;
  lws->ws_xpixel = ws->ws_xpixel;
  lws->ws_ypixel = ws->ws_ypixel;
}

void
linux_to_darwin_winsize(struct winsize *ws, struct linux_winsize *lws)
{
  ws->ws_row = lws->ws_row;
  ws->ws_col = lws->ws_col;
  ws->ws_xpixel = lws->ws_xpixel;
  ws->ws_ypixel = lws->ws_ypixel;
}

int
linux_to_darwin_signal(int signum)
{
  if (signum < LINUX_SIGRTMIN) {
    return linux_to_darwin_signum(signum);
  } else {
    // TODO: RT signals
    return 0;
  }
}

int
darwin_to_linux_signal(int signum)
{
  if (signum < LINUX_SIGRTMIN) {
    return darwin_to_linux_signum(signum);
  } else {
    // TODO: RT signals
    return 0;
  }
}

void
linux_to_darwin_sigset(l_sigset_t *lset, sigset_t *dset)
{
  sigemptyset(dset);
  for (int i = 1; i <= LINUX_SIGRTMAX; i++) {
    if (!LINUX_SIGISMEMBER(lset, i)) {
      continue;
    }
    if (i == LINUX_SIGSTKFLT || i == LINUX_SIGPWR) {
      continue; // No corresponding signals in Darwin
    }
    sigaddset(dset, linux_to_darwin_signal(i));
  }
}

void
darwin_to_linux_sigset(sigset_t *dset, l_sigset_t *lset)
{
  LINUX_SIGEMPTYSET(lset);
  for (int i = 1; i <= LINUX_SIGRTMIN; i++) {
    if (sigismember(dset, i)) {
      int num = darwin_to_linux_signal(i);
      if (num > 0) {
        LINUX_SIGADDSET(lset, num);
      }
    }
  }
}

void
linux_to_darwin_sigaction(l_sigaction_t *lsa, struct sigaction *dsa, void *handler)
{

  linux_to_darwin_sigset(&lsa->lsa_mask, &dsa->sa_mask);
  dsa->sa_handler = handler;
  dsa->sa_flags = 0;
  if (lsa->lsa_flags & LINUX_SA_NOCLDSTOP)
    dsa->sa_flags |= SA_NOCLDSTOP;
  if (lsa->lsa_flags & LINUX_SA_NOCLDWAIT)
    dsa->sa_flags |= SA_NOCLDWAIT;
  if (lsa->lsa_flags & LINUX_SA_SIGINFO)
    dsa->sa_flags |= SA_SIGINFO;
  if (lsa->lsa_flags & LINUX_SA_ONSTACK)
    dsa->sa_flags |= SA_ONSTACK;
  if (lsa->lsa_flags & LINUX_SA_RESTART)
    dsa->sa_flags |= SA_RESTART;
  if (lsa->lsa_flags & LINUX_SA_ONESHOT)
    dsa->sa_flags |= SA_RESETHAND;
  if (lsa->lsa_flags & LINUX_SA_NOMASK)
    dsa->sa_flags |= SA_NODEFER;
}

void
darwin_to_linux_sigaction(struct sigaction *dsa, l_sigaction_t *lsa, gaddr_t handler)
{

  darwin_to_linux_sigset(&dsa->sa_mask, &lsa->lsa_mask);
  lsa->lsa_handler = handler;
  lsa->lsa_restorer = 0;
  lsa->lsa_flags = 0;
  if (dsa->sa_flags & SA_NOCLDSTOP)
    lsa->lsa_flags |= LINUX_SA_NOCLDSTOP;
  if (dsa->sa_flags & SA_NOCLDWAIT)
    lsa->lsa_flags |= LINUX_SA_NOCLDWAIT;
  if (dsa->sa_flags & SA_SIGINFO)
    lsa->lsa_flags |= LINUX_SA_SIGINFO;
  if (dsa->sa_flags & SA_ONSTACK)
    lsa->lsa_flags |= LINUX_SA_ONSTACK;
  if (dsa->sa_flags & SA_RESTART)
    lsa->lsa_flags |= LINUX_SA_RESTART;
  if (dsa->sa_flags & SA_RESETHAND)
    lsa->lsa_flags |= LINUX_SA_ONESHOT;
  if (dsa->sa_flags & SA_NODEFER)
    lsa->lsa_flags |= LINUX_SA_NOMASK;
}

void
linux_to_darwin_flock(struct l_flock *linux_flock, struct flock *darwin_flock)
{
  switch (linux_flock->l_type) {
    case LINUX_F_RDLCK:
      darwin_flock->l_type = F_RDLCK;
      break;
    case LINUX_F_WRLCK:
      darwin_flock->l_type = F_WRLCK;
      break;
    case LINUX_F_UNLCK:
      darwin_flock->l_type = F_UNLCK;
      break;
    default:
      darwin_flock->l_type = -1;
      break;
  }
  darwin_flock->l_whence = linux_flock->l_whence;
  darwin_flock->l_start = (off_t)linux_flock->l_start;
  darwin_flock->l_len = (off_t)linux_flock->l_len;
  darwin_flock->l_pid = (pid_t)linux_flock->l_pid;
}

void
darwin_to_linux_flock(struct flock *darwin_flock, struct l_flock *linux_flock)
{
  switch (darwin_flock->l_type) {
    case F_RDLCK:
      linux_flock->l_type = LINUX_F_RDLCK;
      break;
    case F_WRLCK:
      linux_flock->l_type = LINUX_F_WRLCK;
      break;
    case F_UNLCK:
      linux_flock->l_type = LINUX_F_UNLCK;
      break;
  }
  linux_flock->l_whence = darwin_flock->l_whence;
  linux_flock->l_start = (l_off_t)darwin_flock->l_start;
  linux_flock->l_len = (l_off_t)darwin_flock->l_len;
  linux_flock->l_pid = (l_pid_t)darwin_flock->l_pid;
}

void
darwin_to_linux_rlimit(int resource, struct rlimit *darwin_rlimit, struct l_rlimit *linux_rlimit)
{
  switch (resource) {
    case RLIMIT_NOFILE:
      darwin_to_linux_rlimit_nofile(darwin_rlimit, linux_rlimit);
      break;
    default:
      *linux_rlimit = (struct l_rlimit) {
        .rlim_cur = darwin_rlimit->rlim_cur == RLIM_INFINITY ? LINUX_RLIM_INFINITY : darwin_rlimit->rlim_cur,
        .rlim_max = darwin_rlimit->rlim_max == RLIM_INFINITY ? LINUX_RLIM_INFINITY : darwin_rlimit->rlim_max
      };
  }
}
