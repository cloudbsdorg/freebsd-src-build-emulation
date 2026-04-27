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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Emulation Framework - SMP (Symmetric Multi-Processing) Support Header
 *
 * This header provides the SMP support definitions for the emulation
 * framework, including vCPU state management, topology detection, and
 * APIC ID assignment.
 */

#ifndef _EMU_SMP_H_
#define	_EMU_SMP_H_

/*
 * vCPU state enumeration
 */
enum emu_vcpu_state {
	EMU_VCPU_STOPPED = 0,
	EMU_VCPU_RUNNING,
	EMU_VCPU_PAUSED,
	EMU_VCPU_ERROR
};

/*
 * Host CPU topology structure
 */
struct emu_host_topology {
	int	total_cpus;		/* Total CPU count (hw.ncpu) */
	int	total_sockets;		/* Total socket count */
	int	cores_per_socket;	/* Cores per socket */
	int	threads_per_core;	/* Threads per core (SMT) */
};

/*
 * Per-vCPU state structure
 */
struct emu_vcpu_state {
	int			vcpu_id;	/* vCPU index (0..num_vcpus-1) */
	enum emu_vcpu_state	state;		/* vCPU state */
	int			socket_id;	/* Socket ID */
	int			core_id;	/* Core ID within socket */
	int			thread_id;	/* Thread ID within core */
	uint32_t		apic_id;	/* APIC ID */
	struct emu_cpu_state	*regs;		/* Register state */
	uint64_t		cpu_time;	/* CPU time used (ns) */
	uint64_t		last_activity;	/* Last activity timestamp */
};

/*
 * Function prototypes
 */
#ifdef _KERNEL

/* Topology detection */
int	emu_cpu_topology_init(void);
void	emu_cpu_topology_destroy(void);
const struct emu_host_topology *emu_get_host_topology(void);

/* APIC ID calculation */
uint32_t emu_calc_apic_id(int socket_id, int core_id, int thread_id);

/* vCPU configuration validation */
int	emu_validate_vcpu_config(int num_vcpus, int num_sockets, uid_t uid,
		    bool is_root);

/* vCPU array management */
int	emu_vcpu_array_init(struct emu_vcpu_state **vcpus, int num_vcpus);
void	emu_vcpu_array_destroy(struct emu_vcpu_state *vcpus, int num_vcpus);

/* vCPU lifecycle */
int	emu_vcpu_create(struct emu_vcpu_state *vcpu, int vcpu_id,
		    int socket_id, int core_id, int thread_id);
void	emu_vcpu_destroy(struct emu_vcpu_state *vcpu);
int	emu_vcpu_start(struct emu_vcpu_state *vcpu);
int	emu_vcpu_stop(struct emu_vcpu_state *vcpu);

/* Utility functions */
const char *emu_vcpu_state_str(enum emu_vcpu_state state);

/* Sysctl interface */
void	emu_smp_sysctl_init(void);
void	emu_smp_sysctl_destroy(void);

#endif /* _KERNEL */

#endif /* !_EMU_SMP_H_ */
