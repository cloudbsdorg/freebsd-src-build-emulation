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
 * Emulation Framework Instance Management
 *
 * This file provides instance lifecycle management, resource tracking,
 * and resource limits enforcement for the kernel emulation framework.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/ucred.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/limits.h>
#include "emu.h"
#include "emu_smp.h"

/*
 * Instance resource limits
 */
#define	EMU_DEFAULT_MAX_INSTANCES		32
#define	EMU_DEFAULT_MAX_MEMORY_PER_INST		(16ULL * 1024ULL * 1024ULL * 1024ULL) /* 16GB */
#define	EMU_DEFAULT_MAX_CPU_TIME_PER_INST	(24 * 60 * 60) /* 24 hours in seconds */
#define	EMU_DEFAULT_MAX_INSTANCES_PER_USER	8

/*
 * Emulation instance structure
 */
struct emu_instance {
	uint64_t		inst_id;
	char			inst_name[64];
	int			inst_state;
	struct ucred		*inst_cred;	/* Creating user's credentials */
	uid_t			inst_uid;
	gid_t			inst_gid;
	uint64_t		inst_memory_limit;	/* Max memory in bytes */
	uint64_t		inst_memory_used;	/* Current memory usage */
	uint64_t		inst_cpu_time_limit;	/* Max CPU time in seconds */
	uint64_t		inst_cpu_time_used;	/* Current CPU time used */
	struct timeval		inst_start_time;	/* When instance was started */
	struct timeval		inst_last_activity;	/* Last activity timestamp */
	int			inst_num_vcpus;	/* Number of vCPUs */
	int			inst_num_sockets;	/* Number of sockets */
	struct emu_vcpu_state	*inst_vcpus;	/* vCPU state array */
	TAILQ_ENTRY(emu_instance) inst_link;
};

/*
 * Per-user resource tracking
 */
struct emu_user_limits {
	uid_t			ul_uid;
	int			ul_instance_count;
	uint64_t		ul_total_memory;
	TAILQ_ENTRY(emu_user_limits) ul_link;
};

/*
 * Global instance registry
 */
static struct mtx emu_instance_lock;
static TAILQ_HEAD(, emu_instance) emu_instances = TAILQ_HEAD_INITIALIZER(emu_instances);
static TAILQ_HEAD(, emu_user_limits) emu_user_limits = TAILQ_HEAD_INITIALIZER(emu_user_limits);

/*
 * Global resource limits (sysctl-controlled)
 */
static int emu_max_instances = EMU_DEFAULT_MAX_INSTANCES;
static int emu_max_instances_per_user = EMU_DEFAULT_MAX_INSTANCES_PER_USER;
static uint64_t emu_max_memory_per_instance = EMU_DEFAULT_MAX_MEMORY_PER_INST;
static uint64_t emu_max_cpu_time_per_instance = EMU_DEFAULT_MAX_CPU_TIME_PER_INST;

/*
 * Instance ID counter
 */
static uint64_t emu_next_instance_id = 1;

/*
 * Find user limits structure by UID
 * Must be called with emu_instance_lock held
 */
static struct emu_user_limits *
emu_find_user_limits(uid_t uid)
{
	struct emu_user_limits *ul;

	TAILQ_FOREACH(ul, &emu_user_limits, ul_link) {
		if (ul->ul_uid == uid)
			return (ul);
	}
	return (NULL);
}

/*
 * Get or create user limits structure
 * Must be called with emu_instance_lock held
 */
static struct emu_user_limits *
emu_get_user_limits(uid_t uid)
{
	struct emu_user_limits *ul;

	ul = emu_find_user_limits(uid);
	if (ul != NULL)
		return (ul);

	ul = malloc(sizeof(*ul), M_EMU, M_WAITOK | M_ZERO);
	ul->ul_uid = uid;
	TAILQ_INSERT_TAIL(&emu_user_limits, ul, ul_link);

	return (ul);
}

/*
 * Check if user can create a new instance
 * Returns 0 on success, error code on failure
 */
static int
emu_check_user_limits(uid_t uid)
{
	struct emu_user_limits *ul;
	int error;

	mtx_assert(&emu_instance_lock, MA_OWNED);

	/* Check total instance count */
	if (TAILQ_EMPTY(&emu_instances) == 0 &&
	    TAILQ_COUNT(&emu_instances) >= emu_max_instances) {
		printf("emu: maximum instance count reached (%d)\n",
		    emu_max_instances);
		return (EMFILE);
	}

	/* Check per-user instance count */
	ul = emu_get_user_limits(uid);
	if (ul->ul_instance_count >= emu_max_instances_per_user) {
		printf("emu: user %d has reached maximum instance count (%d)\n",
		    uid, emu_max_instances_per_user);
		return (EMFILE);
	}

	return (0);
}

/*
 * Update user limits when creating an instance
 * Must be called with emu_instance_lock held
 */
static void
emu_user_create_instance(uid_t uid, uint64_t memory)
{
	struct emu_user_limits *ul;

	mtx_assert(&emu_instance_lock, MA_OWNED);

	ul = emu_get_user_limits(uid);
	ul->ul_instance_count++;
	ul->ul_total_memory += memory;
}

/*
 * Update user limits when destroying an instance
 * Must be called with emu_instance_lock held
 */
static void
emu_user_destroy_instance(uid_t uid, uint64_t memory)
{
	struct emu_user_limits *ul;

	mtx_assert(&emu_instance_lock, MA_OWNED);

	ul = emu_find_user_limits(uid);
	if (ul != NULL) {
		ul->ul_instance_count--;
		if (ul->ul_total_memory >= memory)
			ul->ul_total_memory -= memory;
		else
			ul->ul_total_memory = 0;

		/* Clean up if no longer in use */
		if (ul->ul_instance_count == 0) {
			TAILQ_REMOVE(&emu_user_limits, ul, ul_link);
			free(ul, M_EMU);
		}
	}
}

/*
 * Check if credential can see an instance
 * This implements cr_cansee()-like filtering for emulation instances
 *
 * Returns 1 if visible, 0 if hidden
 * Must be called with emu_instance_lock held
 */
static int
emu_instance_cansee(struct ucred *cred, struct emu_instance *inst)
{

	/* Root can see all instances */
	if (cred->cr_uid == 0)
		return (1);

	/* Users in GID_EMU group can see all instances */
	if (groupmember(GID_EMU, cred))
		return (1);

	/* Owner can see their own instance */
	if (cred->cr_uid == inst->inst_uid)
		return (1);

	/* Others cannot see this instance */
	return (0);
}

/*
 * Find instance by ID with visibility filtering
 * Must be called with emu_instance_lock held
 */
static struct emu_instance *
emu_find_instance(uint64_t inst_id)
{
	struct emu_instance *inst;
	struct thread *td;
	struct ucred *cred;

	td = curthread;
	cred = td->td_ucred;

	TAILQ_FOREACH(inst, &emu_instances, inst_link) {
		if (inst->inst_id == inst_id) {
			/* Check if caller can see this instance */
			if (emu_instance_cansee(cred, inst))
				return (inst);
			break;
		}
	}
	return (NULL);
}

/*
 * Find instance by name
 * Must be called with emu_instance_lock held
 */
static struct emu_instance *
emu_find_instance_by_name(const char *name)
{
	struct emu_instance *inst;

	TAILQ_FOREACH(inst, &emu_instances, inst_link) {
		if (strcmp(inst->inst_name, name) == 0)
			return (inst);
	}
	return (NULL);
}

/*
 * Create a new emulation instance
 * Returns instance ID on success, error code on failure
 */
int
emu_instance_create(const char *name, uid_t uid, gid_t gid, uint64_t memory_limit,
    uint64_t cpu_time_limit, int num_vcpus, int num_sockets, uint64_t *inst_id_out)
{
	struct emu_instance *inst;
	int error;

	mtx_lock(&emu_instance_lock);

	/* Check if name already exists */
	if (emu_find_instance_by_name(name) != NULL) {
		mtx_unlock(&emu_instance_lock);
		return (EEXIST);
	}

	/* Check user limits */
	error = emu_check_user_limits(uid);
	if (error != 0) {
		mtx_unlock(&emu_instance_lock);
		return (error);
	}

	/* Validate vCPU configuration */
	if (num_vcpus <= 0)
		num_vcpus = 1;
	if (num_sockets <= 0)
		num_sockets = 1;

	error = emu_validate_vcpu_config(num_vcpus, num_sockets, uid,
	    suser(curthread) == 0);
	if (error != 0) {
		mtx_unlock(&emu_instance_lock);
		return (error);
	}

	/* Allocate instance structure */
	inst = malloc(sizeof(*inst), M_EMU, M_WAITOK | M_ZERO);
	inst->inst_id = emu_next_instance_id++;
	strlcpy(inst->inst_name, name, sizeof(inst->inst_name));
	inst->inst_state = EMU_INST_STOPPED;
	inst->inst_uid = uid;
	inst->inst_gid = gid;
	inst->inst_memory_limit = memory_limit != 0 ?
	    memory_limit : emu_max_memory_per_instance;
	inst->inst_cpu_time_limit = cpu_time_limit != 0 ?
	    cpu_time_limit : emu_max_cpu_time_per_instance;
	inst->inst_num_vcpus = num_vcpus;
	inst->inst_num_sockets = num_sockets;
	getmicrouptime(&inst->inst_start_time);
	inst->inst_last_activity = inst->inst_start_time;

	/* Initialize vCPU array */
	error = emu_vcpu_array_init(&inst->inst_vcpus, num_vcpus);
	if (error != 0) {
		mtx_unlock(&emu_instance_lock);
		free(inst, M_EMU);
		return (error);
	}

	/* Assign APIC IDs based on topology */
	for (int i = 0; i < num_vcpus; i++) {
		int socket_id = i / (num_vcpus / num_sockets);
		int core_id = i % (num_vcpus / num_sockets);
		inst->inst_vcpus[i].socket_id = socket_id;
		inst->inst_vcpus[i].core_id = core_id;
		inst->inst_vcpus[i].thread_id = 0;
		inst->inst_vcpus[i].apic_id = emu_calc_apic_id(socket_id, core_id, 0);
	}

	/* Update user limits */
	emu_user_create_instance(uid, inst->inst_memory_limit);

	/* Add to instance list */
	TAILQ_INSERT_TAIL(&emu_instances, inst, inst_link);

	mtx_unlock(&emu_instance_lock);

	if (inst_id_out != NULL)
		*inst_id_out = inst->inst_id;

	printf("emu: created instance %s (ID %lu, UID %d, memory limit %lu bytes, %d vCPUs, %d sockets)\n",
	    name, (u_long)inst->inst_id, uid, (u_long)inst->inst_memory_limit,
	    num_vcpus, num_sockets);

	return (0);
}

/*
 * Destroy an emulation instance
 * Returns 0 on success, error code on failure
 */
int
emu_instance_destroy(uint64_t inst_id)
{
	struct emu_instance *inst;
	int error;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (ENOENT);
	}

	/* Refuse to destroy running instance */
	if (inst->inst_state == EMU_INST_RUNNING) {
		mtx_unlock(&emu_instance_lock);
		return (EBUSY);
	}

	/* Update user limits */
	emu_user_destroy_instance(inst->inst_uid, inst->inst_memory_limit);

	/* Remove from instance list */
	TAILQ_REMOVE(&emu_instances, inst, inst_link);

	mtx_unlock(&emu_instance_lock);

	printf("emu: destroyed instance %s (ID %lu)\n",
	    inst->inst_name, (u_long)inst->inst_id);

	/* Destroy vCPU array */
	if (inst->inst_vcpus != NULL)
		emu_vcpu_array_destroy(inst->inst_vcpus, inst->inst_num_vcpus);

	free(inst, M_EMU);

	return (0);
}

/*
 * Start an emulation instance
 * Returns 0 on success, error code on failure
 */
int
emu_instance_start(uint64_t inst_id)
{
	struct emu_instance *inst;
	int error;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (ENOENT);
	}

	/* Check if already running */
	if (inst->inst_state == EMU_INST_RUNNING) {
		mtx_unlock(&emu_instance_lock);
		return (EALREADY);
	}

	/* Update state and timestamps */
	inst->inst_state = EMU_INST_RUNNING;
	getmicrouptime(&inst->inst_last_activity);

	mtx_unlock(&emu_instance_lock);

	printf("emu: started instance %s (ID %lu)\n",
	    inst->inst_name, (u_long)inst->inst_id);

	return (0);
}

/*
 * Stop an emulation instance
 * Returns 0 on success, error code on failure
 */
int
emu_instance_stop(uint64_t inst_id)
{
	struct emu_instance *inst;
	int error;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (ENOENT);
	}

	/* Check if already stopped */
	if (inst->inst_state == EMU_INST_STOPPED) {
		mtx_unlock(&emu_instance_lock);
		return (EALREADY);
	}

	/* Update state and timestamps */
	inst->inst_state = EMU_INST_STOPPED;
	getmicrouptime(&inst->inst_last_activity);

	mtx_unlock(&emu_instance_lock);

	printf("emu: stopped instance %s (ID %lu)\n",
	    inst->inst_name, (u_long)inst->inst_id);

	return (0);
}

/*
 * Get instance state
 * Returns state flags on success, -1 on error
 */
int
emu_instance_get_state(uint64_t inst_id)
{
	struct emu_instance *inst;
	int state;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (-1);
	}

	state = inst->inst_state;
	mtx_unlock(&emu_instance_lock);

	return (state);
}

/*
 * Update instance memory usage
 */
void
emu_instance_update_memory(uint64_t inst_id, uint64_t memory_used)
{
	struct emu_instance *inst;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return;
	}

	inst->inst_memory_used = memory_used;
	getmicrouptime(&inst->inst_last_activity);

	mtx_unlock(&emu_instance_lock);
}

/*
 * Update instance CPU time usage
 */
void
emu_instance_update_cpu_time(uint64_t inst_id, uint64_t cpu_time)
{
	struct emu_instance *inst;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return;
	}

	inst->inst_cpu_time_used = cpu_time;
	getmicrouptime(&inst->inst_last_activity);

	mtx_unlock(&emu_instance_lock);
}

/*
 * Check if instance has exceeded CPU time limit
 * Returns 1 if exceeded, 0 otherwise
 */
int
emu_instance_check_cpu_limit(uint64_t inst_id)
{
	struct emu_instance *inst;
	int exceeded;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (0);
	}

	exceeded = (inst->inst_cpu_time_used >= inst->inst_cpu_time_limit);

	mtx_unlock(&emu_instance_lock);

	return (exceeded);
}

/*
 * Get instance info for sysctl
 * Returns 0 on success, error code on failure
 */
int
emu_instance_get_info(uint64_t inst_id, struct sbuf *sb)
{
	struct emu_instance *inst;
	int error;

	mtx_lock(&emu_instance_lock);

	inst = emu_find_instance(inst_id);
	if (inst == NULL) {
		mtx_unlock(&emu_instance_lock);
		return (ENOENT);
	}

	sbuf_printf(sb, "name=%s,state=%d,uid=%d,gid=%d,",
	    inst->inst_name, inst->inst_state,
	    inst->inst_uid, inst->inst_gid);
	sbuf_printf(sb, "memory_limit=%lu,memory_used=%lu,",
	    (u_long)inst->inst_memory_limit, (u_long)inst->inst_memory_used);
	sbuf_printf(sb, "cpu_limit=%lu,cpu_used=%lu",
	    (u_long)inst->inst_cpu_time_limit, (u_long)inst->inst_cpu_time_used);

	mtx_unlock(&emu_instance_lock);

	return (0);
}

/*
 * Get total instance count (all instances, no filtering)
 */
int
emu_instance_total_count(void)
{
	int count;

	mtx_lock(&emu_instance_lock);
	count = TAILQ_COUNT(&emu_instances);
	mtx_unlock(&emu_instance_lock);

	return (count);
}

/*
 * Get visible instance count for current thread
 * This implements cr_cansee()-like filtering
 */
int
emu_instance_visible_count(void)
{
	struct emu_instance *inst;
	struct thread *td;
	struct ucred *cred;
	int count;

	td = curthread;
	cred = td->td_ucred;

	mtx_lock(&emu_instance_lock);
	count = 0;
	TAILQ_FOREACH(inst, &emu_instances, inst_link) {
		if (emu_instance_cansee(cred, inst))
			count++;
	}
	mtx_unlock(&emu_instance_lock);

	return (count);
}

/*
 * Get list of visible instance IDs for current thread
 * Returns number of instances filled in the array, or error code
 */
int
emu_instance_list(uint64_t *inst_ids, int max_count)
{
	struct emu_instance *inst;
	struct thread *td;
	struct ucred *cred;
	int count;

	td = curthread;
	cred = td->td_ucred;

	if (inst_ids == NULL || max_count <= 0)
		return (EINVAL);

	mtx_lock(&emu_instance_lock);
	count = 0;
	TAILQ_FOREACH(inst, &emu_instances, inst_link) {
		if (count >= max_count)
			break;
		if (emu_instance_cansee(cred, inst)) {
			inst_ids[count] = inst->inst_id;
			count++;
		}
	}
	mtx_unlock(&emu_instance_lock);

	return (count);
}

/*
 * Initialize instance management subsystem
 */
void
emu_instance_init(void)
{
	mtx_init(&emu_instance_lock, "emu instance lock", NULL, MTX_DEF);
	TAILQ_INIT(&emu_instances);
	TAILQ_INIT(&emu_user_limits);
	emu_next_instance_id = 1;

	printf("emu: instance management initialized (max %d instances, %d per user)\n",
	    emu_max_instances, emu_max_instances_per_user);
}

/*
 * Destroy instance management subsystem
 */
void
emu_instance_destroy(void)
{
	struct emu_instance *inst;
	struct emu_user_limits *ul;

	mtx_lock(&emu_instance_lock);

	/* Free all instances */
	while ((inst = TAILQ_FIRST(&emu_instances)) != NULL) {
		TAILQ_REMOVE(&emu_instances, inst, inst_link);
		free(inst, M_EMU);
	}

	/* Free all user limits */
	while ((ul = TAILQ_FIRST(&emu_user_limits)) != NULL) {
		TAILQ_REMOVE(&emu_user_limits, ul, ul_link);
		free(ul, M_EMU);
	}

	mtx_unlock(&emu_instance_lock);
	mtx_destroy(&emu_instance_lock);

	printf("emu: instance management destroyed\n");
}

/*
 * Sysctl handlers for resource limits
 */
static int
sysctl_emu_max_instances(SYSCTL_HANDLER_ARGS)
{
	int error;
	int newval;

	newval = emu_max_instances;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 1 || newval > MAXEMUINSTANCES)
		return (EINVAL);

	emu_max_instances = newval;

	return (0);
}

static int
sysctl_emu_max_instances_per_user(SYSCTL_HANDLER_ARGS)
{
	int error;
	int newval;

	newval = emu_max_instances_per_user;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 1 || newval > emu_max_instances)
		return (EINVAL);

	emu_max_instances_per_user = newval;

	return (0);
}

static int
sysctl_emu_max_memory_per_instance(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint64_t newval;

	newval = emu_max_memory_per_instance;
	error = sysctl_handle_64(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < (128 * 1024 * 1024)) /* Minimum 128MB */
		return (EINVAL);

	emu_max_memory_per_instance = newval;

	return (0);
}

static int
sysctl_emu_max_cpu_time_per_instance(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint64_t newval;

	newval = emu_max_cpu_time_per_instance;
	error = sysctl_handle_64(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 60) /* Minimum 60 seconds */
		return (EINVAL);

	emu_max_cpu_time_per_instance = newval;

	return (0);
}

SYSCTL_NODE(_kern, OID_AUTO, emulation, CTLFLAG_RD, 0, "Emulation Framework");

SYSCTL_PROC(_kern_emulation, OID_AUTO, max_instances, CTLTYPE_INT | CTLFLAG_RW,
    &emu_max_instances, 0, sysctl_emu_max_instances, "I",
    "Maximum number of concurrent emulation instances");

SYSCTL_PROC(_kern_emulation, OID_AUTO, max_instances_per_user, CTLTYPE_INT | CTLFLAG_RW,
    &emu_max_instances_per_user, 0, sysctl_emu_max_instances_per_user, "I",
    "Maximum number of instances per user");

SYSCTL_PROC(_kern_emulation, OID_AUTO, max_memory_per_instance, CTLTYPE_U64 | CTLFLAG_RW,
    &emu_max_memory_per_instance, 0, sysctl_emu_max_memory_per_instance, "QU",
    "Maximum memory per instance in bytes");

SYSCTL_PROC(_kern_emulation, OID_AUTO, max_cpu_time_per_instance, CTLTYPE_U64 | CTLFLAG_RW,
    &emu_max_cpu_time_per_instance, 0, sysctl_emu_max_cpu_time_per_instance, "QU",
    "Maximum CPU time per instance in seconds");
