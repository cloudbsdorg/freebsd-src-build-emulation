/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Emulation Framework Project
 * All rights reserved.
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

#ifndef _EMU_AUDIT_H_
#define _EMU_AUDIT_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/uio.h>

/*
 * Audit event types
 */
typedef enum emu_audit_event {
	EMU_AUDIT_EVENT_NONE = 0,
	EMU_AUDIT_EVENT_INSTANCE_CREATE,
	EMU_AUDIT_EVENT_INSTANCE_DESTROY,
	EMU_AUDIT_EVENT_INSTANCE_START,
	EMU_AUDIT_EVENT_INSTANCE_STOP,
	EMU_AUDIT_EVENT_INSTANCE_PAUSE,
	EMU_AUDIT_EVENT_INSTANCE_RESUME,
	EMU_AUDIT_EVENT_MEMORY_ALLOC,
	EMU_AUDIT_EVENT_MEMORY_FREE,
	EMU_AUDIT_EVENT_PERMISSION_DENIED,
	EMU_AUDIT_EVENT_SECURELEVEL_VIOLATION,
	EMU_AUDIT_EVENT_MAC_VIOLATION,
	EMU_AUDIT_EVENT_PTRACE_ATTACH,
	EMU_AUDIT_EVENT_PTRACE_DETACH,
	EMU_AUDIT_EVENT_AUDIT_ACCESS,
	EMU_AUDIT_EVENT_SHARE_MOUNT,
	EMU_AUDIT_EVENT_SHARE_UNMOUNT,
	EMU_AUDIT_EVENT_SNAPSHOT_CREATE,
	EMU_AUDIT_EVENT_SNAPSHOT_DELETE,
	EMU_AUDIT_EVENT_MMIO_VIOLATION,
	EMU_AUDIT_EVENT_SHARE_PATH_INVALID,
	EMU_AUDIT_EVENT_MAX
} emu_audit_event_t;

/*
 * Audit severity levels
 */
typedef enum emu_audit_severity {
	EMU_AUDIT_SEVERITY_EMERG = 0,
	EMU_AUDIT_SEVERITY_ALERT = 1,
	EMU_AUDIT_SEVERITY_CRIT = 2,
	EMU_AUDIT_SEVERITY_ERR = 3,
	EMU_AUDIT_SEVERITY_WARNING = 4,
	EMU_AUDIT_SEVERITY_NOTICE = 5,
	EMU_AUDIT_SEVERITY_INFO = 6,
	EMU_AUDIT_SEVERITY_DEBUG = 7
} emu_audit_severity_t;

/*
 * Audit subsystem state
 */
struct emu_audit_state {
	int		as_initialized;
	int		as_enabled;
	int		as_min_severity;
	uint64_t	as_event_count;
};

/*
 * Audit log limits
 */
#define	EMU_AUDIT_MAX_MSG_LEN	256
#define	EMU_AUDIT_MAX_DATA_LEN	64

/*
 * External event name strings
 */
extern const char *emu_audit_event_names[];

/*
 * Initialize audit subsystem
 */
int emu_audit_init(void);

/*
 * Destroy audit subsystem
 */
void emu_audit_destroy(void);

/*
 * Log an audit event
 */
int emu_audit_log(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const char *fmt, ...);

/*
 * Log an audit event with binary data
 */
int emu_audit_log_data(emu_audit_event_t event, emu_audit_severity_t severity,
    const char *instance, const void *data, size_t datalen, const char *fmt, ...);

/*
 * Read audit log
 */
int emu_audit_read_log(struct uio *uio);

/*
 * Audit convenience macros
 */
#define	AUDIT_INSTANCE_CREATE(inst) \
	emu_audit_log(EMU_AUDIT_EVENT_INSTANCE_CREATE, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Instance created")
#define	AUDIT_INSTANCE_DESTROY(inst) \
	emu_audit_log(EMU_AUDIT_EVENT_INSTANCE_DESTROY, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Instance destroyed")
#define	AUDIT_INSTANCE_START(inst) \
	emu_audit_log(EMU_AUDIT_EVENT_INSTANCE_START, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Instance started")
#define	AUDIT_INSTANCE_STOP(inst) \
	emu_audit_log(EMU_AUDIT_EVENT_INSTANCE_STOP, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Instance stopped")
#define	AUDIT_PERM_DENIED(op, reason) \
	emu_audit_log(EMU_AUDIT_EVENT_PERMISSION_DENIED, EMU_AUDIT_SEVERITY_WARNING, \
	    NULL, "Permission denied: %s - %s", op, reason)
#define	AUDIT_SECURELEVEL(op) \
	emu_audit_log(EMU_AUDIT_EVENT_SECURELEVEL_VIOLATION, EMU_AUDIT_SEVERITY_ERR, \
	    NULL, "Securelevel violation: %s", op)
#define	AUDIT_AUDIT_LOG_ACCESS(op) \
	emu_audit_log(EMU_AUDIT_EVENT_AUDIT_ACCESS, EMU_AUDIT_SEVERITY_INFO, \
	    NULL, "Audit log accessed: %s", op)
#define	AUDIT_SHARE_ACCESS(inst, path) \
	emu_audit_log(EMU_AUDIT_EVENT_SHARE_MOUNT, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Share access: %s", path)
#define	AUDIT_SNAPSHOT_ACCESS(inst, name) \
	emu_audit_log(EMU_AUDIT_EVENT_SNAPSHOT_CREATE, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Snapshot access: %s", name)

#endif /* _KERNEL */

#endif /* !_EMU_AUDIT_H_ */
