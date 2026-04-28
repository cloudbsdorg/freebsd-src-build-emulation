/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 * Copyright (c) 2026 JetBrains s.r.o.
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
 * Emulation Framework Memory Management
 *
 * This module provides memory policy sysctls and overcommit safety
 * mechanisms for the emulation framework.
 *
 * Features:
 * - memory_policy: Select memory allocation strategy (prealloc/demand)
 * - memory_overcommit: Enable/disable memory overcommitment
 * - memory_warn_percent: Warning threshold for overcommit
 * - memory_balloon_min_pct: Minimum balloon target percentage
 * - memory_balloon_interval: Balloon adjustment interval (seconds)
 * - memory_system_reserve_percent: Safety margin for system memory
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <vm/vm.h>
#include <vm/vm_param.h>

#include "emu.h"
#include "emu_memmgmt.h"
#include "emu_securelevel.h"
#include <sys/syslog.h>

/* Default values */
#define EMU_MEM_POLICY_DEFAULT		EMU_MEM_POLICY_PREALLOC
#define EMU_MEM_OVERCOMMIT_DEFAULT	0	/* Disabled by default */
#define EMU_MEM_WARN_PERCENT_DEFAULT	80	/* Warn at 80% */
#define EMU_MEM_BALLOON_MIN_PCT_DEFAULT	50	/* Minimum 50% of allocated */
#define EMU_MEM_BALLOON_INTERVAL_DEFAULT 30	/* 30 seconds */
#define EMU_MEM_SYSTEM_RESERVE_DEFAULT	10	/* 10% reserve for system */

/* Global memory management state */
struct emu_memmgmt_state {
	int			mms_policy;
	int			mms_overcommit;
	int			mms_warn_percent;
	int			mms_balloon_min_pct;
	int			mms_balloon_interval;
	int			mms_system_reserve_pct;
	struct mtx		mms_lock;
} emu_memmgmt;

/* Sysctl node */
static struct sysctl_ctx_list emu_memmgmt_ctx;
static struct sysctl_oid *emu_memmgmt_oid;

/*
 * Calculate total physical memory
 */
static uint64_t
emu_memmgmt_total_physmem(void)
{
	uint64_t total;

	total = (uint64_t)vm_cnt.v_page_count * PAGE_SIZE;
	return (total);
}

/*
 * Calculate system-wide used memory (OS + all processes)
 * This is an approximation based on active, wired, and cache pages
 */
static uint64_t
emu_memmgmt_system_used(void)
{
	uint64_t used;

	/*
	 * Calculate used memory from VM statistics:
	 * - Active pages: in use by processes
	 * - Wired pages: locked in memory
	 * - Inactive pages: recently used, may be reclaimed
	 * We don't count free or cache pages as "used"
	 */
	used = (uint64_t)(vm_cnt.v_active_count + vm_cnt.v_wire_count) * PAGE_SIZE;
	return (used);
}

/*
 * Calculate available memory for new instances
 * Formula: total_phys - system_used - instances_consumed - safety_reserve
 */
static uint64_t
emu_memmgmt_available_memory(void)
{
	uint64_t total, used, reserve, instances_consumed;
	int i;

	total = emu_memmgmt_total_physmem();
	used = emu_memmgmt_system_used();

	/* Calculate safety reserve */
	reserve = (total * emu_memmgmt.mms_system_reserve_pct) / 100;

	/* Get memory consumed by all emulation instances */
	instances_consumed = emu_instance_total_memory();

	/* Available = total - used - instances - reserve */
	if (used + instances_consumed + reserve > total)
		return (0);

	return (total - used - instances_consumed - reserve);
}

/*
 * Check if memory allocation would exceed overcommit threshold
 * Returns 0 if OK, -1 if would exceed threshold
 */
int
emu_memmgmt_check_overcommit(uint64_t requested_memory)
{
	uint64_t total, available, threshold;
	uint64_t total_allocated;

	if (!emu_memmgmt.mms_overcommit)
		return (0);

	total = emu_memmgmt_total_physmem();
	available = emu_memmgmt_available_memory();
	total_allocated = emu_instance_total_memory() + requested_memory;

	/* Calculate warning threshold */
	threshold = (total * emu_memmgmt.mms_warn_percent) / 100;

	if (total_allocated > threshold) {
		/* Log warning */
		printf("kern.emulation: WARNING: Memory overcommit threshold exceeded\n");
		printf("  Total physical: %llu MB\n", (unsigned long long)(total / (1024 * 1024)));
		printf("  System used: %llu MB\n", (unsigned long long)(emu_memmgmt_system_used() / (1024 * 1024)));
		printf("  Instances consumed: %llu MB\n", (unsigned long long)(emu_instance_total_memory() / (1024 * 1024)));
		printf("  Safety reserve: %d%%\n", emu_memmgmt.mms_system_reserve_pct);
		printf("  Available: %llu MB\n", (unsigned long long)(available / (1024 * 1024)));
		printf("  Requested: %llu MB\n", (unsigned long long)(requested_memory / (1024 * 1024)));
		printf("  Total allocated would be: %llu MB (%d%% of physical)\n",
		    (unsigned long long)(total_allocated / (1024 * 1024)),
		    (int)((total_allocated * 100) / total));

		/* Still allow if under hard limit, just warn */
		if (total_allocated > total) {
			printf("kern.emulation: REJECTED: Would exceed physical memory\n");
			return (-1);
		}
	}

	return (0);
}

/*
 * Get current memory policy
 */
int
emu_memmgmt_get_policy(void)
{
	return (emu_memmgmt.mms_policy);
}

/*
 * Get balloon minimum percentage
 */
int
emu_memmgmt_get_balloon_min_pct(void)
{
	return (emu_memmgmt.mms_balloon_min_pct);
}

/*
 * Get balloon interval in seconds
 */
int
emu_memmgmt_get_balloon_interval(void)
{
	return (emu_memmgmt.mms_balloon_interval);
}

/*
 * Sysctl handlers
 */
static int
emu_memmgmt_sysctl_policy(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_policy;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval != EMU_MEM_POLICY_PREALLOC && newval != EMU_MEM_POLICY_DEMAND)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_policy = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_overcommit(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_overcommit;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 0 || newval > 1)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_overcommit = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_warn_percent(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_warn_percent;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 50 || newval > 100)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_warn_percent = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_balloon_min_pct(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_balloon_min_pct;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 10 || newval > 90)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_balloon_min_pct = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_balloon_interval(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_balloon_interval;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 5 || newval > 300)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_balloon_interval = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_system_reserve(SYSCTL_HANDLER_ARGS)
{
	int error, newval;

	newval = emu_memmgmt.mms_system_reserve_pct;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (newval < 5 || newval > 50)
		return (EINVAL);

	/* Check securelevel restrictions (S9.2) */
	if (req->newptr != NULL) {
		error = emu_securelevel_restricted_op(curthread, "sysctl_write");
		if (error != 0) {
			log(LOG_WARNING, "emu: sysctl write restricted by securelevel\n");
			return (error);
		}
	}

	mtx_lock(&emu_memmgmt.mms_lock);
	emu_memmgmt.mms_system_reserve_pct = newval;
	mtx_unlock(&emu_memmgmt.mms_lock);

	return (0);
}

static int
emu_memmgmt_sysctl_available(SYSCTL_HANDLER_ARGS)
{
	uint64_t available;
	int error;

	available = emu_memmgmt_available_memory();
	error = sysctl_handle_64(oidp, &available, 0, req);
	return (error);
}

static int
emu_memmgmt_sysctl_total(SYSCTL_HANDLER_ARGS)
{
	uint64_t total;
	int error;

	total = emu_memmgmt_total_physmem();
	error = sysctl_handle_64(oidp, &total, 0, req);
	return (error);
}

static int
emu_memmgmt_sysctl_system_used(SYSCTL_HANDLER_ARGS)
{
	uint64_t used;
	int error;

	used = emu_memmgmt_system_used();
	error = sysctl_handle_64(oidp, &used, 0, req);
	return (error);
}

/*
 * Initialize memory management subsystem
 */
void
emu_memmgmt_init(void)
{

	mtx_init(&emu_memmgmt.mms_lock, "emu_memmgmt", NULL, MTX_DEF);

	/* Set defaults */
	emu_memmgmt.mms_policy = EMU_MEM_POLICY_DEFAULT;
	emu_memmgmt.mms_overcommit = EMU_MEM_OVERCOMMIT_DEFAULT;
	emu_memmgmt.mms_warn_percent = EMU_MEM_WARN_PERCENT_DEFAULT;
	emu_memmgmt.mms_balloon_min_pct = EMU_MEM_BALLOON_MIN_PCT_DEFAULT;
	emu_memmgmt.mms_balloon_interval = EMU_MEM_BALLOON_INTERVAL_DEFAULT;
	emu_memmgmt.mms_system_reserve_pct = EMU_MEM_SYSTEM_RESERVE_DEFAULT;

	/* Initialize sysctl context */
	SYSCTL_CTX_INIT(&emu_memmgmt_ctx);

	/* Create sysctl tree: kern.emulation.memory.* */
	emu_memmgmt_oid = SYSCTL_ADD_NODE(&emu_memmgmt_ctx,
	    SYSCTL_STATIC_CHILDREN(_kern_emulation), OID_AUTO, "memory",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "Emulation memory management");

	/* memory_policy sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "policy", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_policy, 0, emu_memmgmt_sysctl_policy, "I",
	    "Memory allocation policy (0=prealloc, 1=demand)");

	/* memory_overcommit sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "overcommit", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_overcommit, 0, emu_memmgmt_sysctl_overcommit, "I",
	    "Enable memory overcommit (0=disabled, 1=enabled)");

	/* memory_warn_percent sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "warn_percent", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_warn_percent, 0, emu_memmgmt_sysctl_warn_percent, "I",
	    "Warning threshold for overcommit (50-100%)");

	/* memory_balloon_min_pct sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "balloon_min_pct", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_balloon_min_pct, 0, emu_memmgmt_sysctl_balloon_min_pct, "I",
	    "Minimum balloon target percentage (10-90%)");

	/* memory_balloon_interval sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "balloon_interval", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_balloon_interval, 0, emu_memmgmt_sysctl_balloon_interval, "I",
	    "Balloon adjustment interval in seconds (5-300)");

	/* memory_system_reserve_percent sysctl */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "system_reserve_percent", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_memmgmt.mms_system_reserve_pct, 0, emu_memmgmt_sysctl_system_reserve, "I",
	    "System memory reserve percentage (5-50%)");

	/* Read-only sysctls for monitoring */
	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "available", CTLTYPE_U64 | CTLFLAG_RD,
	    NULL, 0, emu_memmgmt_sysctl_available, "QU",
	    "Available memory for new instances (bytes)");

	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "total", CTLTYPE_U64 | CTLFLAG_RD,
	    NULL, 0, emu_memmgmt_sysctl_total, "QU",
	    "Total physical memory (bytes)");

	SYSCTL_ADD_PROC(&emu_memmgmt_ctx, SYSCTL_CHILDREN(emu_memmgmt_oid),
	    OID_AUTO, "system_used", CTLTYPE_U64 | CTLFLAG_RD,
	    NULL, 0, emu_memmgmt_sysctl_system_used, "QU",
	    "System-wide used memory (bytes)");
}

/*
 * Destroy memory management subsystem
 */
void
emu_memmgmt_destroy(void)
{

	mtx_destroy(&emu_memmgmt.mms_lock);
	SYSCTL_CTX_FREE(&emu_memmgmt_ctx);
}
