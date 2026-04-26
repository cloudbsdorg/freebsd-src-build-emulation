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
 * PowerPC Architecture-Specific Emulation Module
 *
 * This module provides PowerPC-specific CPU emulation handlers and
 * architecture-specific functionality for the kernel emulation framework.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/refcount.h>
#include <machine/cpu.h>
#include "emu.h"

/*
 * PowerPC-specific CPU state structure
 */
struct powerpc_cpu_state {
	uint32_t	gpr[32];	/* General purpose registers */
	uint32_t	pc;		/* Program counter */
	uint32_t	msr;		/* Machine state register */
	uint32_t	cr;		/* Condition register */
	uint32_t	xer;		/* Fixed-point exception register */
	uint32_t	lr;		/* Link register */
	uint32_t	ctr;		/* Count register */
};

/*
 * PowerPC module instance count
 */
static int powerpc_instance_count = 0;
static struct mtx powerpc_instance_lock;

/*
 * Register a PowerPC instance
 * Returns 0 on success, error code on failure
 */
int
powerpc_instance_register(void)
{
	int error;

	mtx_lock(&powerpc_instance_lock);
	if (powerpc_instance_count >= MAXEMUINSTANCES) {
		mtx_unlock(&powerpc_instance_lock);
		return (ENOSPC);
	}
	powerpc_instance_count++;
	emu_module_refcount_inc("emu_powerpc");
	mtx_unlock(&powerpc_instance_lock);

	return (0);
}

/*
 * Deregister a PowerPC instance
 */
void
powerpc_instance_deregister(void)
{
	mtx_lock(&powerpc_instance_lock);
	if (powerpc_instance_count > 0) {
		powerpc_instance_count--;
		emu_module_refcount_dec("emu_powerpc");
	}
	mtx_unlock(&powerpc_instance_lock);
}

/*
 * Get current PowerPC instance count
 */
int
powerpc_get_instance_count(void)
{
	int count;

	mtx_lock(&powerpc_instance_lock);
	count = powerpc_instance_count;
	mtx_unlock(&powerpc_instance_lock);

	return (count);
}

/*
 * Initialize PowerPC CPU state
 */
void
powerpc_cpu_state_init(struct powerpc_cpu_state *state)
{
	bzero(state, sizeof(*state));
	/* Set initial MSR with default bits */
	state->msr = 0x10000;
}

/*
 * PowerPC module event handler
 */
static int
powerpc_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&powerpc_instance_lock, "powerpc instance lock",
		    NULL, MTX_DEF);
		powerpc_instance_count = 0;
		emu_sysctl_register_module("emu_powerpc");
		error = emu_module_register("emu_powerpc", 1);
		if (error != 0) {
			mtx_destroy(&powerpc_instance_lock);
			return (error);
		}
		break;

	case MOD_UNLOAD:
		/* Refuse to unload if there are active instances */
		if (powerpc_instance_count > 0) {
			printf("emu_powerpc: cannot unload, %d active instances\n",
			    powerpc_instance_count);
			return (EBUSY);
		}
		emu_module_deregister("emu_powerpc");
		mtx_destroy(&powerpc_instance_lock);
		break;

	case MOD_SHUTDOWN:
		/* Handle system shutdown */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t powerpc_mod = {
	"emu_powerpc",
	powerpc_modevent,
	NULL
};

DECLARE_MODULE(emu_powerpc, powerpc_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_DEPEND(emu_powerpc, emu_core, 1, 1, 1);
MODULE_VERSION(emu_powerpc, 1);
