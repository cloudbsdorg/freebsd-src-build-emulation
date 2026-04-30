/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Compatibility stubs for FreeBSD Capsicum headers.
 * These provide minimal definitions for building on non-FreeBSD systems.
 * 
 * On FreeBSD, we include the real headers.
 * On other systems, we provide stubs.
 */

#ifndef _SYS_CAPSICUM_COMPAT_H_
#define _SYS_CAPSICUM_COMPAT_H_

/*
 * Capsicum compatibility layer.
 * 
 * If the system has Capsicum support (via sys/types.h including capsicum.h),
 * we use the native definitions. Otherwise, we provide stubs.
 */

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>

/* Capsicum capability rights - provide definitions if not already defined */
#ifndef CAP_READ
#define CAP_READ            0x00000001
#define CAP_WRITE           0x00000002
#define CAP_SEEK            0x00000004
#define CAP_FCNTL           0x00000008
#define CAP_FSTAT           0x00000010
#define CAP_FLOCK           0x00000020
#define CAP_FSYNC           0x00000040
#define CAP_FTRUNC          0x00000080
#define CAP_IOCTL           0x00000100
#define CAP_TTYSTATE        0x00000200
#define CAP_PDGETPID        0x00000400
#define CAP_PDWAIT          0x00000800
#define CAP_PDKILL          0x00001000
#define CAP_NETWORK         0x00002000
#define CAP_BIND            0x00004000
#define CAP_CONNECT         0x00008000
#define CAP_ACCEPT         0x00010000
#define CAP_LISTEN          0x00020000
#define CAP_GETSOCKNAME     0x00040000
#define CAP_GETPEERNAME     0x00080000
#define CAP_SHUTDOWN        0x00100000
#define CAP_SEND            0x00200000
#define CAP_RECEIVE         0x00400000
#define CAP_MAC_LABEL       0x00800000
#define CAP_MAP_ANON        0x01000000
#define CAP_MMAP           0x02000000
#define CAP_MPROTECT       0x04000000
#define CAP_MUNMAP         0x04000000
#define CAP_CREATE          0x08000000
#define CAP_DESTROY         0x10000000
#define CAP_REAPER          0x20000000
#define CAP_SETTIME         0x40000000
#define CAP_ALL             0x7FFFFFFF
#endif

/* Capability mode for cap_enter() - provide full definition if needed */
#ifndef _CAP_RIGHTS_COMPLETE
#ifndef _CAP_RIGHTS_T_DECLARED
#ifndef cap_rights_t
typedef unsigned int cap_rights_t;
#endif
#else
/* _CAP_RIGHTS_T_DECLARED is set but struct might be incomplete.
 * Provide our own complete definition. */
#ifndef _CAP_RIGHTS_STRUCT_DEFINED
struct cap_rights {
	uint64_t cr_rights[2];
};
#define _CAP_RIGHTS_STRUCT_DEFINED
#endif
#endif
#define _CAP_RIGHTS_COMPLETE
#endif

/* File descriptor flags for Capsicum */
#ifndef CAP_FDFLAGS_CLOEXEC
#define CAP_FDFLAGS_CLOEXEC 0x01
#endif

/* Proc control commands for procctl() */
#ifndef PROC_TRACE_CTL_DISABLE
#define PROC_TRACE_CTL_DISABLE 0
#define PROC_TRACE_CTL_ENABLE  1
#endif

/* Stub capability type */
#ifndef cap_channel_t
typedef int cap_channel_t;
#endif

/* Stub function declarations */
static inline int cap_enter(void) { return (-1); }
static inline int cap_rights_limit(int fd, const cap_rights_t *rights) { (void)fd; (void)rights; return (-1); }
static inline int cap_rights_get(int fd, cap_rights_t *rights) { (void)fd; (void)rights; return (-1); }
static inline int cap_new(int fd, unsigned long rights) { (void)fd; (void)rights; return (-1); }
static inline int cap_getrights(int fd, cap_rights_t *rights) { (void)fd; (void)rights; return (-1); }
static inline int cap_getmode(unsigned int *modep) { (void)modep; return (-1); }
static inline int cap_setmode(unsigned int mode) { (void)mode; return (-1); }
static inline cap_channel_t *cap_init(void) { return (NULL); }
static inline cap_channel_t *cap_open(const char *path, int flags) { (void)path; (void)flags; return (NULL); }
static inline int cap_closeonexec(int fd) { (void)fd; return (-1); }
static inline int cap_fcntls_limit(int fd, unsigned int fcntlrights) { (void)fd; (void)fcntlrights; return (-1); }
static inline int cap_fcntls_get(int fd, unsigned int *fcntlrightsp) { (void)fd; (void)fcntlrightsp; return (-1); }
static inline int cap_ioctls_limit(int fd, const unsigned long *cmds, size_t ncmds) { (void)fd; (void)cmds; (void)ncmds; return (-1); }
static inline ssize_t cap_ioctls_get(int fd, unsigned long *cmds, size_t maxcmds) { (void)fd; (void)cmds; (void)maxcmds; return (-1); }
static inline int cap_rights_init(cap_rights_t *rightsp, ...) { (void)rightsp; return (0); }
static inline int cap_rights_set(cap_rights_t *rightsp, ...) { (void)rightsp; return (0); }
static inline int cap_rights_clear(cap_rights_t *rightsp, ...) { (void)rightsp; return (0); }
static inline bool cap_rights_is_set(const cap_rights_t *rightsp, ...) { (void)rightsp; return (true); }
static inline bool cap_sandboxed(void) { return (false); }

#endif /* !_SYS_CAPSICUM_COMPAT_H_ */
