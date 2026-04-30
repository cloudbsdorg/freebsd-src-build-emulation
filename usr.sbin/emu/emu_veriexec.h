/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
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

#ifndef _EMU_VERIEXEC_H_
#define	_EMU_VERIEXEC_H_

/*
 * Veriexec (Verified Executable) Integration for Emulation Framework
 *
 * This module provides integration with FreeBSD's Veriexec subsystem
 * for secure executable verification and integrity checking.
 */

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/*
 * Get the maximum label length from mac_veriexec header.
 * Define it here if not available.
 */
#ifndef MAXLABELLEN
#define	MAXLABELLEN	128
#endif

/* Veriexec status flags */
#define	EMU_VERIEXEC_TRUSTED	0x01
#define	EMU_VERIEXEC_SIGNED	0x02
#define	EMU_VERIEXEC_LOCKED	0x04

/*
 * Veriexec subsystem lifecycle
 */

/* Check if Veriexec subsystem is available */
bool emu_veriexec_available(void);

/* Check if Veriexec is in enforcing mode */
int emu_veriexec_is_enforcing(void);

/* Initialize Veriexec subsystem */
int emu_veriexec_init(void);

/*
 * Veriexec verification
 */

/* Verify an executable file */
int emu_veriexec_check(const char *path, int fd);

/*
 * Veriexec registration
 */

/* Register an executable with Veriexec */
int emu_veriexec_register(const char *path, uint32_t flags);

/* Unregister an executable from Veriexec */
int emu_veriexec_unregister(const char *path);

/*
 * Veriexec status queries
 */

/* Get Veriexec status for an executable */
int emu_veriexec_status(const char *path, uint32_t *status);

/* List all registered executables */
int emu_veriexec_list(char **list, int max_items);

/*
 * Self-verification
 */

/* Verify the emulator binary itself */
int emu_veriexec_self_check(const char *progpath);

/*
 * Cleanup
 */

/* Cleanup veriexec subsystem */
void emu_veriexec_fini(void);

#endif /* !_EMU_VERIEXEC_H_ */
