/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _EMU_BHYVE_H_
#define	_EMU_BHYVE_H_

/*
 * Emulation Framework - bhyve/VMM Integration
 *
 * This module provides bhyve/VMM integration with proper privilege
 * dropping after VM creation for security.
 *
 * Compile with -DEMU_BHYVE_SUPPORT to enable bhyve support.
 * Without this flag, stub implementations are used for graceful
 * degradation on systems without VMM/bhyve.
 *
 * Security considerations (when enabled):
 * - Root privileges required only for VM creation
 * - Privileges dropped immediately after VM creation via setuid/setgid
 * - VM process runs as unprivileged user
 * - File descriptors closed after VM creation
 * - No passthrough devices for emulation instances
 */

#ifdef EMU_BHYVE_SUPPORT
#include <sys/types.h>
#include "sys_capsicum_compat.h"
#include <stdint.h>
#include <stdbool.h>
#include "emu_share.h"
#else
/*
 * bhyve support is disabled at compile time.
 * Stub implementations are provided in emu_bhyve_stub.h
 * for graceful degradation on systems without VMM/bhyve.
 */
#include "emu_bhyve_stub.h"
#endif

#ifdef EMU_BHYVE_SUPPORT

/* bhyve VM configuration */
struct emu_bhyve_config {
	char		vm_name[64];	/* VM name */
	uint64_t	memory_size;	/* VM memory size */
	int		num_cpus;	/* Number of vCPUs */
	char		firmware[256];	/* Firmware image path */
	char		disk_image[256];	/* Disk image path */
	bool		host_only_net;	/* Host-only networking */
	bool		no_passthrough;	/* No device passthrough */
	uid_t		target_uid;	/* Target UID for privilege dropping */
	gid_t		target_gid;	/* Target GID for privilege dropping */
	struct emu_share_config shares;	/* Filesystem share configuration */
};

/* bhyve VM state */
struct emu_bhyve_state {
	int		vm_fd;		/* VM file descriptor */
	int		vcpu_fds[16];	/* vCPU file descriptors */
	int		num_vcpus;	/* Number of vCPUs */
	pid_t		pid;		/* VM process PID */
	bool		privileges_dropped;	/* Privileges dropped flag */
	uid_t		original_uid;	/* Original UID */
	gid_t		original_gid;	/* Original GID */
};

/*
 * bhyve lifecycle management
 */

/* Initialize bhyve configuration */
int emu_bhyve_config_init(struct emu_bhyve_config *config);

/* Create bhyve VM (requires root) */
int emu_bhyve_vm_create(struct emu_bhyve_config *config,
    struct emu_bhyve_state *state);

/* Start bhyve VM */
int emu_bhyve_vm_start(struct emu_bhyve_state *state);

/* Stop bhyve VM */
int emu_bhyve_vm_stop(struct emu_bhyve_state *state);

/* Destroy bhyve VM */
int emu_bhyve_vm_destroy(struct emu_bhyve_state *state);

/*
 * Privilege management
 */

/* Drop privileges after VM creation */
int emu_bhyve_drop_privileges(uid_t target_uid, gid_t target_gid);

/* Check if running with sufficient privileges */
bool emu_bhyve_check_privileges(void);

/*
 * Resource management
 */

/* Close unnecessary file descriptors */
int emu_bhyve_close_fds(struct emu_bhyve_state *state);

/* Get VM file descriptor */
int emu_bhyve_get_vm_fd(struct emu_bhyve_state *state);

/*
 * Utility functions
 */

/* Convert bhyve error to string */
const char *emu_bhyve_error_str(int error);

/* Check if bhyve is available on this system */
bool emu_bhyve_is_available(void);

/* Get bhyve version */
int emu_bhyve_get_version(void);

/*
 * Core dump prevention for bhyve process
 */

/* Disable core dumps for bhyve process */
int emu_bhyve_disable_coredump(void);

/*
 * Ptrace prevention for bhyve process
 */

/* Disable ptrace attachment for bhyve process */
int emu_bhyve_disable_ptrace(void);

/*
 * Capsicum sandboxing for bhyve process
 */

/* Enter Capsicum capability mode for bhyve process */
int emu_bhyve_enter_sandbox(void);

/* Limit rights on VMM file descriptor */
int emu_bhyve_limit_vmm_rights(int vmm_fd);

/* Limit ioctl operations on VMM file descriptor */
int emu_bhyve_limit_vmm_ioctls(int vmm_fd);

/* Check if FD is essential for bhyve operation */
bool emu_bhyve_is_essential_fd(int fd);

/*
 * VirtIO-9p filesystem sharing for bhyve
 */

/* Configure virtio-9p devices for bhyve VM */
int emu_bhyve_configure_9p(struct emu_bhyve_config *config,
    struct emu_bhyve_state *state);

#endif /* EMU_BHYVE_SUPPORT */

#endif /* !_EMU_BHYVE_H_ */
