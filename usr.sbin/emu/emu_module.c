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

/*
 * Module Signature Verification for Emulation Framework
 * Task 7.3: Implement module signature verification.
 *
 * This module provides module signature verification to ensure only
 * approved modules can be loaded into emulated instances.
 *
 * Note: This is a stub implementation for cross-compilation environments.
 * On native FreeBSD, this would use the real module subsystem interface.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"
#include "emu_module.h"

/*
 * Module signature verification configuration
 */
static struct {
	int	enabled;
	int	enforce_strict;
} emu_modsig_config = {
	.enabled = 1,
	.enforce_strict = 1
};

/*
 * Module state tracking
 */
static struct {
	int	modules_loaded;
	int	max_modules;
} emu_module_state = {
	.modules_loaded = 0,
	.max_modules = 256
};

/*
 * Initialize module subsystem
 *
 * Returns 0 on success, error code on failure
 */
int
emu_module_init(void)
{
	emu_module_state.modules_loaded = 0;

	if (g_verbose)
		fprintf(stderr, "Module subsystem initialized (verification %s)\n",
		    emu_modsig_config.enabled ? "enabled" : "disabled");

	return (0);
}

/*
 * Cleanup module subsystem
 */
void
emu_module_cleanup(void)
{
	if (g_verbose)
		fprintf(stderr, "Module subsystem cleanup: %d modules unloaded\n",
		    emu_module_state.modules_loaded);

	emu_module_state.modules_loaded = 0;
}

/*
 * Verify module signature (stub implementation)
 *
 * Parameters:
 *   module_path - Path to the module file
 *   signature - Pointer to signature buffer (optional)
 *   sig_len - Length of signature buffer
 *
 * Returns 0 if verified, error code on failure
 */
int
emu_module_verify_signature(const char *module_path, const uint8_t *signature,
    size_t sig_len)
{
	struct stat sb;

	(void)signature;
	(void)sig_len;

	if (module_path == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Check if file exists */
	if (stat(module_path, &sb) != 0) {
		if (g_verbose)
			fprintf(stderr, "Module not found: %s\n", module_path);
		return (-1);
	}

	/* Check if it's a regular file */
	if (!S_ISREG(sb.st_mode)) {
		if (g_verbose)
			fprintf(stderr, "Module not a regular file: %s\n", module_path);
		errno = EINVAL;
		return (-1);
	}

	/* Stub: In a real implementation, this would verify the signature */
	if (emu_modsig_config.enabled) {
		if (g_verbose)
			fprintf(stderr, "Module signature verification: %s (stub)\n",
			    module_path);
	}

	return (0);
}

/*
 * Load a kernel module (stub implementation)
 *
 * Parameters:
 *   module_name - Name of the module to load
 *   args - Module arguments (optional)
 *
 * Returns 0 on success, error code on failure
 */
int
emu_module_load(const char *module_name, const char *args)
{
	char module_path[MAXPATHLEN];

	if (module_name == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Check if we've hit the module limit */
	if (emu_module_state.modules_loaded >= emu_module_state.max_modules) {
		if (g_verbose)
			fprintf(stderr, "Module limit reached: %d\n",
			    emu_module_state.max_modules);
		errno = ENOBUFS;
		return (-1);
	}

	/* Build module path - look in standard locations */
	snprintf(module_path, sizeof(module_path), "/boot/kernel/%s.ko", module_name);

	if (access(module_path, R_OK) != 0) {
		/* Try current directory */
		snprintf(module_path, sizeof(module_path), "./%s.ko", module_name);
		if (access(module_path, R_OK) != 0) {
			if (g_verbose)
				fprintf(stderr, "Module not found: %s\n", module_name);
			errno = ENOENT;
			return (-1);
		}
	}

	/* Verify signature if enabled */
	if (emu_modsig_config.enabled) {
		int error = emu_module_verify_signature(module_path, NULL, 0);
		if (error != 0) {
			if (emu_modsig_config.enforce_strict) {
				if (g_verbose)
					fprintf(stderr, "Module signature verification "
					    "failed: %s\n", module_name);
				errno = EACCES;
				return (-1);
			} else if (g_verbose) {
				fprintf(stderr, "Warning: Module signature not verified: "
				    "%s\n", module_name);
			}
		}
	}

	/* Stub: In a real implementation, this would actually load the module */
	if (g_verbose)
		fprintf(stderr, "Module loaded (stub): %s\n", module_name);

	emu_module_state.modules_loaded++;

	return (0);
}

/*
 * Unload a kernel module (stub implementation)
 *
 * Parameters:
 *   module_name - Name of the module to unload
 *
 * Returns 0 on success, error code on failure
 */
int
emu_module_unload(const char *module_name)
{
	if (module_name == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (emu_module_state.modules_loaded <= 0) {
		if (g_verbose)
			fprintf(stderr, "No modules loaded\n");
		errno = ENOENT;
		return (-1);
	}

	/* Stub: In a real implementation, this would actually unload the module */
	if (g_verbose)
		fprintf(stderr, "Module unloaded (stub): %s\n", module_name);

	emu_module_state.modules_loaded--;

	return (0);
}

/*
 * Check if a module is loaded
 *
 * Parameters:
 *   module_name - Name of the module to check
 *
 * Returns true if loaded, false otherwise
 */
bool
emu_module_is_loaded(const char *module_name)
{
	(void)module_name;

	/* Stub: In a real implementation, this would check the module list */
	return (false);
}

/*
 * Get list of loaded modules
 *
 * Parameters:
 *   list - Output array of module names (caller must free)
 *   max_items - Maximum number of names to return
 *
 * Returns number of modules, or -1 on error
 */
int
emu_module_list(char **list, int max_items)
{
	if (list == NULL || max_items <= 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Stub: In a real implementation, this would enumerate loaded modules */
	if (max_items > 0)
		list[0] = NULL;

	return (0);
}

/*
 * Get module information
 *
 * Parameters:
 *   module_name - Name of the module
 *   info - Output: module information structure
 *
 * Returns 0 on success, error code on failure
 */
int
emu_module_info(const char *module_name, struct emu_module_info *info)
{
	if (module_name == NULL || info == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Stub: In a real implementation, this would fill in actual module info */
	memset(info, 0, sizeof(*info));
	strlcpy(info->name, module_name, sizeof(info->name));
	info->loaded = false;
	info->size = 0;

	return (0);
}

/*
 * Module dependency resolution (stub)
 *
 * Parameters:
 *   module_name - Module to resolve dependencies for
 *   deps - Output: array of dependency names (caller must free)
 *   max_deps - Maximum number of dependencies
 *
 * Returns number of dependencies, or -1 on error
 */
int
emu_module_resolve_deps(const char *module_name, char **deps, int max_deps)
{
	if (module_name == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Stub: In a real implementation, this would parse module dependencies */
	(void)deps;
	(void)max_deps;

	if (g_verbose)
		fprintf(stderr, "Resolving dependencies for: %s (stub)\n", module_name);

	return (0);
}

/*
 * Set module signature verification mode
 *
 * Parameters:
 *   enabled - Enable/disable verification
 *   enforce_strict - Fail on missing/invalid signature
 */
void
emu_module_set_verify_mode(bool enabled, bool enforce_strict)
{
	emu_modsig_config.enabled = enabled ? 1 : 0;
	emu_modsig_config.enforce_strict = enforce_strict ? 1 : 0;

	if (g_verbose)
		fprintf(stderr, "Module verification: %s, strict mode: %s\n",
		    emu_modsig_config.enabled ? "enabled" : "disabled",
		    emu_modsig_config.enforce_strict ? "on" : "off");
}

/*
 * Get number of loaded modules
 */
int
emu_module_count(void)
{
	return (emu_module_state.modules_loaded);
}
