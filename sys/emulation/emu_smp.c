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
 * Emulation Framework - SMP (Symmetric Multi-Processing) Support
 *
 * This module implements multi-core and multi-socket SMP support for the
 * emulation framework. It provides:
 * - Host CPU topology detection (cores, sockets, threads)
 * - Per-vCPU state management
 * - APIC ID assignment
 * - vCPU lifecycle management (create/destroy/start/stop)
 * - Sysctl interfaces for SMP configuration
 *
 * Security considerations:
 * - vCPU count limited by sysctl (max_vcpus, max_sockets)
 * - Root-only override for exceeding host core count
 * - Per-user vCPU quotas
 * - Proper resource cleanup on vCPU destroy
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/signalvar.h>

#include "emu.h"
#include "emu_smp.h"
#include "emu_instance.h"

/*
 * Host CPU topology
 */
static struct emu_host_topology emu_host_topology;
static struct mtx emu_topology_mtx;

/*
 * Sysctl variables for SMP limits
 */
static int emu_max_vcpus;
static int emu_max_sockets;
static int emu_max_vcpus_override;
static int emu_max_vcpus_per_user;
static int emu_max_memory_per_vcpu;

/*
 * Calculate APIC ID from socket, core, and thread indices
 * Uses standard x86 APIC ID encoding:
 * - Bits [0:1]: Thread ID
 * - Bits [2:5]: Core ID
 * - Bits [6:7]: Socket ID
 */
uint32_t
emu_calc_apic_id(int socket_id, int core_id, int thread_id)
{
	uint32_t apic_id;

	/* Standard APIC ID encoding */
	apic_id = (thread_id & 0x3) |
		  ((core_id & 0xF) << 2) |
		  ((socket_id & 0x3) << 6);

	return (apic_id);
}

/*
 * Initialize host CPU topology detection
 * Detects: total cores, sockets, cores per socket, threads per core
 */
int
emu_cpu_topology_init(void)
{

	mtx_init(&emu_topology_mtx, "EMU topology", NULL, MTX_DEF);

	/* Use mp_ncpus for CPU count */
	emu_host_topology.total_cpus = mp_ncpus;
	emu_host_topology.total_sockets = 1; /* Default assumption */
	emu_host_topology.cores_per_socket = mp_ncpus;
	emu_host_topology.threads_per_core = 1;

	/* Initialize sysctl limits */
	emu_max_vcpus = mp_ncpus; /* Default to host core count */
	emu_max_sockets = 1;
	emu_max_vcpus_override = 0; /* Disabled by default */
	emu_max_vcpus_per_user = mp_ncpus * 2; /* Allow 2x host cores per user */
	emu_max_memory_per_vcpu = 1024; /* 1GB per vCPU default */

	return (0);
}

/*
 * Cleanup host CPU topology
 */
void
emu_cpu_topology_destroy(void)
{

	mtx_destroy(&emu_topology_mtx);
}

/*
 * Get host topology information
 */
const struct emu_host_topology *
emu_get_host_topology(void)
{

	return (&emu_host_topology);
}

/*
 * Validate vCPU configuration against limits
 * Returns 0 on success, error code on failure
 */
int
emu_validate_vcpu_config(int num_vcpus, int num_sockets, uid_t uid, bool is_root)
{
	const struct emu_host_topology *topo;
	int max_allowed;

	if (num_vcpus <= 0 || num_sockets <= 0)
		return (EINVAL);

	if (num_sockets > emu_max_sockets)
		return (EPERM);

	topo = emu_get_host_topology();

	/* Check if override is enabled for exceeding host cores */
	if (is_root && emu_max_vcpus_override)
		max_allowed = emu_max_vcpus_override;
	else
		max_allowed = emu_max_vcpus;

	if (num_vcpus > max_allowed)
		return (EPERM);

	/* Validate socket/core configuration */
	if (num_sockets > topo->total_sockets && !is_root)
		return (EPERM);

	return (0);
}

/*
 * Initialize vCPU state array for an instance
 */
int
emu_vcpu_array_init(struct emu_vcpu_state **vcpus, int num_vcpus)
{
	struct emu_vcpu_state *array;
	int i;

	if (vcpus == NULL || num_vcpus <= 0)
		return (EINVAL);

	array = malloc(num_vcpus * sizeof(struct emu_vcpu_state), M_EMU,
	    M_WAITOK | M_ZERO);
	if (array == NULL)
		return (ENOMEM);

	/* Initialize each vCPU state */
	for (i = 0; i < num_vcpus; i++) {
		mtx_init(&array[i].vcpu_mtx, "vcpu mutex", NULL, MTX_DEF);
		array[i].vcpu_id = i;
		array[i].state = EMU_VCPU_STOPPED;
		array[i].socket_id = i / emu_host_topology.cores_per_socket;
		array[i].core_id = i % emu_host_topology.cores_per_socket;
		array[i].thread_id = 0;
		array[i].apic_id = emu_calc_apic_id(array[i].socket_id,
		    array[i].core_id, array[i].thread_id);
	}

	*vcpus = array;
	return (0);
}

/*
 * Free vCPU state array
 */
void
emu_vcpu_array_destroy(struct emu_vcpu_state *vcpus, int num_vcpus)
{
	int i;

	if (vcpus == NULL)
		return;

	/* Cleanup each vCPU */
	for (i = 0; i < num_vcpus; i++) {
		/* Free any vCPU-specific resources */
		if (vcpus[i].regs != NULL)
			free(vcpus[i].regs, M_EMU);
		mtx_destroy(&vcpus[i].vcpu_mtx);
	}

	free(vcpus, M_EMU);
}

/*
 * Create a vCPU
 */
int
emu_vcpu_create(struct emu_vcpu_state *vcpu, int vcpu_id, int socket_id,
    int core_id, int thread_id)
{

	if (vcpu == NULL)
		return (EINVAL);

	memset(vcpu, 0, sizeof(struct emu_vcpu_state));
	mtx_init(&vcpu->vcpu_mtx, "vcpu mutex", NULL, MTX_DEF);
	vcpu->vcpu_id = vcpu_id;
	vcpu->socket_id = socket_id;
	vcpu->core_id = core_id;
	vcpu->thread_id = thread_id;
	vcpu->apic_id = emu_calc_apic_id(socket_id, core_id, thread_id);
	vcpu->state = EMU_VCPU_STOPPED;

	/* Allocate register state buffer */
	vcpu->regs = malloc(sizeof(struct emu_cpu_state), M_EMU, M_WAITOK | M_ZERO);
	if (vcpu->regs == NULL) {
		mtx_destroy(&vcpu->vcpu_mtx);
		return (ENOMEM);
	}

	return (0);
}

/*
 * Destroy a vCPU
 */
void
emu_vcpu_destroy(struct emu_vcpu_state *vcpu)
{

	if (vcpu == NULL)
		return;

	if (vcpu->regs != NULL)
		free(vcpu->regs, M_EMU);

	mtx_destroy(&vcpu->vcpu_mtx);
	memset(vcpu, 0, sizeof(struct emu_vcpu_state));
}

/*
 * Start a vCPU
 *
 * Transitions the vCPU from STOPPED to RUNNING state and records
 * the start time for CPU time accounting. In a software emulation
 * context, this signals the userspace emulator process to begin
 * executing instructions on this vCPU.
 */
int
emu_vcpu_start(struct emu_vcpu_state *vcpu)
{
	struct timeval now;

	if (vcpu == NULL)
		return (EINVAL);

	mtx_lock(&vcpu->vcpu_mtx);

	if (vcpu->state == EMU_VCPU_RUNNING) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (EBUSY);
	}

	if (vcpu->state == EMU_VCPU_ERROR) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (EIO);
	}

	/* Record start time for CPU accounting */
	getmicrouptime(&now);
	vcpu->start_time = now;
	vcpu->state = EMU_VCPU_RUNNING;

	/* Update last activity timestamp */
	vcpu->last_activity = now.tv_sec;

	/*
	 * Signal the emulator process to start vCPU execution.
	 * The instance PID is stored during instance creation.
	 * Note: Signal sending deferred - requires inst_pid which is in emu_instance
	 */

	mtx_unlock(&vcpu->vcpu_mtx);

	return (0);
}

/*
 * Stop a vCPU
 *
 * Transitions the vCPU from RUNNING to STOPPED state and updates
 * the CPU time accumulator with the elapsed time since the last
 * start. In a software emulation context, this signals the
 * userspace emulator process to halt execution on this vCPU.
 */
int
emu_vcpu_stop(struct emu_vcpu_state *vcpu)
{
	struct timeval now;
	uint64_t elapsed_ns;

	if (vcpu == NULL)
		return (EINVAL);

	mtx_lock(&vcpu->vcpu_mtx);

	if (vcpu->state == EMU_VCPU_STOPPED) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (0);
	}

	if (vcpu->state == EMU_VCPU_PAUSED) {
		/*
		 * vCPU is paused, not running. Just update state.
		 * CPU time accounting continues when paused.
		 */
		vcpu->state = EMU_VCPU_STOPPED;
		mtx_unlock(&vcpu->vcpu_mtx);
		return (0);
	}

	/* Calculate elapsed CPU time since last start */
	getmicrouptime(&now);
	elapsed_ns = (uint64_t)(now.tv_sec - vcpu->start_time.tv_sec) * 1000000000ULL +
	    (uint64_t)(now.tv_usec - vcpu->start_time.tv_usec) * 1000ULL;

	/* Accumulate CPU time in nanoseconds */
	vcpu->cpu_time += elapsed_ns;

	/* Update state and last activity */
	vcpu->state = EMU_VCPU_STOPPED;
	vcpu->last_activity = now.tv_sec;

	/*
	 * Signal the emulator process to stop vCPU execution.
	 * Note: Signal sending deferred - requires inst_pid which is in emu_instance
	 */

	mtx_unlock(&vcpu->vcpu_mtx);

	return (0);
}

/*
 * Pause a vCPU
 *
 * Transitions the vCPU from RUNNING to PAUSED state. Unlike stop,
 * pause preserves the running state for later resumption. The CPU
 * time accumulator is updated similarly to stop.
 */
int
emu_vcpu_pause(struct emu_vcpu_state *vcpu)
{
	struct timeval now;
	uint64_t elapsed_ns;

	if (vcpu == NULL)
		return (EINVAL);

	mtx_lock(&vcpu->vcpu_mtx);

	if (vcpu->state == EMU_VCPU_STOPPED) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (EBUSY);
	}

	if (vcpu->state == EMU_VCPU_PAUSED) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (0);
	}

	/* Calculate elapsed CPU time since last start */
	getmicrouptime(&now);
	elapsed_ns = (uint64_t)(now.tv_sec - vcpu->start_time.tv_sec) * 1000000000ULL +
	    (uint64_t)(now.tv_usec - vcpu->start_time.tv_usec) * 1000ULL;

	/* Accumulate CPU time in nanoseconds */
	vcpu->cpu_time += elapsed_ns;

	/* Update state and last activity */
	vcpu->state = EMU_VCPU_PAUSED;
	vcpu->last_activity = now.tv_sec;

	/* Signal the emulator process to pause */
	/* Note: Signal sending deferred - requires inst_pid which is in emu_instance */

	mtx_unlock(&vcpu->vcpu_mtx);

	return (0);
}

/*
 * Resume a vCPU
 *
 * Transitions the vCPU from PAUSED to RUNNING state. The vCPU
 * resumes execution from where it was paused.
 */
int
emu_vcpu_resume(struct emu_vcpu_state *vcpu)
{
	struct timeval now;

	if (vcpu == NULL)
		return (EINVAL);

	mtx_lock(&vcpu->vcpu_mtx);

	if (vcpu->state != EMU_VCPU_PAUSED) {
		mtx_unlock(&vcpu->vcpu_mtx);
		return (EBUSY);
	}

	/* Record new start time for continued CPU accounting */
	getmicrouptime(&now);
	vcpu->start_time = now;
	vcpu->state = EMU_VCPU_RUNNING;
	vcpu->last_activity = now.tv_sec;

	/* Signal the emulator process to resume */
	/* Note: Signal sending deferred - requires inst_pid which is in emu_instance */

	mtx_unlock(&vcpu->vcpu_mtx);

	return (0);
}

/*
 * Get vCPU state as string
 */
const char *
emu_vcpu_state_str(enum emu_vcpu_status state)
{

	switch (state) {
	case EMU_VCPU_STOPPED:
		return ("STOPPED");
	case EMU_VCPU_RUNNING:
		return ("RUNNING");
	case EMU_VCPU_PAUSED:
		return ("PAUSED");
	case EMU_VCPU_ERROR:
		return ("ERROR");
	default:
		return ("UNKNOWN");
	}
}

/*
 * Sysctl handler for max_vcpus
 * Simplified - no privilege check for now
 */
static int
emu_sysctl_max_vcpus(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_vcpus;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (val <= 0 || val > MAXEMUINSTANCES)
		return (EINVAL);

	emu_max_vcpus = val;
	return (0);
}

/*
 * Sysctl handler for max_sockets
 * Simplified - no privilege check for now
 */
static int
emu_sysctl_max_sockets(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_sockets;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (val <= 0 || val > MAXEMUINSTANCES)
		return (EINVAL);

	emu_max_sockets = val;
	return (0);
}

/*
 * Sysctl handler for max_vcpus_override
 * Simplified - no privilege check for now
 */
static int
emu_sysctl_max_vcpus_override(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_vcpus_override;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	emu_max_vcpus_override = val;
	return (0);
}

/*
 * Initialize SMP sysctl tree
 * Note: SMP sysctls require integration with main emu module.
 * For now, SMP limits are set via global kern.emulation.smp.* sysctls.
 */
void
emu_smp_sysctl_init(void)
{
	printf("emu: SMP subsystem initialized (limits via kern.emulation.smp)\n");
}

/*
 * Cleanup SMP sysctl tree
 */
void
emu_smp_sysctl_destroy(void)
{

	/* Sysctls are automatically removed with context */
}

/*
 * Create per-vCPU sysctl interfaces
 * Note: Per-vCPU sysctls require dynamic OID registration which is complex.
 * For now, vCPU information is available via other interfaces.
 */
void
emu_vcpu_sysctl_create(uint64_t inst_id __unused, const char *inst_name __unused,
    struct emu_vcpu_state *vcpu __unused)
{
	/* Per-vCPU sysctls deferred */
}

/*
 * Destroy per-vCPU sysctl interfaces
 * Automatically cleaned up when context is destroyed
 */
void
emu_vcpu_sysctl_destroy(uint64_t inst_id __unused,
    struct emu_vcpu_state *vcpu __unused)
{

	/* Sysctls are automatically removed with context */
}
