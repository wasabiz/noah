#include "noah.h"
#include "common.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>

#include "linux/common.h"
#include "linux/socket.h"
#include "linux/misc.h"

DEFINE_SYSCALL(socket, int, family, int, type, int, protocol)
{
  int fd = syswrap(socket(linux_to_darwin_sa_family(family), type & ~(LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC), protocol));
  if (fd < 0)
    return fd;

  if (type & LINUX_SOCK_NONBLOCK) {
    int e = syswrap(fcntl(fd, F_SETFL, O_NONBLOCK));
    if (e < 0)
      return e;
  }
  if (type & LINUX_SOCK_CLOEXEC) {
    int e = syswrap(fcntl(fd, F_SETFD, FD_CLOEXEC));
    if (e < 0)
      return e;
  }
  vfs_expose_darwinfs_fd(fd);
  return fd;
}

int
linux_to_darwin_sockaddr(struct sockaddr **sockaddr, const struct l_sockaddr *l_sockaddr, size_t l_sockaddr_len)
{
  if (l_sockaddr == NULL) {
    return -1;
  }

  *sockaddr = malloc(l_sockaddr_len);

  memcpy(*sockaddr, l_sockaddr, l_sockaddr_len);
  assert(offsetof(struct sockaddr, sa_data) == offsetof(struct l_sockaddr, sa_data));
  (*sockaddr)->sa_family = l_sockaddr->sa_family;

  switch (linux_to_darwin_sa_family(l_sockaddr->sa_family)) {
  case AF_UNIX: {
    int slen;
    struct sockaddr_un *sockaddr_un = (struct sockaddr_un*)*sockaddr;

    if (sockaddr_un->sun_path[0] == '\0') {
      // Linux abstract namespace starts with NULL, which we do not support yet
      printk("Abstract namespace: %20s\n", &sockaddr_un->sun_path[1]);
      slen = strnlen(&sockaddr_un->sun_path[1], l_sockaddr_len - offsetof(struct sockaddr_un, sun_path) - 1);
    } else {
      slen = strnlen(sockaddr_un->sun_path, l_sockaddr_len - offsetof(struct sockaddr_un, sun_path));
    }
    (*sockaddr)->sa_len = sizeof(struct sockaddr_un);
    if ((*sockaddr)->sa_len < offsetof(struct sockaddr_un, sun_path) + slen) {
      // Name too long
      goto err;
    }
    break;
  }

  case AF_INET:
    (*sockaddr)->sa_len = sizeof(struct sockaddr_in);
    break;

  case AF_INET6:
    assert(l_sockaddr_len != sizeof(struct sockaddr_in6) - sizeof(uint32_t));
    // Fall through

  default:
    (*sockaddr)->sa_len = l_sockaddr_len;
    break;

  case -1:
    warnk("Unimplemented sa_family: 0x%x(%s)\n", l_sockaddr->sa_family, linux_sa_family_str(l_sockaddr->sa_family));
    goto err;
  }

  return 0;

err:
  free(*sockaddr);
  return -1;
}

void
darwin_to_linux_sockaddr(struct l_sockaddr *l_sockaddr, const struct sockaddr *sockaddr)
{
  if (sockaddr == NULL || l_sockaddr == NULL) {
    return;
  }
  assert((void*)l_sockaddr != (void*)sockaddr);
  memcpy(l_sockaddr, sockaddr, sockaddr->sa_len);
  l_sockaddr->sa_family = darwin_to_linux_sa_family(sockaddr->sa_family);
}

DEFINE_SYSCALL(connect, int, sockfd, gaddr_t, addr_ptr, uint64_t, addrlen)
{
  char addr[addrlen];

  if (copy_from_user(addr, addr_ptr, addrlen))
    return -LINUX_EFAULT;

  struct sockaddr *sockaddr;
  if (linux_to_darwin_sockaddr(&sockaddr, (struct l_sockaddr *) addr, addrlen) < 0) {
    return -1;
  }

  int fd = syswrap(connect(sockfd, sockaddr, sockaddr->sa_len));
  free(sockaddr);
  return fd;
}

int
linux_to_darwin_sockopt_level(int level)
{
  switch (level) {
  case LINUX_SOL_SOCKET: return SOL_SOCKET;
  default: return level; // Other values are the same as OSX as long as they exist.
  };
}

int
to_host_sockopt_name(int name)
{
  switch (name) {
  case LINUX_SO_DEBUG:
    return SO_DEBUG;
  case LINUX_SO_REUSEADDR:
    return SO_REUSEADDR;
  case LINUX_SO_TYPE:
    return SO_TYPE;
  case LINUX_SO_ERROR:
    return SO_ERROR;
  case LINUX_SO_DONTROUTE:
    return SO_DONTROUTE;
  case LINUX_SO_BROADCAST:
    return SO_BROADCAST;
  case LINUX_SO_SNDBUF:
    return SO_SNDBUF;
  case LINUX_SO_RCVBUF:
    return SO_RCVBUF;
  case LINUX_SO_KEEPALIVE:
    return SO_KEEPALIVE;
  case LINUX_SO_OOBINLINE:
    return SO_OOBINLINE;
  case LINUX_SO_LINGER:
    return SO_LINGER;
  case LINUX_SO_RCVLOWAT:
    return SO_RCVLOWAT;
  case LINUX_SO_SNDLOWAT:
    return SO_SNDLOWAT;
  case LINUX_SO_RCVTIMEO:
    return SO_RCVTIMEO;
  case LINUX_SO_SNDTIMEO:
    return SO_SNDTIMEO;
  case LINUX_SO_TIMESTAMP:
    return SO_TIMESTAMP;
  case LINUX_SO_ACCEPTCONN:
    return SO_ACCEPTCONN;
  default:
    printk("Unsupported sockopt name: 0x%x\n", name);
    return -1;
  }
}

DEFINE_SYSCALL(setsockopt, int, fd, int, level, int, optname, gaddr_t, optval_ptr, uint, opt_len)
{
  char optval[opt_len];
  if (copy_from_user(optval, optval_ptr, opt_len))
    return -LINUX_EFAULT;

  // Darwin's optval is compatible with that of Linux
  return syswrap(setsockopt(fd, linux_to_darwin_sockopt_level(level), to_host_sockopt_name(optname), optval, opt_len));
}

DEFINE_SYSCALL(getsockopt, int, fd, int, level, int, optname, gaddr_t, optval_ptr, gaddr_t, optlen_ptr)
{
  l_socklen_t len;
  if (copy_from_user(&len, optlen_ptr, sizeof len))
    return -LINUX_EFAULT;
  char optval[len];
  unsigned int optlen;
  // Darwin's optval is compatible with that of Linux
  int r = syswrap(getsockopt(fd, linux_to_darwin_sockopt_level(level), to_host_sockopt_name(optname), optval, &optlen));
  if (r >= 0) {
    if (copy_to_user(optval_ptr, optval, optlen))
      return -LINUX_EFAULT;
    l_socklen_t tmp = optlen;
    if (copy_to_user(optlen_ptr, &tmp, sizeof tmp))
      return -LINUX_EFAULT;
  }
  return r;
}

DEFINE_SYSCALL(shutdown, int, socket, int, how)
{
  return syswrap(shutdown(socket, how));
}

DEFINE_SYSCALL(sendto, int, socket, gaddr_t, buf_ptr, int, length, int, flags, gaddr_t, dest_addr, socklen_t, dest_len)
{
  warnk("sendto: dest_addr is not used! (dest_addr = 0x%llx, dest_len = %d)\n", dest_addr, dest_len);
  char buf[length];
  if (copy_from_user(buf, buf_ptr, sizeof buf))
    return -LINUX_EFAULT;
  return syswrap(sendto(socket, buf, length, flags, NULL, 0));
}

int
linux_to_darwin_msg_flags(l_int flags)
{
  int ret = 0;
  if (flags & LINUX_MSG_OOB) {
    ret |= MSG_OOB;
    flags &= ~LINUX_MSG_OOB;
  }
  if (flags & LINUX_MSG_PEEK) {
    ret |= MSG_PEEK;
    flags &= ~LINUX_MSG_PEEK;
  }
  if (flags & LINUX_MSG_DONTROUTE) {
    ret |= MSG_DONTROUTE;
    flags &= ~LINUX_MSG_DONTROUTE;
  }
  if (flags & LINUX_MSG_EOR) {
    ret |= MSG_EOR;
    flags &= ~LINUX_MSG_EOR;
  }
  if (flags & LINUX_MSG_TRUNC) {
    ret |= MSG_TRUNC;
    flags &= ~LINUX_MSG_TRUNC;
  }
  if (flags & LINUX_MSG_CTRUNC) {
    ret |= MSG_CTRUNC;
    flags &= ~LINUX_MSG_CTRUNC;
  }
  if (flags & LINUX_MSG_WAITALL) {
    ret |= MSG_WAITALL;
    flags &= ~LINUX_MSG_WAITALL;
  }
  if (flags & LINUX_MSG_DONTWAIT) {
    ret |= MSG_DONTWAIT;
    flags &= ~LINUX_MSG_DONTWAIT;
  }

  if (flags) {
    printk("unsupported msg_flags: 0x%x", flags);
    return -LINUX_EOPNOTSUPP;
  }

  return ret;
}

DEFINE_SYSCALL(recvfrom, int, socket, gaddr_t, buf_ptr, int, length, int, flags, gaddr_t, addr_ptr, gaddr_t, addrlen_ptr)
{
  socklen_t *socklen_ptr = NULL;
  struct sockaddr *sock_ptr = NULL;
  if (addr_ptr != 0) {
    l_socklen_t addrbuflen;
    if (copy_from_user(&addrbuflen, addrlen_ptr, sizeof addrbuflen))
      return -LINUX_EFAULT;
    socklen_ptr = alloca(sizeof *socklen_ptr);
    *socklen_ptr = addrbuflen;
    sock_ptr = alloca(addrbuflen);
  }
  char *buf = alloca(length);
  int ret = syswrap(recvfrom(socket, buf, length, flags, sock_ptr, socklen_ptr));
  if (ret < 0)
    return ret;
  if (copy_to_user(buf_ptr, buf, length))
    return -LINUX_EFAULT;
  if (addr_ptr != 0) {
    char addr[sock_ptr->sa_len];
    darwin_to_linux_sockaddr((struct l_sockaddr *) addr, sock_ptr);
    if (copy_to_user(addr_ptr, addr, sizeof addr))
      return -LINUX_EFAULT;
    if (copy_to_user(addrlen_ptr, socklen_ptr, sizeof *socklen_ptr))
      return -LINUX_EFAULT;
  }
  return ret;
}

static int
do_sendmsg(int sockfd, const struct l_msghdr *msg, int flags)
{
  struct msghdr hdr;

  if (msg->msg_controllen > INT_MAX)
    return -LINUX_ENOBUFS;

  hdr.msg_namelen = msg->msg_namelen;
  hdr.msg_name = NULL;
  if (hdr.msg_namelen > 0) {
    hdr.msg_name = alloca(hdr.msg_namelen);
    if (strncpy_from_user(hdr.msg_name, msg->msg_name, hdr.msg_namelen) < 0)
      return -LINUX_EFAULT;
  }
  hdr.msg_iovlen = msg->msg_iovlen;
  hdr.msg_iov = alloca(sizeof(struct iovec) * hdr.msg_iovlen);
  struct l_iovec *liov = alloca(sizeof(struct l_iovec) * hdr.msg_iovlen);
  if (copy_from_user(liov, msg->msg_iov, sizeof(struct l_iovec) * hdr.msg_iovlen))
    return -LINUX_EFAULT;
  for (int i = 0; i < hdr.msg_iovlen; ++i) {
    hdr.msg_iov[i].iov_len = liov[i].iov_len;
    hdr.msg_iov[i].iov_base = alloca(liov[i].iov_len);
    if (copy_from_user(hdr.msg_iov[i].iov_base, liov[i].iov_base, hdr.msg_iov[i].iov_len))
      return -LINUX_EFAULT;
  }
  hdr.msg_flags = linux_to_darwin_msg_flags(msg->msg_flags);
  if (hdr.msg_flags < 0) {
    warnk("do_sendmsg: unsupported flags\n");
    return hdr.msg_flags;
  }
  if (LINUX_CMSG_FIRSTHDR(msg) != 0) {
    warnk("we do not support ancillary data yet\n");
    return -LINUX_EINVAL;
  }
  hdr.msg_control = NULL;
  hdr.msg_controllen = 0;
  return syswrap(sendmsg(sockfd, &hdr, flags));
}

DEFINE_SYSCALL(sendmsg, int, sockfd, gaddr_t, msg_ptr, int, flags)
{
  struct l_msghdr msg;
  if (copy_from_user(&msg, msg_ptr, sizeof msg))
    return -LINUX_EFAULT;
  return do_sendmsg(sockfd, &msg, flags);
}

DEFINE_SYSCALL(sendmmsg, int, sockfd, gaddr_t, msgvec_ptr, unsigned int, vlen, unsigned int, flags)
{
  struct l_mmsghdr msg[vlen];
  if (copy_from_user(msg, msgvec_ptr, sizeof msg))
    return -LINUX_EFAULT;
  uint i;
  for (i = 0; i < vlen; ++i) {
    int err = do_sendmsg(sockfd, &msg[i].msg_hdr, flags);
    if (err < 0) {
      return err;
    }
    if (copy_to_user(msgvec_ptr + sizeof msg[0] * i + offsetof(struct l_mmsghdr, msg_len), &err, sizeof err))
      return -LINUX_EFAULT;
  }
  return i;
}

DEFINE_SYSCALL(recvmsg, int, sockfd, gaddr_t, msg_ptr, int, flags)
{
  struct l_msghdr lmsg;
  if (copy_from_user(&lmsg, msg_ptr, sizeof lmsg)) {
    return -LINUX_EFAULT;
  }
  char msg_name[lmsg.msg_name == 0 ? 0 : lmsg.msg_namelen];
  struct l_iovec liov[lmsg.msg_iovlen];
  if (copy_from_user(liov, lmsg.msg_iov, sizeof liov)) {
    return -LINUX_EFAULT;
  }
  struct iovec msg_iov[lmsg.msg_iovlen];
  size_t iov_total_len = 0;
  for (size_t i = 0; i < lmsg.msg_iovlen; ++i) {
    iov_total_len += liov[i].iov_len;
  }
  char iov_buf[iov_total_len], *iov_buf_ptr = iov_buf;
  for (size_t i = 0; i < lmsg.msg_iovlen; ++i) {
    msg_iov[i].iov_base = iov_buf_ptr;
    msg_iov[i].iov_len = liov[i].iov_len;
    iov_buf_ptr += liov[i].iov_len;
  }
  char msg_control[lmsg.msg_controllen];
  struct msghdr dmsg;
  dmsg.msg_namelen = lmsg.msg_namelen;
  dmsg.msg_name = lmsg.msg_name == 0 ? 0 : msg_name;
  dmsg.msg_iov = msg_iov;
  dmsg.msg_iovlen = lmsg.msg_iovlen;
  dmsg.msg_control = msg_control;
  dmsg.msg_controllen = lmsg.msg_controllen;
  dmsg.msg_flags = linux_to_darwin_msg_flags(lmsg.msg_flags);
  int r = syswrap(recvmsg(sockfd, &dmsg, flags));
  if (r < 0) {
    return r;
  }
  if (lmsg.msg_name != 0) {
    if (copy_to_user(lmsg.msg_name, dmsg.msg_name, dmsg.msg_namelen))
      return -LINUX_EFAULT;
  }
  if (copy_to_user(msg_ptr + offsetof(struct l_msghdr, msg_namelen), &dmsg.msg_namelen, sizeof dmsg.msg_namelen))
    return -LINUX_EFAULT;
  for (size_t i = 0; i < lmsg.msg_iovlen; ++i) {
    if (copy_to_user(liov[i].iov_base, dmsg.msg_iov[i].iov_base, liov[i].iov_len))
      return -LINUX_EFAULT;
  }
  if (copy_to_user(lmsg.msg_control, dmsg.msg_control, lmsg.msg_controllen))
    return -LINUX_EFAULT;
  if (copy_to_user(msg_ptr + offsetof(struct l_msghdr, msg_controllen), &dmsg.msg_controllen, sizeof dmsg.msg_controllen))
    return -LINUX_EFAULT;
  return r;
}

DEFINE_SYSCALL(listen, int, socket, int, backlog)
{
  return syswrap(listen(socket, backlog));
}

DEFINE_SYSCALL(accept, int, sockfd, gaddr_t, addr_ptr, gaddr_t, addrlen_ptr)
{
  socklen_t *socklen_ptr = NULL;
  struct sockaddr *sock_ptr = NULL;
  if (addr_ptr != 0) {
    l_socklen_t addrbuflen;
    if (copy_from_user(&addrbuflen, addrlen_ptr, sizeof addrbuflen))
      return -LINUX_EFAULT;
    socklen_ptr = alloca(sizeof *socklen_ptr);
    *socklen_ptr = addrbuflen;
    sock_ptr = alloca(addrbuflen);
  }
  int ret = syswrap(accept(sockfd, sock_ptr, socklen_ptr));
  if (ret < 0) {
    return ret;
  }
  if (addr_ptr != 0) {
    char addr[sock_ptr->sa_len];
    darwin_to_linux_sockaddr((struct l_sockaddr *) addr, sock_ptr);
    if (copy_to_user(addr_ptr, addr, sizeof addr))
      return -LINUX_EFAULT;
    if (copy_to_user(addrlen_ptr, socklen_ptr, sizeof *socklen_ptr))
      return -LINUX_EFAULT;
  }
  vfs_expose_darwinfs_fd(ret);
  return ret;
}

DEFINE_SYSCALL(bind, int, sockfd, gaddr_t, addr_ptr, int, addrlen)
{
  struct sockaddr *sockaddr;
  char addr[addrlen];
  if (copy_from_user(addr, addr_ptr, addrlen))
    return -LINUX_EFAULT;

  if (linux_to_darwin_sockaddr(&sockaddr, (struct l_sockaddr *) addr, addrlen) < 0) {
    return -LINUX_EINVAL;
  }
  int ret = syswrap(bind(sockfd, sockaddr, addrlen));

  free(sockaddr);
  return ret;
}

DEFINE_SYSCALL(getsockname, int, sockfd, gaddr_t, addr_ptr, gaddr_t, addrlen_ptr)
{
  socklen_t *socklen_ptr = NULL;
  struct sockaddr *sock_ptr = NULL;
  if (addr_ptr != 0) {
    l_socklen_t addrbuflen;
    if (copy_from_user(&addrbuflen, addrlen_ptr, sizeof addrbuflen))
      return -LINUX_EFAULT;
    socklen_ptr = alloca(sizeof *socklen_ptr);
    *socklen_ptr = addrbuflen;
    sock_ptr = alloca(addrbuflen);
  }
  int ret = syswrap(getsockname(sockfd, sock_ptr, socklen_ptr));
  if (ret < 0) {
    return ret;
  }
  if (addr_ptr != 0) {
    char addr[sock_ptr->sa_len];
    darwin_to_linux_sockaddr((struct l_sockaddr *) addr, sock_ptr);
    if (copy_to_user(addr_ptr, addr, sizeof addr))
      return -LINUX_EFAULT;
    if (copy_to_user(addrlen_ptr, socklen_ptr, sizeof *socklen_ptr))
      return -LINUX_EFAULT;
  }
  return ret;
}

DEFINE_SYSCALL(getpeername, int, sockfd, gaddr_t, addr_ptr, gaddr_t, addrlen_ptr)
{
  socklen_t *socklen_ptr = NULL;
  struct sockaddr *sock_ptr = NULL;
  if (addr_ptr != 0) {
    l_socklen_t addrbuflen;
    if (copy_from_user(&addrbuflen, addrlen_ptr, sizeof addrbuflen))
      return -LINUX_EFAULT;
    socklen_ptr = alloca(sizeof *socklen_ptr);
    *socklen_ptr = addrbuflen;
    sock_ptr = alloca(addrbuflen);
  }
  int ret = syswrap(getpeername(sockfd, sock_ptr, socklen_ptr));
  if (ret < 0) {
    return ret;
  }
  if (addr_ptr != 0) {
    char addr[sock_ptr->sa_len];
    darwin_to_linux_sockaddr((struct l_sockaddr *) addr, sock_ptr);
    if (copy_to_user(addr_ptr, addr, sizeof addr))
      return -LINUX_EFAULT;
    if (copy_to_user(addrlen_ptr, socklen_ptr, sizeof *socklen_ptr))
      return -LINUX_EFAULT;
  }
  return ret;
}

DEFINE_SYSCALL(socketpair, int, family, int, type, int, protocol, gaddr_t, usockvec_ptr)
{
  int fds[2];
  int r = syswrap(socketpair(linux_to_darwin_sa_family(family), type, protocol, fds));
  if (r < 0)
    return r;
  if (copy_to_user(usockvec_ptr, fds, sizeof fds))
    return -LINUX_EFAULT;
  vfs_expose_darwinfs_fd(fds[0]);
  vfs_expose_darwinfs_fd(fds[1]);
  return r;
}
