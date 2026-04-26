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
 * AMD64 Architecture-Specific Emulation Module
 *
 * This module provides AMD64-specific CPU emulation handlers and
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
 * AMD64-specific CPU state structure
 */
struct amd64_cpu_state {
	uint64_t	rax;
	uint64_t	rbx;
	uint64_t	rcx;
	uint64_t	rdx;
	uint64_t	rsi;
	uint64_t	rdi;
	uint64_t	rbp;
	uint64_t	rsp;
	uint64_t	r8;
	uint64_t	r9;
	uint64_t	r10;
	uint64_t	r11;
	uint64_t	r12;
	uint64_t	r13;
	uint64_t	r14;
	uint64_t	r15;
	uint64_t	rip;
	uint64_t	rflags;
	uint64_t	cs;
	uint64_t	ss;
	uint64_t	ds;
	uint64_t	es;
	uint64_t	fs;
	uint64_t	gs;
};

/*
 * AMD64 module instance count
 */
static int amd64_instance_count = 0;
static struct mtx amd64_instance_lock;

/*
 * Register an AMD64 instance
 * Returns 0 on success, error code on failure
 */
int
amd64_instance_register(void)
{
	int error;

	mtx_lock(&amd64_instance_lock);
	if (amd64_instance_count >= MAXEMUINSTANCES) {
		mtx_unlock(&amd64_instance_lock);
		return (ENOSPC);
	}
	amd64_instance_count++;
	emu_module_refcount_inc("emu_amd64");
	mtx_unlock(&amd64_instance_lock);

	return (0);
}

/*
 * Deregister an AMD64 instance
 */
void
amd64_instance_deregister(void)
{
	mtx_lock(&amd64_instance_lock);
	if (amd64_instance_count > 0) {
		amd64_instance_count--;
		emu_module_refcount_dec("emu_amd64");
	}
	mtx_unlock(&amd64_instance_lock);
}

/*
 * Get current AMD64 instance count
 */
int
amd64_get_instance_count(void)
{
	int count;

	mtx_lock(&amd64_instance_lock);
	count = amd64_instance_count;
	mtx_unlock(&amd64_instance_lock);

	return (count);
}

/*
 * Initialize AMD64 CPU state
 */
void
amd64_cpu_state_init(struct amd64_cpu_state *state)
{
	bzero(state, sizeof(*state));
	/* Set initial RFLAGS with reserved bits set */
	state->rflags = 0x2;
}

/*
 * AMD64 module event handler
 */
static int
amd64_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&amd64_instance_lock, "amd64 instance lock",
		    NULL, MTX_DEF);
		amd64_instance_count = 0;
		emu_sysctl_register_module("emu_amd64");
		error = emu_module_register("emu_amd64", 1);
		if (error != 0) {
			mtx_destroy(&amd64_instance_lock);
			return (error);
		}
		break;

	case MOD_UNLOAD:
		/* Refuse to unload if there are active instances */
		if (amd64_instance_count > 0) {
			printf("emu_amd64: cannot unload, %d active instances\n",
			    amd64_instance_count);
			return (EBUSY);
		}
		emu_module_deregister("emu_amd64");
		mtx_destroy(&amd64_instance_lock);
		break;

	case MOD_SHUTDOWN:
		/* Handle system shutdown */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t amd64_mod = {
	"emu_amd64",
	amd64_modevent,
	NULL
};

DECLARE_MODULE(emu_amd64, amd64_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_DEPEND(emu_amd64, emu_core, 1, 1, 1);
MODULE_VERSION(emu_amd64, 1);
