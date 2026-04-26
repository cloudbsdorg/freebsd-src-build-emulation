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
#include <sys/priv.h>

#include "emu.h"

/*
 * Emulation Framework Sysctl Interface
 *
 * This file provides sysctl interfaces for monitoring and configuring
 * the emulation framework, including:
 * - Module listing and version information
 * - Per-module refcount monitoring
 * - Instance statistics
 */

/*
 * Module registry tracking
 */
struct emu_module_entry {
	const char *name;
	int version;
	int refcount;
	TAILQ_ENTRY(emu_module_entry) link;
};

static TAILQ_HEAD(, emu_module_entry) emu_module_list =
    TAILQ_HEAD_INITIALIZER(emu_module_list);
static struct mtx emu_module_lock;

/*
 * kern.emulation.allow_nonroot - Master switch for non-root access
 *
 * When set to 1, allows non-root users in the emu group to use
 * emulation features. When 0 (default), only root can use emulation.
 */
static int emu_allow_nonroot = 0;

static int
sysctl_emu_allow_nonroot(SYSCTL_HANDLER_ARGS)
{
	int error;
	int newval;

	newval = emu_allow_nonroot;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/* Only root can change this setting */
	if (priv_check(curthread, PRIV_ROOT) != 0)
		return (EPERM);

	emu_allow_nonroot = newval;

	return (0);
}

SYSCTL_PROC(_kern_emulation, OID_AUTO, allow_nonroot, CTLTYPE_INT | CTLFLAG_RW,
    &emu_allow_nonroot, 0, sysctl_emu_allow_nonroot, "I",
    "Allow non-root users in emu group to use emulation (default 0)");

/*
 * kern.emulation.modules_loaded - List all loaded emulation modules
 *
 * Returns a comma-separated list of loaded emulation module names.
 * Read-only sysctl.
 */
static int
sysctl_kern_emulation_modules_loaded(SYSCTL_HANDLER_ARGS)
{
	struct emu_module_entry *eme;
	struct sbuf sb;
	int error;

	sbuf_new_for_sysctl(&sb, NULL, 256, req);

	mtx_lock(&emu_module_lock);
	TAILQ_FOREACH(eme, &emu_module_list, link) {
		if (TAILQ_FIRST(&emu_module_list) != eme)
			sbuf_putc(&sb, ',');
		sbuf_printf(&sb, "%s", eme->name);
	}
	mtx_unlock(&emu_module_lock);

	error = sbuf_finish(&sb);
	if (error == 0)
		error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);

	return (error);
}

SYSCTL_PROC(_kern_emulation, OID_AUTO, modules_loaded,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_kern_emulation_modules_loaded, "A",
    "List of loaded emulation modules");

/*
 * emu_module_register() - Register a module with the emulation framework
 *
 * Called by architecture-specific modules during their MOD_LOAD to
 * register with the core framework.
 */
int
emu_module_register(const char *name, int version)
{
	struct emu_module_entry *eme;

	eme = malloc(sizeof(*eme), M_EMU, M_WAITOK | M_ZERO);
	eme->name = name;
	eme->version = version;
	eme->refcount = 0;

	mtx_lock(&emu_module_lock);
	TAILQ_INSERT_TAIL(&emu_module_list, eme, link);
	mtx_unlock(&emu_module_lock);

	return (0);
}

/*
 * emu_module_deregister() - Deregister a module from the emulation framework
 *
 * Called by architecture-specific modules during their MOD_UNLOAD.
 */
void
emu_module_deregister(const char *name)
{
	struct emu_module_entry *eme;

	mtx_lock(&emu_module_lock);
	TAILQ_FOREACH(eme, &emu_module_list, link) {
		if (strcmp(eme->name, name) == 0) {
			TAILQ_REMOVE(&emu_module_list, eme, link);
			free(eme, M_EMU);
			break;
		}
	}
	mtx_unlock(&emu_module_lock);
}

/*
 * emu_module_refcount_inc() - Increment module refcount
 */
void
emu_module_refcount_inc(const char *name)
{
	struct emu_module_entry *eme;

	mtx_lock(&emu_module_lock);
	TAILQ_FOREACH(eme, &emu_module_list, link) {
		if (strcmp(eme->name, name) == 0) {
			eme->refcount++;
			break;
		}
	}
	mtx_unlock(&emu_module_lock);
}

/*
 * emu_module_refcount_dec() - Decrement module refcount
 */
void
emu_module_refcount_dec(const char *name)
{
	struct emu_module_entry *eme;

	mtx_lock(&emu_module_lock);
	TAILQ_FOREACH(eme, &emu_module_list, link) {
		if (strcmp(eme->name, name) == 0) {
			if (eme->refcount > 0)
				eme->refcount--;
			break;
		}
	}
	mtx_unlock(&emu_module_lock);
}

/*
 * Helper function for per-module sysctls
 */
static int
sysctl_kern_emulation_module(SYSCTL_HANDLER_ARGS)
{
	struct emu_module_entry *eme;
	const char *name = arg1;
	int field = arg2;
	int val;
	char buf[32];
	int error;

	mtx_lock(&emu_module_lock);
	TAILQ_FOREACH(eme, &emu_module_list, link) {
		if (strcmp(eme->name, name) == 0) {
			switch (field) {
			case 0: /* version */
				val = eme->version;
				break;
			case 1: /* refcount */
				val = eme->refcount;
				break;
			default:
				mtx_unlock(&emu_module_lock);
				return (EINVAL);
			}
			mtx_unlock(&emu_module_lock);
			snprintf(buf, sizeof(buf), "%d", val);
			return (SYSCTL_OUT(req, buf, strlen(buf)));
		}
	}
	mtx_unlock(&emu_module_lock);

	return (ENOENT);
}

/*
 * Dynamic sysctl creation for per-module information
 * Called during module registration to create per-module sysctls
 */
void
emu_sysctl_register_module(const char *name)
{
	static struct sysctl_ctx_list ctx;
	static struct sysctl_oid_list child_list;
	static int initialized = 0;
	struct sysctl_oid *oid;

	if (!initialized) {
		SYSCTL_INIT_LIST(&child_list);
		initialized = 1;
	}

	/* Create kern.emulation.module.<name> node */
	oid = SYSCTL_ADD_NODE(NULL, SYSCTL_STATIC_CHILDREN(_kern_emulation),
	    OID_AUTO, name, CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "Module information");

	if (oid == NULL)
		return;

	/* Add version sysctl */
	SYSCTL_ADD_PROC(NULL, SYSCTL_CHILDREN(oid), OID_AUTO,
	    "version", CTLTYPE_INT | CTLFLAG_RD,
	    __DECONST(char *, name), 0,
	    sysctl_kern_emulation_module, "I", "Module version");

	/* Add refcount sysctl */
	SYSCTL_ADD_PROC(NULL, SYSCTL_CHILDREN(oid), OID_AUTO,
	    "refcount", CTLTYPE_INT | CTLFLAG_RD,
	    __DECONST(char *, name), 1,
	    sysctl_kern_emulation_module, "I", "Module reference count");
}

/*
 * emu_sysctl_init() - Initialize sysctl infrastructure
 */
void
emu_sysctl_init(void)
{

	mtx_init(&emu_module_lock, "emu_module", NULL, MTX_DEF);
}

/*
 * emu_sysctl_destroy() - Clean up sysctl infrastructure
 */
void
emu_sysctl_destroy(void)
{
	struct emu_module_entry *eme;

	mtx_lock(&emu_module_lock);
	while ((eme = TAILQ_FIRST(&emu_module_list)) != NULL) {
		TAILQ_REMOVE(&emu_module_list, eme, link);
		free(eme, M_EMU);
	}
	mtx_unlock(&emu_module_lock);

	mtx_destroy(&emu_module_lock);
}
