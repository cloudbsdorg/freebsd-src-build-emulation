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

/*
 * Emulation Framework - bhyve Stub Implementations
 *
 * This file provides stub implementations when EMU_BHYVE_SUPPORT is disabled.
 * These stubs allow the emulation framework to compile and run without bhyve
 * support, providing graceful degradation for systems without VMM/bhyve.
 */

#ifndef _EMU_BHYVE_STUB_H_
#define	_EMU_BHYVE_STUB_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Stub configuration and state structures
 * These are minimal versions that allow compilation without bhyve
 */

/* Stub bhyve VM configuration */
struct emu_bhyve_config {
	char		vm_name[64];
	uint64_t	memory_size;
	int		num_cpus;
	char		firmware[256];
	char		disk_image[256];
	bool		host_only_net;
	bool		no_passthrough;
	uid_t		target_uid;
	gid_t		target_gid;
	void		*shares;	/* Placeholder */
};

/* Stub bhyve VM state */
struct emu_bhyve_state {
	int		vm_fd;
	int		vcpu_fds[16];
	int		num_vcpus;
	pid_t		pid;
	bool		privileges_dropped;
	uid_t		original_uid;
	gid_t		original_gid;
};

/*
 * Stub function implementations
 * All return error codes indicating bhyve is not available
 */

static inline const char *
emu_bhyve_error_str(int error __unused)
{
	return ("bhyve support disabled at compile time");
}

static inline bool
emu_bhyve_is_available(void)
{
	return (false);
}

static inline int
emu_bhyve_get_version(void)
{
	return (-1);
}

static inline bool
emu_bhyve_check_privileges(void)
{
	return (false);
}

static inline int
emu_bhyve_config_init(struct emu_bhyve_config *config)
{
	if (config == NULL)
		return (-1);
	memset(config, 0, sizeof(struct emu_bhyve_config));
	return (0);
}

static inline int
emu_bhyve_drop_privileges(uid_t target_uid __unused, gid_t target_gid __unused)
{
	return (-1);
}

static inline int
emu_bhyve_vm_create(struct emu_bhyve_config *config __unused,
    struct emu_bhyve_state *state __unused)
{
	return (-1);
}

static inline int
emu_bhyve_vm_start(struct emu_bhyve_state *state __unused)
{
	return (-1);
}

static inline int
emu_bhyve_vm_stop(struct emu_bhyve_state *state __unused)
{
	return (-1);
}

static inline int
emu_bhyve_vm_destroy(struct emu_bhyve_state *state __unused)
{
	return (-1);
}

static inline int
emu_bhyve_close_fds(struct emu_bhyve_state *state __unused)
{
	return (0);
}

static inline int
emu_bhyve_get_vm_fd(struct emu_bhyve_state *state __unused)
{
	return (-1);
}

static inline int
emu_bhyve_disable_coredump(void)
{
	return (-1);
}

static inline int
emu_bhyve_disable_ptrace(void)
{
	return (-1);
}

static inline int
emu_bhyve_enter_sandbox(void)
{
	return (-1);
}

static inline int
emu_bhyve_limit_vmm_rights(int vmm_fd __unused)
{
	return (-1);
}

static inline int
emu_bhyve_limit_vmm_ioctls(int vmm_fd __unused)
{
	return (-1);
}

static inline bool
emu_bhyve_is_essential_fd(int fd __unused)
{
	return (false);
}

static inline int
emu_bhyve_configure_9p(struct emu_bhyve_config *config __unused,
    struct emu_bhyve_state *state __unused)
{
	return (-1);
}

#endif /* !_EMU_BHYVE_STUB_H_ */
