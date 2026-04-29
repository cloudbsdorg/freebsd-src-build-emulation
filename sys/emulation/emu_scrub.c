/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * This software is developed by FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

/*
 * Memory Scrubbing for FreeBSD Kernel Emulation Framework
 *
 * This module provides secure memory scrubbing to prevent data leakage
 * between instances. When an instance is destroyed, its memory is scrubbed
 * using the configured method (zero, random, or pattern).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/random.h>

#include "emu.h"
#include "emu_scrub.h"

/* Sysctl node */
static struct sysctl_ctx_list emu_scrub_ctx;
static struct sysctl_oid *emu_scrub_oid;

/* Scrubbing methods */
#define EMU_SCRUB_METHOD_ZERO		0
#define EMU_SCRUB_METHOD_RANDOM		1
#define EMU_SCRUB_METHOD_PATTERN	2

/* Global scrubbing configuration */
struct emu_scrub_config {
	int			sc_enabled;
	int			sc_method;
	uint8_t			sc_pattern[32];
	int			sc_pattern_len;
	struct mtx		sc_lock;
} emu_scrub_cfg;

/*
 * Initialize memory scrubbing subsystem
 */
void
emu_scrub_init(void)
{

	mtx_init(&emu_scrub_cfg.sc_lock, "emu_scrub", NULL, MTX_DEF);
	emu_scrub_cfg.sc_enabled = 1;	/* Enabled by default */
	emu_scrub_cfg.sc_method = EMU_SCRUB_METHOD_ZERO;	/* Zero by default */
	emu_scrub_cfg.sc_pattern_len = 0;

	/* Initialize sysctl context */
	SYSCTL_CTX_INIT(&emu_scrub_ctx);

	/* Create sysctl tree: kern.emulation.memory.scrub.* */
	emu_scrub_oid = SYSCTL_ADD_NODE(&emu_scrub_ctx,
	    SYSCTL_STATIC_CHILDREN(_kern_emulation_memory), OID_AUTO, "scrub",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "Memory scrubbing configuration");

	/* memory.scrub.enabled sysctl */
	SYSCTL_ADD_PROC(&emu_scrub_ctx, SYSCTL_CHILDREN(emu_scrub_oid),
	    OID_AUTO, "enabled", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_scrub_cfg.sc_enabled, 0, emu_scrub_sysctl_enabled, "I",
	    "Enable memory scrubbing (0=disabled, 1=enabled)");

	/* memory.scrub.method sysctl */
	SYSCTL_ADD_PROC(&emu_scrub_ctx, SYSCTL_CHILDREN(emu_scrub_oid),
	    OID_AUTO, "method", CTLTYPE_INT | CTLFLAG_RW,
	    &emu_scrub_cfg.sc_method, 0, emu_scrub_sysctl_method, "I",
	    "Scrubbing method (0=zero, 1=random, 2=pattern)");
}

/*
 * Sysctl handler for enabled
 */
static int
emu_scrub_sysctl_enabled(SYSCTL_HANDLER_ARGS)
{
	int error;
	int enabled;

	enabled = emu_scrub_cfg.sc_enabled;
	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (enabled < 0 || enabled > 1)
		return (EINVAL);

	mtx_lock(&emu_scrub_cfg.sc_lock);
	emu_scrub_cfg.sc_enabled = enabled;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (0);
}

/*
 * Sysctl handler for method
 */
static int
emu_scrub_sysctl_method(SYSCTL_HANDLER_ARGS)
{
	int error;
	int method;

	method = emu_scrub_cfg.sc_method;
	error = sysctl_handle_int(oidp, &method, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (method < EMU_SCRUB_METHOD_ZERO || method > EMU_SCRUB_METHOD_PATTERN)
		return (EINVAL);

	mtx_lock(&emu_scrub_cfg.sc_lock);
	emu_scrub_cfg.sc_method = method;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (0);
}

/*
 * Set scrubbing method
 */
int
emu_scrub_set_method(int method)
{
	int error;

	mtx_lock(&emu_scrub_cfg.sc_lock);
	switch (method) {
	case EMU_SCRUB_METHOD_ZERO:
	case EMU_SCRUB_METHOD_RANDOM:
	case EMU_SCRUB_METHOD_PATTERN:
		emu_scrub_cfg.sc_method = method;
		error = 0;
		break;
	default:
		error = EINVAL;
		break;
	}
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (error);
}

/*
 * Get current scrubbing method
 */
int
emu_scrub_get_method(void)
{
	int method;

	mtx_lock(&emu_scrub_cfg.sc_lock);
	method = emu_scrub_cfg.sc_method;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (method);
}

/*
 * Set scrubbing pattern
 */
int
emu_scrub_set_pattern(const uint8_t *pattern, int len)
{
	int error;

	if (len < 0 || len > sizeof(emu_scrub_cfg.sc_pattern))
		return (EINVAL);

	mtx_lock(&emu_scrub_cfg.sc_lock);
	memcpy(emu_scrub_cfg.sc_pattern, pattern, len);
	emu_scrub_cfg.sc_pattern_len = len;
	emu_scrub_cfg.sc_method = EMU_SCRUB_METHOD_PATTERN;
	error = 0;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (error);
}

/*
 * Enable/disable scrubbing
 */
int
emu_scrub_set_enabled(int enabled)
{
	mtx_lock(&emu_scrub_cfg.sc_lock);
	emu_scrub_cfg.sc_enabled = enabled ? 1 : 0;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (0);
}

/*
 * Check if scrubbing is enabled
 */
int
emu_scrub_is_enabled(void)
{
	int enabled;

	mtx_lock(&emu_scrub_cfg.sc_lock);
	enabled = emu_scrub_cfg.sc_enabled;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	return (enabled);
}

/*
 * Scrub memory region
 *
 * This function scrubs a memory region using the configured method.
 * It uses explicit_bzero() to ensure the compiler doesn't optimize
 * away the memory write operations.
 */
void
emu_scrub_memory(void *addr, size_t len)
{
	int method;
	uint8_t *buf;
	size_t i;

	if (addr == NULL || len == 0)
		return;

	mtx_lock(&emu_scrub_cfg.sc_lock);
	if (!emu_scrub_cfg.sc_enabled) {
		mtx_unlock(&emu_scrub_cfg.sc_lock);
		return;
	}
	method = emu_scrub_cfg.sc_method;
	mtx_unlock(&emu_scrub_cfg.sc_lock);

	buf = (uint8_t *)addr;

	switch (method) {
	case EMU_SCRUB_METHOD_ZERO:
		/* Zero out memory using explicit_bzero to prevent optimization */
		explicit_bzero(buf, len);
		break;

	case EMU_SCRUB_METHOD_RANDOM:
		/* Fill with random data */
		for (i = 0; i < len; i += 256) {
			size_t chunk = MIN(256, len - i);
			read_random(buf + i, chunk);
		}
		/* Ensure the write is not optimized away */
		explicit_bzero(buf, len);
		explicit_bzero(buf, len);
		break;

	case EMU_SCRUB_METHOD_PATTERN:
		/* Fill with configured pattern */
		if (emu_scrub_cfg.sc_pattern_len > 0) {
			for (i = 0; i < len; i++) {
				buf[i] = emu_scrub_cfg.sc_pattern[i % emu_scrub_cfg.sc_pattern_len];
			}
			/* Ensure the write is not optimized away */
			explicit_bzero(buf, len);
			explicit_bzero(buf, len);
		} else {
			/* Fallback to zero if no pattern set */
			explicit_bzero(buf, len);
		}
		break;

	default:
		/* Should not happen, but zero as fallback */
		explicit_bzero(buf, len);
		break;
	}
}

/*
 * Scrub memory with specific method (override global config)
 */
void
emu_scrub_memory_method(void *addr, size_t len, int method)
{
	uint8_t *buf;
	size_t i;

	if (addr == NULL || len == 0)
		return;

	buf = (uint8_t *)addr;

	switch (method) {
	case EMU_SCRUB_METHOD_ZERO:
		explicit_bzero(buf, len);
		break;

	case EMU_SCRUB_METHOD_RANDOM:
		for (i = 0; i < len; i += 256) {
			size_t chunk = MIN(256, len - i);
			read_random(buf + i, chunk);
		}
		explicit_bzero(buf, len);
		explicit_bzero(buf, len);
		break;

	case EMU_SCRUB_METHOD_PATTERN:
		mtx_lock(&emu_scrub_cfg.sc_lock);
		if (emu_scrub_cfg.sc_pattern_len > 0) {
			for (i = 0; i < len; i++) {
				buf[i] = emu_scrub_cfg.sc_pattern[i % emu_scrub_cfg.sc_pattern_len];
			}
			explicit_bzero(buf, len);
			explicit_bzero(buf, len);
		} else {
			explicit_bzero(buf, len);
		}
		mtx_unlock(&emu_scrub_cfg.sc_lock);
		break;

	default:
		explicit_bzero(buf, len);
		break;
	}
}

/*
 * Destroy memory scrubbing subsystem
 */
void
emu_scrub_destroy(void)
{

	mtx_destroy(&emu_scrub_cfg.sc_lock);
	SYSCTL_CTX_FREE(&emu_scrub_ctx);
}
