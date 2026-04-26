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
 * i386 Architecture-Specific Emulation Module
 *
 * This module provides i386-specific CPU emulation handlers and
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
 * i386-specific CPU state structure
 */
struct i386_cpu_state {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	ebp;
	uint32_t	esp;
	uint32_t	eip;
	uint32_t	eflags;
	uint32_t	cs;
	uint32_t	ss;
	uint32_t	ds;
	uint32_t	es;
	uint32_t	fs;
	uint32_t	gs;
};

/*
 * i386 module instance count
 */
static int i386_instance_count = 0;
static struct mtx i386_instance_lock;

/*
 * Register an i386 instance
 * Returns 0 on success, error code on failure
 */
int
i386_instance_register(void)
{
	int error;

	mtx_lock(&i386_instance_lock);
	if (i386_instance_count >= MAXEMUINSTANCES) {
		mtx_unlock(&i386_instance_lock);
		return (ENOSPC);
	}
	i386_instance_count++;
	emu_module_refcount_inc("emu_i386");
	mtx_unlock(&i386_instance_lock);

	return (0);
}

/*
 * Deregister an i386 instance
 */
void
i386_instance_deregister(void)
{
	mtx_lock(&i386_instance_lock);
	if (i386_instance_count > 0) {
		i386_instance_count--;
		emu_module_refcount_dec("emu_i386");
	}
	mtx_unlock(&i386_instance_lock);
}

/*
 * Get current i386 instance count
 */
int
i386_get_instance_count(void)
{
	int count;

	mtx_lock(&i386_instance_lock);
	count = i386_instance_count;
	mtx_unlock(&i386_instance_lock);

	return (count);
}

/*
 * Initialize i386 CPU state
 */
void
i386_cpu_state_init(struct i386_cpu_state *state)
{
	bzero(state, sizeof(*state));
	/* Set initial EFLAGS with reserved bits set */
	state->eflags = 0x2;
}

/*
 * i386 module event handler
 */
static int
i386_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&i386_instance_lock, "i386 instance lock",
		    NULL, MTX_DEF);
		i386_instance_count = 0;
		emu_sysctl_register_module("emu_i386");
		error = emu_module_register("emu_i386", 1);
		if (error != 0) {
			mtx_destroy(&i386_instance_lock);
			return (error);
		}
		break;

	case MOD_UNLOAD:
		/* Refuse to unload if there are active instances */
		if (i386_instance_count > 0) {
			printf("emu_i386: cannot unload, %d active instances\n",
			    i386_instance_count);
			return (EBUSY);
		}
		emu_module_deregister("emu_i386");
		mtx_destroy(&i386_instance_lock);
		break;

	case MOD_SHUTDOWN:
		/* Handle system shutdown */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t i386_mod = {
	"emu_i386",
	i386_modevent,
	NULL
};

DECLARE_MODULE(emu_i386, i386_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_DEPEND(emu_i386, emu_core, 1, 1, 1);
MODULE_VERSION(emu_i386, 1);
