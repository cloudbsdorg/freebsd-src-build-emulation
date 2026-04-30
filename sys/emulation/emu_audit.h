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
 *
 * $FreeBSD$
 */

#ifndef _EMULATION_EMU_AUDIT_H_
#define	_EMULATION_EMU_AUDIT_H_

#include <sys/types.h>
#include <sys/time.h>
#include <sys/priv.h>
#include <sys/syslog.h>

/*
 * Emulation Framework Audit Logging Subsystem
 * 
 * Provides comprehensive audit logging for all security-relevant events
 * in the emulation framework. Supports multiple output destinations
 * (syslog, file) with configurable rotation and filtering.
 */

/* Maximum audit event message length */
#define	EMU_AUDIT_MAX_MSG_LEN		256

/* Maximum audit context data size */
#define	EMU_AUDIT_MAX_DATA_LEN		512

/* Maximum number of concurrent audit writers */
#define	EMU_AUDIT_MAX_WRITERS		4

/* Audit file rotation defaults */
#define	EMU_AUDIT_DEFAULT_ROTATION_SIZE	(10 * 1024 * 1024)	/* 10 MB */
#define	EMU_AUDIT_DEFAULT_ROTATION_COUNT	5

/* Audit event types - comprehensive coverage of security events */
typedef enum {
	/* Instance lifecycle events */
	EMU_AUDIT_EVENT_INSTANCE_CREATE = 1,	/* Instance created */
	EMU_AUDIT_EVENT_INSTANCE_DESTROY,	/* Instance destroyed */
	EMU_AUDIT_EVENT_INSTANCE_START,		/* Instance started */
	EMU_AUDIT_EVENT_INSTANCE_STOP,		/* Instance stopped */
	EMU_AUDIT_EVENT_INSTANCE_PAUSE,		/* Instance paused */
	EMU_AUDIT_EVENT_INSTANCE_RESUME,	/* Instance resumed */
	
	/* Access control events */
	EMU_AUDIT_EVENT_PERM_DENIED,		/* Permission denied */
	EMU_AUDIT_EVENT_PERM_GRANTED,		/* Permission granted (admin override) */
	EMU_AUDIT_EVENT_PRIV_CHECK_FAILED,	/* Privilege check failed */
	EMU_AUDIT_EVENT_OWNERSHIP_VIOLATION,	/* Instance ownership violation */
	EMU_AUDIT_EVENT_GROUP_DELEGATION,	/* GID_EMU group delegation used */
	
	/* Resource limit events */
	EMU_AUDIT_EVENT_RESOURCE_LIMIT_HIT,	/* Resource limit exceeded */
	EMU_AUDIT_EVENT_OVERCOMMIT_WARNING,	/* Memory overcommit warning */
	EMU_AUDIT_EVENT_MEMORY_ALLOC_FAILED,	/* Memory allocation failed */
	EMU_AUDIT_EVENT_INSTANCE_LIMIT_HIT,	/* Instance count limit hit */
	
	/* Filesystem sharing events */
	EMU_AUDIT_EVENT_SHARE_MOUNT,		/* Filesystem share mounted */
	EMU_AUDIT_EVENT_SHARE_UNMOUNT,		/* Filesystem share unmounted */
	EMU_AUDIT_EVENT_SHARE_ACCESS_DENIED,	/* Share access denied */
	EMU_AUDIT_EVENT_SHARE_PATH_INVALID,	/* Invalid share path rejected */
	EMU_AUDIT_EVENT_SHARE_TOCTOU_DETECTED,	/* TOCTOU attack detected */
	
	/* Snapshot events */
	EMU_AUDIT_EVENT_SNAPSHOT_CREATE,	/* Snapshot created */
	EMU_AUDIT_EVENT_SNAPSHOT_ROLLBACK,	/* Snapshot rollback performed */
	EMU_AUDIT_EVENT_SNAPSHOT_DESTROY,	/* Snapshot destroyed */
	
	/* Network events */
	EMU_AUDIT_EVENT_NETWORK_MODE_CHANGE,	/* Network mode changed */
	EMU_AUDIT_EVENT_NETWORK_VIOLATION,	/* Network policy violation */
	EMU_AUDIT_EVENT_GDB_CONNECTION,		/* GDB stub connection */
	EMU_AUDIT_EVENT_GDB_AUTH_FAILED,	/* GDB authentication failed */
	
	/* Device events */
	EMU_AUDIT_EVENT_MMIO_VIOLATION,		/* MMIO access violation */
	EMU_AUDIT_EVENT_DEVICE_ERROR,		/* Device emulation error */
	EMU_AUDIT_EVENT_DEVICE_RESET,		/* Device reset triggered */
	
	/* Crash and error events */
	EMU_AUDIT_EVENT_CRASH_DETECTED,		/* Guest crash detected */
	EMU_AUDIT_EVENT_CRASH_CONTAINED,	/* Crash successfully contained */
	EMU_AUDIT_EVENT_CRASH_PROPAGATED,	/* Crash propagated (severity: CRITICAL) */
	EMU_AUDIT_EVENT_WATCHDOG_TIMEOUT,	/* Watchdog timeout triggered */
	EMU_AUDIT_EVENT_TRIPLE_FAULT,		/* Triple fault occurred */
	
	/* Sandbox and isolation events */
	EMU_AUDIT_EVENT_CAPSICUM_ENTER,		/* Entered Capsicum capability mode */
	EMU_AUDIT_EVENT_CAPSICUM_VIOLATION,	/* Capsicum capability violation */
	EMU_AUDIT_EVENT_CAPSICUM_FAILED,	/* Failed to enter sandbox */
	EMU_AUDIT_EVENT_JAIL_RESTRICTION,	/* Jail restriction applied */
	
	/* Module events */
	EMU_AUDIT_EVENT_MODULE_LOAD,		/* Kernel module loaded */
	EMU_AUDIT_EVENT_MODULE_UNLOAD,		/* Kernel module unloaded */
	EMU_AUDIT_EVENT_MODULE_UNLOAD_DENIED,	/* Module unload denied (active instances) */
	
	/* Configuration events */
	EMU_AUDIT_EVENT_CONFIG_CHANGE,		/* Configuration changed */
	EMU_AUDIT_EVENT_SYSCTL_CHANGE,		/* Security sysctl changed */
	EMU_AUDIT_EVENT_LIMIT_CHANGE,		/* Resource limit changed */
	
	/* Security audit events */
	EMU_AUDIT_EVENT_AUDIT_LOG_ACCESS,	/* Audit log accessed */
	EMU_AUDIT_EVENT_AUDIT_CONFIG_CHANGE,	/* Audit configuration changed */
	
	/* Supply chain events */
	EMU_AUDIT_EVENT_FIRMWARE_VERIFY,	/* Firmware verification */
	EMU_AUDIT_EVENT_FIRMWARE_VERIFY_FAILED,	/* Firmware verification failed */
	EMU_AUDIT_EVENT_BINARY_INTEGRITY,	/* Binary integrity check */
	
	/* Miscellaneous */
	EMU_AUDIT_EVENT_UNKNOWN,		/* Unknown/unclassified event */
	EMU_AUDIT_EVENT_MAX			/* Sentinel value */
} emu_audit_event_t;

/* Audit severity levels - aligned with syslog priorities */
typedef enum {
	EMU_AUDIT_SEVERITY_EMERG = 0,		/* System is unusable */
	EMU_AUDIT_SEVERITY_ALERT = 1,		/* Action must be taken immediately */
	EMU_AUDIT_SEVERITY_CRIT = 2,		/* Critical conditions */
	EMU_AUDIT_SEVERITY_ERR = 3,		/* Error conditions */
	EMU_AUDIT_SEVERITY_WARNING = 4,		/* Warning conditions */
	EMU_AUDIT_SEVERITY_NOTICE = 5,		/* Normal but significant */
	EMU_AUDIT_SEVERITY_INFO = 6,		/* Informational */
	EMU_AUDIT_SEVERITY_DEBUG = 7		/* Debug-level messages */
} emu_audit_severity_t;

/* Audit output destinations */
typedef enum {
	EMU_AUDIT_DEST_NONE = 0,		/* Audit logging disabled */
	EMU_AUDIT_DEST_SYSLOG = 1,		/* Syslog only */
	EMU_AUDIT_DEST_FILE = 2,		/* File only */
	EMU_AUDIT_DEST_BOTH = 3		/* Both syslog and file */
} emu_audit_destination_t;

/* Audit event record structure */
struct emu_audit_record {
	emu_audit_event_t		ar_event;	/* Event type */
	emu_audit_severity_t		ar_severity;	/* Severity level */
	struct timeval			ar_timestamp;	/* Event timestamp */
	pid_t				ar_pid;		/* Process ID */
	pid_t				ar_tid;		/* Thread ID */
	uid_t				ar_uid;		/* User ID */
	gid_t				ar_gid;		/* Group ID */
	char				ar_instance[64];	/* Instance name/ID */
	char				ar_message[EMU_AUDIT_MAX_MSG_LEN]; /* Event message */
	char				ar_data[EMU_AUDIT_MAX_DATA_LEN];	/* Additional context data */
	int				ar_error;	/* Associated error code (errno) */
	int				ar_privilege;	/* Privilege involved (if any) */
};

/* Audit configuration structure */
struct emu_audit_config {
	int				ac_enabled;	/* Audit logging enabled */
	emu_audit_destination_t		ac_destination;	/* Output destination */
	char				ac_file_path[256]; /* Log file path */
	size_t			ac_rotation_size;	/* Rotation size (bytes) */
	int				ac_rotation_count;	/* Number of rotated files */
	int				ac_min_severity;	/* Minimum severity to log */
	int				ac_include_data;	/* Include context data */
};

/* Audit writer state */
struct emu_audit_writer {
	struct emu_audit_config		aw_config;	/* Writer configuration */
	struct vnode			*aw_vp;		/* Log file vnode */
	struct file			*aw_fp;		/* Log file pointer */
	struct mtx			aw_mtx;		/* Writer mutex */
	int				aw_initialized;	/* Writer initialized */
};

/* Global audit state */
struct emu_audit_state {
	struct emu_audit_config		as_config;	/* Current configuration */
	struct emu_audit_writer		as_writer;	/* Primary writer */
	struct mtx			as_mtx;		/* State mutex */
	int				as_initialized;	/* Subsystem initialized */
	int				as_event_count;	/* Total events logged */
};

/*
 * Audit event type to string mapping
 */
static const char *emu_audit_event_names[] = {
	[EMU_AUDIT_EVENT_INSTANCE_CREATE] = "INSTANCE_CREATE",
	[EMU_AUDIT_EVENT_INSTANCE_DESTROY] = "INSTANCE_DESTROY",
	[EMU_AUDIT_EVENT_INSTANCE_START] = "INSTANCE_START",
	[EMU_AUDIT_EVENT_INSTANCE_STOP] = "INSTANCE_STOP",
	[EMU_AUDIT_EVENT_INSTANCE_PAUSE] = "INSTANCE_PAUSE",
	[EMU_AUDIT_EVENT_INSTANCE_RESUME] = "INSTANCE_RESUME",
	[EMU_AUDIT_EVENT_PERM_DENIED] = "PERM_DENIED",
	[EMU_AUDIT_EVENT_PERM_GRANTED] = "PERM_GRANTED",
	[EMU_AUDIT_EVENT_PRIV_CHECK_FAILED] = "PRIV_CHECK_FAILED",
	[EMU_AUDIT_EVENT_OWNERSHIP_VIOLATION] = "OWNERSHIP_VIOLATION",
	[EMU_AUDIT_EVENT_GROUP_DELEGATION] = "GROUP_DELEGATION",
	[EMU_AUDIT_EVENT_RESOURCE_LIMIT_HIT] = "RESOURCE_LIMIT_HIT",
	[EMU_AUDIT_EVENT_OVERCOMMIT_WARNING] = "OVERCOMMIT_WARNING",
	[EMU_AUDIT_EVENT_MEMORY_ALLOC_FAILED] = "MEMORY_ALLOC_FAILED",
	[EMU_AUDIT_EVENT_INSTANCE_LIMIT_HIT] = "INSTANCE_LIMIT_HIT",
	[EMU_AUDIT_EVENT_SHARE_MOUNT] = "SHARE_MOUNT",
	[EMU_AUDIT_EVENT_SHARE_UNMOUNT] = "SHARE_UNMOUNT",
	[EMU_AUDIT_EVENT_SHARE_ACCESS_DENIED] = "SHARE_ACCESS_DENIED",
	[EMU_AUDIT_EVENT_SHARE_PATH_INVALID] = "SHARE_PATH_INVALID",
	[EMU_AUDIT_EVENT_SHARE_TOCTOU_DETECTED] = "SHARE_TOCTOU_DETECTED",
	[EMU_AUDIT_EVENT_SNAPSHOT_CREATE] = "SNAPSHOT_CREATE",
	[EMU_AUDIT_EVENT_SNAPSHOT_ROLLBACK] = "SNAPSHOT_ROLLBACK",
	[EMU_AUDIT_EVENT_SNAPSHOT_DESTROY] = "SNAPSHOT_DESTROY",
	[EMU_AUDIT_EVENT_NETWORK_MODE_CHANGE] = "NETWORK_MODE_CHANGE",
	[EMU_AUDIT_EVENT_NETWORK_VIOLATION] = "NETWORK_VIOLATION",
	[EMU_AUDIT_EVENT_GDB_CONNECTION] = "GDB_CONNECTION",
	[EMU_AUDIT_EVENT_GDB_AUTH_FAILED] = "GDB_AUTH_FAILED",
	[EMU_AUDIT_EVENT_MMIO_VIOLATION] = "MMIO_VIOLATION",
	[EMU_AUDIT_EVENT_DEVICE_ERROR] = "DEVICE_ERROR",
	[EMU_AUDIT_EVENT_DEVICE_RESET] = "DEVICE_RESET",
	[EMU_AUDIT_EVENT_CRASH_DETECTED] = "CRASH_DETECTED",
	[EMU_AUDIT_EVENT_CRASH_CONTAINED] = "CRASH_CONTAINED",
	[EMU_AUDIT_EVENT_CRASH_PROPAGATED] = "CRASH_PROPAGATED",
	[EMU_AUDIT_EVENT_WATCHDOG_TIMEOUT] = "WATCHDOG_TIMEOUT",
	[EMU_AUDIT_EVENT_TRIPLE_FAULT] = "TRIPLE_FAULT",
	[EMU_AUDIT_EVENT_CAPSICUM_ENTER] = "CAPSICUM_ENTER",
	[EMU_AUDIT_EVENT_CAPSICUM_VIOLATION] = "CAPSICUM_VIOLATION",
	[EMU_AUDIT_EVENT_CAPSICUM_FAILED] = "CAPSICUM_FAILED",
	[EMU_AUDIT_EVENT_JAIL_RESTRICTION] = "JAIL_RESTRICTION",
	[EMU_AUDIT_EVENT_MODULE_LOAD] = "MODULE_LOAD",
	[EMU_AUDIT_EVENT_MODULE_UNLOAD] = "MODULE_UNLOAD",
	[EMU_AUDIT_EVENT_MODULE_UNLOAD_DENIED] = "MODULE_UNLOAD_DENIED",
	[EMU_AUDIT_EVENT_CONFIG_CHANGE] = "CONFIG_CHANGE",
	[EMU_AUDIT_EVENT_SYSCTL_CHANGE] = "SYSCTL_CHANGE",
	[EMU_AUDIT_EVENT_LIMIT_CHANGE] = "LIMIT_CHANGE",
	[EMU_AUDIT_EVENT_AUDIT_LOG_ACCESS] = "AUDIT_LOG_ACCESS",
	[EMU_AUDIT_EVENT_AUDIT_CONFIG_CHANGE] = "AUDIT_CONFIG_CHANGE",
	[EMU_AUDIT_EVENT_FIRMWARE_VERIFY] = "FIRMWARE_VERIFY",
	[EMU_AUDIT_EVENT_FIRMWARE_VERIFY_FAILED] = "FIRMWARE_VERIFY_FAILED",
	[EMU_AUDIT_EVENT_BINARY_INTEGRITY] = "BINARY_INTEGRITY",
	[EMU_AUDIT_EVENT_UNKNOWN] = "UNKNOWN"
};

/*
 * Audit severity level to syslog priority mapping
 */
static const int emu_audit_severity_syslog[] = {
	[EMU_AUDIT_SEVERITY_EMERG] = LOG_EMERG,
	[EMU_AUDIT_SEVERITY_ALERT] = LOG_ALERT,
	[EMU_AUDIT_SEVERITY_CRIT] = LOG_CRIT,
	[EMU_AUDIT_SEVERITY_ERR] = LOG_ERR,
	[EMU_AUDIT_SEVERITY_WARNING] = LOG_WARNING,
	[EMU_AUDIT_SEVERITY_NOTICE] = LOG_NOTICE,
	[EMU_AUDIT_SEVERITY_INFO] = LOG_INFO,
	[EMU_AUDIT_SEVERITY_DEBUG] = LOG_DEBUG
};

#ifdef _KERNEL

/*
 * Function prototypes
 */

/* Initialization and cleanup */
int	emu_audit_init(void);
void	emu_audit_destroy(void);

/* Event logging */
int	emu_audit_log(emu_audit_event_t event, emu_audit_severity_t severity,
		    const char *instance, const char *fmt, ...)
		    __attribute__((__format__(__printf__, 4, 5)));
int	emu_audit_log_data(emu_audit_event_t event, emu_audit_severity_t severity,
			   const char *instance, const char *data,
			   const char *fmt, ...)
			   __attribute__((__format__(__printf__, 5, 6)));

/* Convenience macros for common events */
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
#define	AUDIT_PERM_DENIED(inst, reason) \
	emu_audit_log(EMU_AUDIT_EVENT_PERM_DENIED, EMU_AUDIT_SEVERITY_WARNING, \
	    inst, "Permission denied: %s", reason)
#define	AUDIT_PRIV_CHECK_FAILED(inst, priv) \
	emu_audit_log(EMU_AUDIT_EVENT_PRIV_CHECK_FAILED, EMU_AUDIT_SEVERITY_WARNING, \
	    inst, "Privilege check failed: %d", priv)
#define	AUDIT_RESOURCE_LIMIT_HIT(inst, resource) \
	emu_audit_log(EMU_AUDIT_EVENT_RESOURCE_LIMIT_HIT, EMU_AUDIT_SEVERITY_WARNING, \
	    inst, "Resource limit hit: %s", resource)
#define	AUDIT_OVERCOMMIT_WARNING(inst, details) \
	emu_audit_log(EMU_AUDIT_EVENT_OVERCOMMIT_WARNING, EMU_AUDIT_SEVERITY_WARNING, \
	    inst, "Memory overcommit warning: %s", details)
#define	AUDIT_CRASH_DETECTED(inst, type) \
	emu_audit_log(EMU_AUDIT_EVENT_CRASH_DETECTED, EMU_AUDIT_SEVERITY_CRIT, \
	    inst, "Crash detected: %s", type)
#define	AUDIT_CAPSICUM_VIOLATION(inst, operation) \
	emu_audit_log(EMU_AUDIT_EVENT_CAPSICUM_VIOLATION, EMU_AUDIT_SEVERITY_ERR, \
	    inst, "Capsicum violation: %s", operation)
#define	AUDIT_MMIO_VIOLATION(inst, addr) \
	emu_audit_log(EMU_AUDIT_EVENT_MMIO_VIOLATION, EMU_AUDIT_SEVERITY_ERR, \
	    inst, "MMIO violation: address 0x%lx", (u_long)addr)
#define	AUDIT_SHARE_PATH_INVALID(inst, path) \
	emu_audit_log(EMU_AUDIT_EVENT_SHARE_PATH_INVALID, EMU_AUDIT_SEVERITY_WARNING, \
	    inst, "Invalid share path: %s", path)
#define	AUDIT_SHARE_ACCESS(inst, path) \
	emu_audit_log(EMU_AUDIT_EVENT_SHARE_CREATE, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Share access granted: %s", path)
#define	AUDIT_SNAPSHOT_ACCESS(inst, name) \
	emu_audit_log(EMU_AUDIT_EVENT_SNAPSHOT_CREATE, EMU_AUDIT_SEVERITY_INFO, \
	    inst, "Snapshot access granted: %s", name)

/* Configuration accessors */
int	emu_audit_is_enabled(void);
int	emu_audit_get_destination(void);
int	emu_audit_set_destination(emu_audit_destination_t dest);
int	emu_audit_set_file_path(const char *path);
int	emu_audit_set_rotation(size_t size, int count);
int	emu_audit_set_min_severity(emu_audit_severity_t severity);

/* Log access (restricted to root) */
int	emu_audit_read_log(struct uio *uio);
int	emu_audit_check_access(struct thread *td);

#endif /* _KERNEL */

#endif /* !_EMULATION_EMU_AUDIT_H_ */
