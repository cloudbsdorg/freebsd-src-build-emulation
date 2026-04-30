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
 * Compatibility stubs for FreeBSD procctl header.
 * These provide minimal definitions for building on non-FreeBSD systems.
 */

#ifndef _SYS_PROCCTL_COMPAT_H_
#define _SYS_PROCCTL_COMPAT_H_

/*
 * Only include system procctl header on native FreeBSD.
 * In cross-compilation environments, use stubs.
 */
#if defined(__FreeBSD__) && !defined(CROSS_COMPILING)
#include <sys/procctl.h>
#else

/* procctl() commands */
#ifndef PROC_PROCCTL_MDVALIDATE
#define PROC_PROCCTL_MDVALIDATE		0x10000000
#define PROC_TRACE_CTL		0x01
#define PROC_COREDUMP_CTL	0x02
#define PROC_REAP		0x03
#endif

/* Process ID type for procctl */
#ifndef P_PID
#define P_PID	1
#endif

/* procctl() subcommands */
#ifndef PROC_SCE_VAL
#define PROC_SCE_VAL		0
#define PROC_SCE_EXEC		1
#define PROC_SCE_LOG		2
#define PROC_SCE_ELFCA		3
#define PROC_SCE_NOREVOKE	4
#define PROC_SCE_DISABLE	5
#define PROC_SCE_ENABLE		6
#define PROC_SCE_PROC		7
#define PROC_SCE_ARGC		8
#define PROC_SCE_ARGV		9
#define PROC_SCE_ENVC		10
#define PROC_SCE_ENVP		11
#define PROC_SCE_STATUS		12
#define PROC_COREDUMP_DISABLE	0
#define PROC_COREDUMP_ENABLE	1
#endif

/* Signal numbers for sandboxing */
#ifndef SIGKILL
#define SIGKILL	9
#endif

/* Stub procctl() function */
#ifndef procctl
static inline int procctl(int type, id_t id, int cmd, void *arg) {
	(void)type;
	(void)id;
	(void)cmd;
	(void)arg;
	return (-1);
}
#endif

#endif /* !(__FreeBSD__ && !CROSS_COMPILING) */

#endif /* !_SYS_PROCCTL_COMPAT_H_ */
