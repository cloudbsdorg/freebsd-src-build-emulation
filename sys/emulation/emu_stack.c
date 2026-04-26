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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include "emu.h"

/*
 * Emulation Framework Stack Capture Interface
 *
 * This file provides kernel-side stack capture functionality for
 * emulated instances. It integrates with DDB/KDB to capture stack
 * traces and provides structured output for userland tools.
 */

/*
 * Stack frame structure - architecture independent
 */
struct emu_stack_frame {
	uint64_t	pc;		/* Program counter */
	uint64_t	sp;		/* Stack pointer */
	uint64_t	fp;		/* Frame pointer */
	char		symbol[256];	/* Symbol name if resolved */
	char		module[64];	/* Module name */
};

#define	MAX_STACK_FRAMES	64

/*
 * Per-instance stack capture buffer
 */
struct emu_stack_capture {
	struct mtx		sc_lock;
	struct emu_stack_frame	sc_frames[MAX_STACK_FRAMES];
	int			sc_num_frames;
	time_t			sc_timestamp;
	char			sc_arch[16];
	bool			sc_valid;
};

static struct emu_stack_capture emu_stack_captures[MAXEMUINSTANCES];

/*
 * Initialize stack capture infrastructure
 */
void
emu_stack_init(void)
{
	for (int i = 0; i < MAXEMUINSTANCES; i++) {
		mtx_init(&emu_stack_captures[i].sc_lock, "emu stack",
		    NULL, MTX_DEF);
		emu_stack_captures[i].sc_num_frames = 0;
		emu_stack_captures[i].sc_valid = false;
	}
}

/*
 * Destroy stack capture infrastructure
 */
void
emu_stack_destroy(void)
{
	for (int i = 0; i < MAXEMUINSTANCES; i++) {
		mtx_destroy(&emu_stack_captures[i].sc_lock);
	}
}

/*
 * Capture stack trace for an instance
 *
 * This function captures the current stack trace for the given instance.
 * In a real implementation, this would interface with the emulator or
 * VMM to capture the guest kernel's stack.
 *
 * For now, this provides a stub implementation that returns a sample
 * stack trace for testing purposes.
 */
int
emu_stack_capture(uint64_t inst_id, const char *arch)
{
	struct emu_stack_capture *sc;

	if (inst_id >= MAXEMUINSTANCES)
		return (EINVAL);

	sc = &emu_stack_captures[inst_id];

	mtx_lock(&sc->sc_lock);

	/* Capture timestamp */
	sc->sc_timestamp = time_second;

	/* Set architecture */
	strlcpy(sc->sc_arch, arch, sizeof(sc->sc_arch));

	/*
	 * TODO: In a real implementation, this would:
	 * 1. Pause the emulated instance
	 * 2. Extract register state (PC, SP, FP)
	 * 3. Walk the guest kernel stack frames
	 * 4. Resolve symbols via DDB/KDB integration
	 * 5. Populate sc_frames[] with the captured data
	 *
	 * For now, we provide a sample stack trace for testing.
	 */

	/* Sample stack trace for testing */
	sc->sc_num_frames = 5;
	sc->sc_frames[0].pc = 0xffffffff81234567;
	sc->sc_frames[0].sp = 0xfffffe0012345000;
	sc->sc_frames[0].fp = 0xfffffe0012345020;
	strlcpy(sc->sc_frames[0].symbol, "kern_test_func", sizeof(sc->sc_frames[0].symbol));
	strlcpy(sc->sc_frames[0].module, "test_module", sizeof(sc->sc_frames[0].module));

	sc->sc_frames[1].pc = 0xffffffff81234500;
	sc->sc_frames[1].sp = 0xfffffe0012345040;
	sc->sc_frames[1].fp = 0xfffffe0012345060;
	strlcpy(sc->sc_frames[1].symbol, "module_init", sizeof(sc->sc_frames[1].symbol));
	strlcpy(sc->sc_frames[1].module, "kernel", sizeof(sc->sc_frames[1].module));

	sc->sc_frames[2].pc = 0xffffffff81234400;
	sc->sc_frames[2].sp = 0xfffffe0012345080;
	sc->sc_frames[2].fp = 0xfffffe00123450a0;
	strlcpy(sc->sc_frames[2].symbol, "kern_syscall", sizeof(sc->sc_frames[2].symbol));
	strlcpy(sc->sc_frames[2].module, "kernel", sizeof(sc->sc_frames[2].module));

	sc->sc_frames[3].pc = 0xffffffff81234300;
	sc->sc_frames[3].sp = 0xfffffe00123450c0;
	sc->sc_frames[3].fp = 0xfffffe00123450e0;
	strlcpy(sc->sc_frames[3].symbol, "syscall_entry", sizeof(sc->sc_frames[3].symbol));
	strlcpy(sc->sc_frames[3].module, "kernel", sizeof(sc->sc_frames[3].module));

	sc->sc_frames[4].pc = 0xffffffff81234200;
	sc->sc_frames[4].sp = 0xfffffe0012345100;
	sc->sc_frames[4].fp = 0xfffffe0012345120;
	strlcpy(sc->sc_frames[4].symbol, "fork_trampoline", sizeof(sc->sc_frames[4].symbol));
	strlcpy(sc->sc_frames[4].module, "kernel", sizeof(sc->sc_frames[4].module));

	sc->sc_valid = true;

	mtx_unlock(&sc->sc_lock);

	return (0);
}

/*
 * Get stack trace for an instance
 *
 * Returns the captured stack trace via the provided sbuf.
 */
int
emu_stack_get(uint64_t inst_id, struct sbuf *sb)
{
	struct emu_stack_capture *sc;
	int error = 0;

	if (inst_id >= MAXEMUINSTANCES)
		return (EINVAL);

	sc = &emu_stack_captures[inst_id];

	mtx_lock(&sc->sc_lock);

	if (!sc->sc_valid) {
		mtx_unlock(&sc->sc_lock);
		return (ENODATA);
	}

	/* Output stack trace in structured format */
	sbuf_printf(sb, "Stack trace for instance %lu (%s):\n",
	    (u_long)inst_id, sc->sc_arch);
	sbuf_printf(sb, "Timestamp: %ld\n", (long)sc->sc_timestamp);
	sbuf_printf(sb, "Number of frames: %d\n\n", sc->sc_num_frames);
	sbuf_printf(sb, "%-4s %-20s %-20s %-20s %-20s %s\n",
	    "Frame", "PC", "SP", "FP", "Module", "Symbol");
	sbuf_printf(sb, "%-4s %-20s %-20s %-20s %-20s %s\n",
	    "----", "-------------------", "-------------------",
	    "-------------------", "--------------------", "-------------------");

	for (int i = 0; i < sc->sc_num_frames; i++) {
		sbuf_printf(sb, "%-4d 0x%-18lx 0x%-18lx 0x%-18lx %-20s %s\n",
		    i,
		    (u_long)sc->sc_frames[i].pc,
		    (u_long)sc->sc_frames[i].sp,
		    (u_long)sc->sc_frames[i].fp,
		    sc->sc_frames[i].module[0] != '\0' ?
		        sc->sc_frames[i].module : "<unknown>",
		    sc->sc_frames[i].symbol[0] != '\0' ?
		        sc->sc_frames[i].symbol : "<unknown>");
	}

	mtx_unlock(&sc->sc_lock);

	return (error);
}

/*
 * Sysctl handler for instance stack trace
 */
static int
sysctl_emu_instance_stack(SYSCTL_HANDLER_ARGS)
{
	uint64_t inst_id;
	struct emu_stack_capture *sc;
	int error;

	/* Parse instance ID from OID */
	inst_id = arg1;

	if (inst_id >= MAXEMUINSTANCES)
		return (EINVAL);

	sc = &emu_stack_captures[inst_id];

	mtx_lock(&sc->sc_lock);

	if (!sc->sc_valid) {
		mtx_unlock(&sc->sc_lock);
		return (ENODATA);
	}

	/* Copy stack data to userland */
	error = SYSCTL_OUT(req, sc, sizeof(struct emu_stack_capture));

	mtx_unlock(&sc->sc_lock);

	return (error);
}

/*
 * Register stack capture sysctls for an instance
 */
void
emu_stack_register_instance(uint64_t inst_id)
{
	char name[64];

	if (inst_id >= MAXEMUINSTANCES)
		return;

	snprintf(name, sizeof(name), "instance.%lu.stack", (u_long)inst_id);

	SYSCTL_ADD_PROC(NULL,
	    SYSCTL_STATIC_CHILDREN(_kern_emulation),
	    OID_AUTO,
	    name,
	    CTLTYPE_STRUCT | CTLFLAG_RD,
	    (void *)(uintptr_t)inst_id,
	    0,
	    sysctl_emu_instance_stack,
	    "S,emu_stack_capture",
	    "Stack trace for emulation instance");
}
