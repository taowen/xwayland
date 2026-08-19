#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/Xfuncproto.h>

/* ErrorF is used by xtrans */
#ifndef HAVE_DIX_CONFIG_H
extern _X_EXPORT void
ErrorF(const char *f, ...)
_X_ATTRIBUTE_PRINTF(1, 2);
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/socket.h>

/* Zygote seccomp blocks accept(2). accept4 is allowed and is what
 * bionic itself uses. xtrans SocketUNIXAccept otherwise returns the
 * listen fd when SIGSYS leaves x0 unchanged. */
#define accept(fd, addr, len) accept4((fd), (addr), (len), 0)

#define TRANS_REOPEN
#define TRANS_SERVER
#define XSERV_t
#include <X11/Xtrans/transport.c>
