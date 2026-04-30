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
#include <sys/cpuinfo.h>
#include <sys/syslog.h>

#include "emulation/emu.h"
#include "emulation/emu_smp.h"
#include "emulation/emu_instance.h"

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
	int error;
	int ncpu_val, sockets_val, cores_per_socket_val;
	size_t len;

	mtx_init(&emu_topology_mtx, "EMU topology", NULL, MTX_DEF);

	/* Get hw.ncpu */
	len = sizeof(ncpu_val);
	error = kernel_sysctlbyname("hw.ncpu", &ncpu_val, &len, NULL, 0);
	if (error != 0) {
		/* Fallback to mp_ncpu */
		ncpu_val = mp_ncpu;
	}

	/* Get hw.sockets */
	len = sizeof(sockets_val);
	error = kernel_sysctlbyname("hw.sockets", &sockets_val, &len, NULL, 0);
	if (error != 0) {
		/* Assume 1 socket if unknown */
		sockets_val = 1;
	}

	/* Get hw.cores per socket */
	len = sizeof(cores_per_socket_val);
	error = kernel_sysctlbyname("hw.cores_per_socket", &cores_per_socket_val, &len, NULL, 0);
	if (error != 0) {
		/* Calculate from ncpu and sockets */
		cores_per_socket_val = ncpu_val / sockets_val;
	}

	/* Populate topology structure */
	emu_host_topology.total_cpus = ncpu_val;
	emu_host_topology.total_sockets = sockets_val;
	emu_host_topology.cores_per_socket = cores_per_socket_val;
	emu_host_topology.threads_per_core = ncpu_val / (sockets_val * cores_per_socket_val);
	if (emu_host_topology.threads_per_core == 0)
		emu_host_topology.threads_per_core = 1;

	/* Initialize sysctl limits */
	emu_max_vcpus = ncpu_val; /* Default to host core count */
	emu_max_sockets = sockets_val;
	if (emu_max_sockets == 0)
		emu_max_sockets = 1;
	emu_max_vcpus_override = 0; /* Disabled by default */
	emu_max_vcpus_per_user = ncpu_val * 2; /* Allow 2x host cores per user */
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
	struct proc *p;
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
	 */
	if (vcpu->inst_id != 0) {
		/*
		 * Look up the emulator process by instance ID.
		 * In practice, the userspace emulator would listen for
		 * SIGUSR1 to begin vCPU execution.
		 */
		struct emu_instance *inst;
		mtx_lock(&emu_instance_lock);
		inst = emu_find_instance(vcpu->inst_id);
		if (inst != NULL && inst->inst_pid != 0) {
			p = pfind(inst->inst_pid);
			if (p != NULL) {
				/* Signal the emulator process */
				psignal(p, SIGUSR1);
				prele(p);
			}
		}
		mtx_unlock(&emu_instance_lock);
	}

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
	struct proc *p;
	struct timeval now, elapsed;

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
	timersub(&now, &vcpu->start_time, &elapsed);

	/* Accumulate CPU time in nanoseconds */
	vcpu->cpu_time += (uint64_t)elapsed.tv_sec * 1000000000ULL +
	    (uint64_t)elapsed.tv_usec * 1000ULL;

	/* Update state and last activity */
	vcpu->state = EMU_VCPU_STOPPED;
	vcpu->last_activity = now.tv_sec;

	/*
	 * Signal the emulator process to stop vCPU execution.
	 * The instance PID is stored during instance creation.
	 */
	if (vcpu->inst_id != 0) {
		struct emu_instance *inst;
		mtx_lock(&emu_instance_lock);
		inst = emu_find_instance(vcpu->inst_id);
		if (inst != NULL && inst->inst_pid != 0) {
			p = pfind(inst->inst_pid);
			if (p != NULL) {
				/* Signal the emulator process to stop */
				psignal(p, SIGUSR2);
				prele(p);
			}
		}
		mtx_unlock(&emu_instance_lock);
	}

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
	struct timeval now, elapsed;

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
	timersub(&now, &vcpu->start_time, &elapsed);

	/* Accumulate CPU time in nanoseconds */
	vcpu->cpu_time += (uint64_t)elapsed.tv_sec * 1000000000ULL +
	    (uint64_t)elapsed.tv_usec * 1000ULL;

	/* Update state and last activity */
	vcpu->state = EMU_VCPU_PAUSED;
	vcpu->last_activity = now.tv_sec;

	/* Signal the emulator process to pause */
	if (vcpu->inst_id != 0) {
		struct emu_instance *inst;
		mtx_lock(&emu_instance_lock);
		inst = emu_find_instance(vcpu->inst_id);
		if (inst != NULL && inst->inst_pid != 0) {
			struct proc *p = pfind(inst->inst_pid);
			if (p != NULL) {
				psignal(p, SIGSTOP);
				prele(p);
			}
		}
		mtx_unlock(&emu_instance_lock);
	}

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
	if (vcpu->inst_id != 0) {
		struct emu_instance *inst;
		mtx_lock(&emu_instance_lock);
		inst = emu_find_instance(vcpu->inst_id);
		if (inst != NULL && inst->inst_pid != 0) {
			struct proc *p = pfind(inst->inst_pid);
			if (p != NULL) {
				psignal(p, SIGCONT);
				prele(p);
			}
		}
		mtx_unlock(&emu_instance_lock);
	}

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
 */
static int
emu_sysctl_max_vcpus(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_vcpus;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* Only root can change this */
	if (req->td != NULL && priv_check(req->td, PRIV_ROOT) != 0)
		return (EPERM);

	if (val <= 0 || val > MAXEMUINSTANCES)
		return (EINVAL);

	emu_max_vcpus = val;
	return (0);
}

/*
 * Sysctl handler for max_sockets
 */
static int
emu_sysctl_max_sockets(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_sockets;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* Only root can change this */
	if (req->td != NULL && priv_check(req->td, PRIV_ROOT) != 0)
		return (EPERM);

	if (val <= 0 || val > MAXEMUINSTANCES)
		return (EINVAL);

	emu_max_sockets = val;
	return (0);
}

/*
 * Sysctl handler for max_vcpus_override
 */
static int
emu_sysctl_max_vcpus_override(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = emu_max_vcpus_override;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* Only root can change this */
	if (req->td != NULL && priv_check(req->td, PRIV_ROOT) != 0)
		return (EPERM);

	emu_max_vcpus_override = val;
	return (0);
}

/*
 * Initialize SMP sysctl tree
 */
void
emu_smp_sysctl_init(void)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;

	ctx = emu_sysctl_get_context();
	oid = emu_sysctl_get_oid();

	/* Add SMP sysctls */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_vcpus",
	    CTLFLAG_RW, &emu_max_vcpus, 0,
	    "Maximum number of vCPUs per instance");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_vcpus_limit",
	    CTLTYPE_INT | CTLFLAG_RW, &emu_max_vcpus, 0,
	    emu_sysctl_max_vcpus, "I", "Set max_vcpus limit (root only)");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_sockets",
	    CTLFLAG_RW, &emu_max_sockets, 0,
	    "Maximum number of sockets per instance");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_sockets_limit",
	    CTLTYPE_INT | CTLFLAG_RW, &emu_max_sockets, 0,
	    emu_sysctl_max_sockets, "I", "Set max_sockets limit (root only)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_vcpus_override",
	    CTLTYPE_INT | CTLFLAG_RW, &emu_max_vcpus_override, 0,
	    emu_sysctl_max_vcpus_override, "I",
	    "Override max_vcpus to exceed host cores (root only)");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_vcpus_per_user",
	    CTLFLAG_RW, &emu_max_vcpus_per_user, 0,
	    "Maximum vCPUs per user across all instances");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "max_memory_per_vcpu",
	    CTLFLAG_RW, &emu_max_memory_per_vcpu, 0,
	    "Maximum memory (MB) per vCPU");

	/* Read-only topology information */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "host_total_cpus",
	    CTLFLAG_RD, &emu_host_topology.total_cpus, 0,
	    "Host total CPU count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "host_total_sockets",
	    CTLFLAG_RD, &emu_host_topology.total_sockets, 0,
	    "Host total socket count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "host_cores_per_socket",
	    CTLFLAG_RD, &emu_host_topology.cores_per_socket, 0,
	    "Host cores per socket");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "host_threads_per_core",
	    CTLFLAG_RD, &emu_host_topology.threads_per_core, 0,
	    "Host threads per core");
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
 * Creates: kern.emulation.instance.<name>.vcpu.<id>.
 *   - state: vCPU state (running/stopped/paused/error)
 *   - apic_id: APIC ID
 *   - socket_id: Socket ID
 *   - core_id: Core ID within socket
 *   - thread_id: Thread ID within core
 *   - cpu_time: CPU time used (ns)
 */
void
emu_vcpu_sysctl_create(uint64_t inst_id __unused, const char *inst_name,
    struct emu_vcpu_state *vcpu)
{
	static struct sysctl_ctx_list vcpu_ctx;
	struct sysctl_oid *vcpu_oid;
	char vcpu_name[32];

	if (vcpu == NULL || inst_name == NULL)
		return;

	/* Initialize context for this vCPU */
	SYSCTL_INIT_LIST(&vcpu_ctx);

	/* Create kern.emulation.instance.<name>.vcpu.<id> node */
	snprintf(vcpu_name, sizeof(vcpu_name), "vcpu%d", vcpu->vcpu_id);
	vcpu_oid = SYSCTL_ADD_NODE(&vcpu_ctx,
	    SYSCTL_STATIC_CHILDREN(_kern_emulation), OID_AUTO,
	    vcpu_name, CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "vCPU %d state", vcpu->vcpu_id);

	if (vcpu_oid == NULL)
		return;

	/* Add vCPU state sysctl */
	SYSCTL_ADD_INT(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "state", CTLFLAG_RD, &vcpu->state, 0,
	    "vCPU state (0=stopped, 1=running, 2=paused, 3=error)");

	/* Add APIC ID sysctl */
	SYSCTL_ADD_UINT(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "apic_id", CTLFLAG_RD, &vcpu->apic_id, 0,
	    "APIC ID");

	/* Add socket ID sysctl */
	SYSCTL_ADD_INT(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "socket_id", CTLFLAG_RD, &vcpu->socket_id, 0,
	    "Socket ID");

	/* Add core ID sysctl */
	SYSCTL_ADD_INT(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "core_id", CTLFLAG_RD, &vcpu->core_id, 0,
	    "Core ID within socket");

	/* Add thread ID sysctl */
	SYSCTL_ADD_INT(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "thread_id", CTLFLAG_RD, &vcpu->thread_id, 0,
	    "Thread ID within core");

	/* Add CPU time sysctl */
	SYSCTL_ADD_U64(&vcpu_ctx, SYSCTL_CHILDREN(vcpu_oid), OID_AUTO,
	    "cpu_time", CTLFLAG_RD, &vcpu->cpu_time, 0,
	    "CPU time used (nanoseconds)");
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
	/* No explicit cleanup needed */
}
