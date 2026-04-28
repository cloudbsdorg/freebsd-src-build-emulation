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
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/ucred.h>
#include <sys/syslog.h>
#include "emu.h"
#include "emu_securelevel.h"

MALLOC_DEFINE(M_EMU, "emu", "Emulation framework memory");

/* Forward declarations */
void emu_sysctl_init(void);
void emu_sysctl_destroy(void);
void emu_instance_init(void);
void emu_instance_cleanup(void);
void emu_stack_init(void);
void emu_stack_destroy(void);
int emu_audit_init(void);
void emu_audit_destroy(void);

/*
 * Emulation Framework Core Module (emu_core.ko)
 *
 * This module provides the core infrastructure for the kernel emulation
 * framework, including:
 * - Instance registry and reference counting
 * - Sysctl interface for configuration and monitoring
 * - Devfs interface for userland interaction
 * - Module dependency management
 */

/* Module version for dependency checking */
#define EMU_CORE_VERSION	1
MODULE_VERSION(emu_core, EMU_CORE_VERSION);

/*
 * Module dependencies - architecture modules depend on emu_core
 * These are declared in the arch modules, not here
 * (emu_amd64, emu_i386, etc. will declare MODULE_DEPEND on emu_core)
 */

/*
 * Instance Registry
 */
static struct mtx emu_instance_lock;
static int emu_num_instances = 0;

/*
 * Sysctl OID tree for emulation framework
 */
static SYSCTL_NODE(_kern, OID_AUTO, emulation, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "Emulation Framework");

/*
 * kern.emulation.instance_count - Number of active emulation instances
 */
SYSCTL_INT(_kern_emulation, OID_AUTO, instance_count, CTLFLAG_RD,
    &emu_num_instances, 0, "Number of active emulation instances");

/*
 * emu_get_instance_count() - Get current instance count
 *
 * Returns the number of active emulation instances.
 * Thread-safe, uses mutex protection.
 */
int
emu_get_instance_count(void)
{
	int count;

	mtx_lock(&emu_instance_lock);
	count = emu_num_instances;
	mtx_unlock(&emu_instance_lock);

	return (count);
}

/*
 * emu_instance_register() - Register a new emulation instance
 *
 * Returns 0 on success, EBUSY if instance limit reached.
 * Thread-safe, uses mutex protection.
 */
int
emu_instance_register(void)
{
	int error;

	mtx_lock(&emu_instance_lock);
	if (emu_num_instances >= MAXEMUINSTANCES) {
		mtx_unlock(&emu_instance_lock);
		return (EBUSY);
	}
	emu_num_instances++;
	error = 0;
	mtx_unlock(&emu_instance_lock);

	return (error);
}

/*
 * emu_instance_deregister() - Deregister an emulation instance
 *
 * Decrements the instance count. Must be called when an instance
 * is destroyed.
 * Thread-safe, uses mutex protection.
 */
void
emu_instance_deregister(void)
{

	mtx_lock(&emu_instance_lock);
	if (emu_num_instances > 0)
		emu_num_instances--;
	mtx_unlock(&emu_instance_lock);
}

/*
 * emu_core_modevent() - Module event handler
 *
 * Handles MOD_LOAD, MOD_UNLOAD, and MOD_STAT events for the emu_core module.
 *
 * MOD_LOAD:
 * - Initializes instance registry mutex
 * - Creates sysctl tree
 * - Creates devfs entries
 * - Validates no conflicts with existing emulation frameworks
 *
 * MOD_UNLOAD:
 * - Refuses unload if active instances exist (returns EBUSY)
 * - Cleans up sysctl and devfs entries
 * - Destroys mutex
 *
 * Returns 0 on success, error code on failure.
 */
static int
emu_core_modevent(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		/* Initialize instance registry */
		mtx_init(&emu_instance_lock, "emu_instance", NULL, MTX_DEF);
		emu_num_instances = 0;

		/* Initialize sysctl infrastructure */
		emu_sysctl_init();

		/* Initialize instance management subsystem */
		emu_instance_init();

		/* Initialize stack capture infrastructure */
		emu_stack_init();

		/* Initialize audit logging subsystem */
		error = emu_audit_init();
		if (error != 0) {
			printf("emu_core: Failed to initialize audit logging, "
			    "error %d\n", error);
			/* Continue anyway - audit is optional */
		}

		/* Validate no conflicts */
		/* XXX: Check for conflicting emulation frameworks */

		/* Sysctl tree already created by SYSCTL_NODE */

		/* Create devfs entries */
		/* XXX: emu_devfs_init() when devfs support is implemented */

		printf("emu_core: Emulation framework core loaded, "
		    "instance limit: %d\n", MAXEMUINSTANCES);
		error = 0;
		break;

	case MOD_UNLOAD:
		/* Check securelevel restrictions (S9.2) */
		error = emu_securelevel_restricted_op(curthread, "module_unload");
		if (error != 0) {
			log(LOG_WARNING, "emu: module unload restricted by securelevel\n");
			return (error);
		}

		/* Refuse unload if active instances */
		mtx_lock(&emu_instance_lock);
		if (emu_num_instances > 0) {
			mtx_unlock(&emu_instance_lock);
			printf("emu_core: Cannot unload with %d active "
			    "instance(s)\n", emu_get_instance_count());
			return (EBUSY);
		}
		mtx_unlock(&emu_instance_lock);

		/* Clean up instance management subsystem */
		/* Note: instances should be destroyed by userland before unload */
		emu_instance_cleanup();

		/* Clean up stack capture infrastructure */
		emu_stack_destroy();

		/* Clean up audit logging subsystem */
		emu_audit_destroy();

		/* Clean up sysctl infrastructure */
		emu_sysctl_destroy();

		/* Clean up devfs entries */
		/* XXX: emu_devfs_destroy() when devfs support is implemented */

		/* Destroy mutex */
		mtx_destroy(&emu_instance_lock);

		printf("emu_core: Emulation framework core unloaded\n");
		error = 0;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/* Module declaration */
static moduledata_t emu_core_mod = {
	"emu_core",
	emu_core_modevent,
	NULL
};

DECLARE_MODULE(emu_core, emu_core_mod, SI_SUB_KLD, SI_ORDER_ANY);
