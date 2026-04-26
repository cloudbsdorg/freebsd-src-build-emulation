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
 * ARM Architecture-Specific Emulation Module
 *
 * This module provides ARM-specific CPU emulation handlers and
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
 * ARM-specific CPU state structure (32-bit)
 */
struct arm_cpu_state {
	uint32_t	r[13];		/* General purpose registers r0-r12 */
	uint32_t	sp;		/* Stack pointer (r13) */
	uint32_t	lr;		/* Link register (r14) */
	uint32_t	pc;		/* Program counter */
	uint32_t	cpsr;		/* Current program state register */
};

/*
 * ARM module instance count
 */
static int arm_instance_count = 0;
static struct mtx arm_instance_lock;

/*
 * Register an ARM instance
 * Returns 0 on success, error code on failure
 */
int
arm_instance_register(void)
{
	int error;

	mtx_lock(&arm_instance_lock);
	if (arm_instance_count >= MAXEMUINSTANCES) {
		mtx_unlock(&arm_instance_lock);
		return (ENOSPC);
	}
	arm_instance_count++;
	emu_module_refcount_inc("emu_arm");
	mtx_unlock(&arm_instance_lock);

	return (0);
}

/*
 * Deregister an ARM instance
 */
void
arm_instance_deregister(void)
{
	mtx_lock(&arm_instance_lock);
	if (arm_instance_count > 0) {
		arm_instance_count--;
		emu_module_refcount_dec("emu_arm");
	}
	mtx_unlock(&arm_instance_lock);
}

/*
 * Get current ARM instance count
 */
int
arm_get_instance_count(void)
{
	int count;

	mtx_lock(&arm_instance_lock);
	count = arm_instance_count;
	mtx_unlock(&arm_instance_lock);

	return (count);
}

/*
 * Initialize ARM CPU state
 */
void
arm_cpu_state_init(struct arm_cpu_state *state)
{
	bzero(state, sizeof(*state));
	/* Set initial CPSR with interrupt mask bits */
	state->cpsr = 0xD3;
}

/*
 * ARM module event handler
 */
static int
arm_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&arm_instance_lock, "arm instance lock",
		    NULL, MTX_DEF);
		arm_instance_count = 0;
		emu_sysctl_register_module("emu_arm");
		error = emu_module_register("emu_arm", 1);
		if (error != 0) {
			mtx_destroy(&arm_instance_lock);
			return (error);
		}
		break;

	case MOD_UNLOAD:
		/* Refuse to unload if there are active instances */
		if (arm_instance_count > 0) {
			printf("emu_arm: cannot unload, %d active instances\n",
			    arm_instance_count);
			return (EBUSY);
		}
		emu_module_deregister("emu_arm");
		mtx_destroy(&arm_instance_lock);
		break;

	case MOD_SHUTDOWN:
		/* Handle system shutdown */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t arm_mod = {
	"emu_arm",
	arm_modevent,
	NULL
};

DECLARE_MODULE(emu_arm, arm_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_DEPEND(emu_arm, emu_core, 1, 1, 1);
MODULE_VERSION(emu_arm, 1);
