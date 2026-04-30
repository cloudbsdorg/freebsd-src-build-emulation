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

#ifndef _EMU_MODULE_H
#define _EMU_MODULE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * Dynamic Kernel Module Signature Verification
 *
 * Provides runtime signature verification for dynamically loaded kernel modules.
 * Modules are signed using HMAC-SHA256 and verified at load time.
 */

/*
 * Module information structure
 */
struct emu_module_info {
	char		name[64];	/* Module name */
	char		version[32];	/* Module version */
	char		author[64];	/* Module author */
	bool		loaded;		/* Currently loaded */
	size_t		size;		/* Module size in bytes */
	uint64_t	loaded_time;	/* Time module was loaded */
};

/*
 * Module subsystem lifecycle
 */

/* Initialize module subsystem */
int emu_module_init(void);

/* Cleanup module subsystem */
void emu_module_cleanup(void);

/*
 * Module loading and unloading
 */

/* Load a kernel module */
int emu_module_load(const char *module_name, const char *args);

/* Unload a kernel module */
int emu_module_unload(const char *module_name);

/*
 * Module queries
 */

/* Check if a module is loaded */
bool emu_module_is_loaded(const char *module_name);

/* Get list of loaded modules */
int emu_module_list(char **list, int max_items);

/* Get module information */
int emu_module_info(const char *module_name, struct emu_module_info *info);

/*
 * Module signature verification
 */

/* Verify module signature at load time */
int emu_module_verify_signature(const char *module_path, const uint8_t *signature,
    size_t sig_len);

/* Set verification mode */
void emu_module_set_verify_mode(bool enabled, bool enforce_strict);

/*
 * Module dependencies
 */

/* Resolve module dependencies */
int emu_module_resolve_deps(const char *module_name, char **deps, int max_deps);

/*
 * Statistics
 */

/* Get number of loaded modules */
int emu_module_count(void);

#endif /* _EMU_MODULE_H */
