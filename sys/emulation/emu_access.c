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

#include "emu.h"
#include "emu_audit.h"

/*
 * Emulation Framework Access Control
 *
 * This file implements the access control model for the emulation framework,
 * including:
 * - Privilege checks for emulation operations
 * - Per-instance ownership validation
 * - Group-based delegation (GID_EMU)
 * - Non-root access control via sysctl
 */

/*
 * Global: kern.emulation.allow_nonroot
 * Defined in emu_sysctl.c
 */
extern int emu_allow_nonroot;

/*
 * Check if thread has a specific emulation privilege
 *
 * Returns 0 if privilege is granted, error code otherwise.
 */
int
emu_check_priv(struct thread *td, int priv)
{
	/* Root always has all privileges */
	if (priv_check(td, 0) == 0)
		return (0);

	/* Check for specific emulation privilege */
	if (priv_check(td, priv) == 0)
		return (0);

	return (EPERM);
}

/*
 * Check if thread can access an instance with a specific permission
 *
 * This function implements the following access control logic:
 * 1. Root can always access all instances
 * 2. If allow_nonroot is 0, only root can access
 * 3. Users in GID_EMU group can access if allow_nonroot is 1
 * 4. Instance owner (matching UID) can access their own instance
 * 5. Jailed processes with PR_ALLOW_EMULATION can access
 *
 * Returns 0 if access is granted, error code otherwise.
 */
int
emu_check_access(struct thread *td, uint64_t inst_id, int perm)
{
	struct ucred *cred;
	uid_t uid;

	cred = td->td_ucred;
	uid = cred->cr_uid;

	/* Root can always access */
	if (uid == 0)
		return (0);

	/* Check if non-root access is allowed */
	if (!emu_allow_nonroot) {
		/* Non-root access denied */
		AUDIT_PERM_DENIED("instance_access", "non-root access denied by allow_nonroot");
		return (EPERM);
	}

	/* Check if jailed with emulation permission */
	if (jailed(cred) && prison_emulation_allowed(cred))
		return (0);

	/* Check if user is in emu group */
	if (groupmember(GID_EMU, cred))
		return (0);

	/* Check if user owns the instance */
	if (emu_check_instance_ownership(td, inst_id) == 0)
		return (0);

	/* Access denied */
	AUDIT_PERM_DENIED("instance_access", "not owner, not in emu group, not root");
	return (EPERM);
}

/*
 * Check if thread can create a new instance
 *
 * Returns 0 if creation is allowed, error code otherwise.
 */
int
emu_check_create(struct thread *td)
{
	struct ucred *cred;
	uid_t uid;

	cred = td->td_ucred;
	uid = cred->cr_uid;

	/* Root can always create */
	if (uid == 0)
		return (0);

	/* Check if non-root access is allowed */
	if (!emu_allow_nonroot) {
		AUDIT_PERM_DENIED("instance_create", "non-root access denied by allow_nonroot");
		return (EPERM);
	}

	/* Check for PRIV_EMU_CREATE privilege */
	if (priv_check(td, PRIV_EMU_CREATE) == 0)
		return (0);

	/* Check if jailed with emulation permission */
	if (jailed(cred) && prison_emulation_allowed(cred))
		return (0);

	/* Check if user is in emu group */
	if (groupmember(GID_EMU, cred) == 0)
		return (0);

	AUDIT_PERM_DENIED("instance_create", "not root, no PRIV_EMU_CREATE, not in emu group");
	return (EPERM);
}

/*
 * Check if thread can destroy an instance
 *
 * Returns 0 if destruction is allowed, error code otherwise.
 */
int
emu_check_destroy(struct thread *td, uint64_t inst_id)
{
	struct ucred *cred;
	uid_t uid;
	int error;

	cred = td->td_ucred;
	uid = cred->cr_uid;

	/* Root can always destroy */
	if (uid == 0)
		return (0);

	/* Check for PRIV_EMU_DESTROY privilege */
	if (priv_check(td, PRIV_EMU_DESTROY) == 0)
		return (0);

	/* Check if jailed with emulation permission */
	if (jailed(cred) && prison_emulation_allowed(cred))
		return (0);

	/* Check instance ownership */
	error = emu_check_instance_ownership(td, inst_id);
	if (error == 0) {
		/* Owner can destroy their own instance */
		return (0);
	}

	AUDIT_PERM_DENIED("instance_destroy", "not root, no PRIV_EMU_DESTROY, not owner");
	return (EPERM);
}
