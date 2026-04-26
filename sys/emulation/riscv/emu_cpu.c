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
 * RISC-V Architecture-Specific Emulation Module
 *
 * This module provides RISC-V-specific CPU emulation handlers and
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
 * RISC-V-specific CPU state structure
 */
struct riscv_cpu_state {
	uint64_t	x[32];		/* General purpose registers x0-x31 */
	uint64_t	pc;		/* Program counter */
	uint64_t	mstatus;	/* Machine status register */
	uint64_t	mie;		/* Machine interrupt-enable register */
	uint64_t	mip;		/* Machine interrupt-pending register */
	uint64_t	mtvec;		/* Machine trap-handler base address */
	uint64_t	mscratch;	/* Machine scratch register */
	uint64_t	mepc;		/* Machine exception program counter */
	uint64_t	mcause;		/* Machine exception cause */
	uint64_t	mtval;		/* Machine trap value */
};

/*
 * RISC-V module instance count
 */
static int riscv_instance_count = 0;
static struct mtx riscv_instance_lock;

/*
 * Register a RISC-V instance
 * Returns 0 on success, error code on failure
 */
int
riscv_instance_register(void)
{
	int error;

	mtx_lock(&riscv_instance_lock);
	if (riscv_instance_count >= MAXEMUINSTANCES) {
		mtx_unlock(&riscv_instance_lock);
		return (ENOSPC);
	}
	riscv_instance_count++;
	emu_module_refcount_inc("emu_riscv");
	mtx_unlock(&riscv_instance_lock);

	return (0);
}

/*
 * Deregister a RISC-V instance
 */
void
riscv_instance_deregister(void)
{
	mtx_lock(&riscv_instance_lock);
	if (riscv_instance_count > 0) {
		riscv_instance_count--;
		emu_module_refcount_dec("emu_riscv");
	}
	mtx_unlock(&riscv_instance_lock);
}

/*
 * Get current RISC-V instance count
 */
int
riscv_get_instance_count(void)
{
	int count;

	mtx_lock(&riscv_instance_lock);
	count = riscv_instance_count;
	mtx_unlock(&riscv_instance_lock);

	return (count);
}

/*
 * Initialize RISC-V CPU state
 */
void
riscv_cpu_state_init(struct riscv_cpu_state *state)
{
	bzero(state, sizeof(*state));
	/* x0 is always zero (hardwired) */
	/* Set initial MSTATUS with default bits */
	state->mstatus = 0x1880;
}

/*
 * RISC-V module event handler
 */
static int
riscv_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&riscv_instance_lock, "riscv instance lock",
		    NULL, MTX_DEF);
		riscv_instance_count = 0;
		emu_sysctl_register_module("emu_riscv");
		error = emu_module_register("emu_riscv", 1);
		if (error != 0) {
			mtx_destroy(&riscv_instance_lock);
			return (error);
		}
		break;

	case MOD_UNLOAD:
		/* Refuse to unload if there are active instances */
		if (riscv_instance_count > 0) {
			printf("emu_riscv: cannot unload, %d active instances\n",
			    riscv_instance_count);
			return (EBUSY);
		}
		emu_module_deregister("emu_riscv");
		mtx_destroy(&riscv_instance_lock);
		break;

	case MOD_SHUTDOWN:
		/* Handle system shutdown */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t riscv_mod = {
	"emu_riscv",
	riscv_modevent,
	NULL
};

DECLARE_MODULE(emu_riscv, riscv_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_DEPEND(emu_riscv, emu_core, 1, 1, 1);
MODULE_VERSION(emu_riscv, 1);
