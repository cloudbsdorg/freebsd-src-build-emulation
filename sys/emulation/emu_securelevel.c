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
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/ucred.h>
#include <sys/jail.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include "emu.h"
#include "emu_audit.h"
#include "emu_securelevel.h"

/*
 * Forward declaration of sysctl node from emu_main.c
 */
SYSCTL_DECL(_kern_emulation);

/*
 * Emulation Framework Securelevel Integration
 *
 * This file implements securelevel-aware operation restrictions for the
 * emulation framework, including:
 * - Securelevel check function that respects jail securelevels
 * - Restricted operation tracking and auditing
 * - Sysctl controls for securelevel-based restrictions
 *
 * The securelevel mechanism prevents certain operations when the system
 * or jail securelevel is raised, providing additional security hardening.
 */

/*
 * Global: kern.emulation.securelevel_restrictions
 * Controls whether securelevel restrictions are enforced (default: 1)
 */
static int emu_securelevel_restrictions = 1;
SYSCTL_INT(_kern_emulation, OID_AUTO, securelevel_restrictions, CTLFLAG_RW,
    &emu_securelevel_restrictions, 1,
    "Enable securelevel-based operation restrictions");

/*
 * Check if the current securelevel restricts an operation
 *
 * This function checks both the system securelevel and the jail securelevel
 * (if running in a jail) to determine if an operation should be restricted.
 *
 * Parameters:
 *   td    - Thread requesting the operation
 *   level - Securelevel threshold (0-3)
 *
 * Returns:
 *   0 if the operation is allowed
 *   EPERM if the securelevel restricts the operation
 *
 * The securelevel levels are:
 *   0 - All operations allowed
 *   1 - Kernel modules cannot be unloaded, /dev/mem restricted
 *   2 - /dev/mem and /dev/kmem restricted, raw sockets disabled
 *   3 - All of above, plus immutable flags on system files
 */
int
emu_securelevel_check(struct thread *td, int level)
{
	struct ucred *cred;
	int error;

	if (!emu_securelevel_restrictions)
		return (0);

	cred = td->td_ucred;

	/* Check jail securelevel if running in a jail */
	if (jailed(cred)) {
		error = securelevel_gt(cred, level);
		if (error != 0) {
			emu_audit_log(EMU_AUDIT_EVENT_PERM_DENIED, EMU_AUDIT_SEVERITY_WARNING,
			    "securelevel_check", "jail securelevel %d > requested level %d",
			    cred->cr_prison->pr_securelevel, level);
			return (error);
		}
		return (0);
	}

	/* Check system securelevel */
	error = securelevel_gt(cred, level);
	if (error != 0) {
		emu_audit_log(EMU_AUDIT_EVENT_PERM_DENIED, EMU_AUDIT_SEVERITY_WARNING,
		    "securelevel_check", "system securelevel > requested level %d", level);
		return (error);
	}

	return (0);
}

/*
 * Check if a restricted operation is allowed at current securelevel
 *
 * This function provides a convenient wrapper for checking common
 * restricted operations with appropriate securelevel thresholds.
 *
 * Parameters:
 *   td  - Thread requesting the operation
 *   op  - Operation name (for auditing)
 *
 * Returns:
 *   0 if the operation is allowed
 *   EPERM if the securelevel restricts the operation
 *
 * Restricted operations and their securelevel thresholds:
 *   - module_unload: level 1 (cannot unload modules when securelevel > 0)
 *   - instance_create: level 0 (restricted at securelevel > 0)
 *   - instance_destroy: level 0 (restricted at securelevel > 0)
 *   - memory_write: level 1 (restricted at securelevel > 1)
 *   - debug_attach: level 1 (restricted at securelevel > 1)
 */
int
emu_securelevel_restricted_op(struct thread *td, const char *op)
{
	int level;

	if (!emu_securelevel_restrictions)
		return (0);

	/* Determine securelevel threshold based on operation */
	if (strcmp(op, "module_unload") == 0) {
		level = 0;  /* Cannot unload when securelevel > 0 */
	} else if (strcmp(op, "instance_create") == 0 ||
		   strcmp(op, "instance_destroy") == 0) {
		level = 0;  /* Restricted at securelevel > 0 */
	} else if (strcmp(op, "memory_write") == 0 ||
		   strcmp(op, "debug_attach") == 0) {
		level = 1;  /* Restricted at securelevel > 1 */
	} else {
		/* Default to level 0 for unknown operations */
		level = 0;
	}

	return (emu_securelevel_check(td, level));
}
