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
 * Veriexec (Verified Executable) Integration for Emulation Framework
 *
 * This module provides integration with FreeBSD's Veriexec subsystem
 * for secure executable verification and integrity checking.
 *
 * Veriexec is a Mandatory Access Control (MAC) framework module that
 * provides executable integrity verification using cryptographic hashes
 * and optionally digital signatures.
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <paths.h>
#include <sys/mac.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"
#include "emu_veriexec.h"
#include <security/mac_veriexec/mac_veriexec.h>
#include <dev/veriexec/veriexec_ioctl.h>
#include <libveriexec.h>

/*
 * Veriexec subsystem state
 */
static bool g_veriexec_available = false;
static bool g_veriexec_enforcing = false;
static bool g_veriexec_initialized = false;
static int g_veriexec_fd = -1;

/*
 * Check if MAC veriexec policy is present/loaded
 */
static bool
emu_veriexec_mac_present(void)
{

	return (mac_is_present(MAC_VERIEXEC_NAME) == 0);
}

/*
 * Check if veriexec device is available and get current state
 */
static bool
emu_veriexec_get_state(bool *enforcing)
{
	int fd;
	int state;
	bool available = false;

	fd = open(_PATH_DEV_VERIEXEC, O_RDONLY);
	if (fd < 0)
		return (false);

	if (ioctl(fd, VERIEXEC_GETSTATE, &state) == 0) {
		available = true;
		if (enforcing != NULL)
			*enforcing = (state & VERIEXEC_STATE_ENFORCE) != 0;
	}

	close(fd);
	return (available);
}

/*
 * Check if Veriexec subsystem is available
 *
 * Returns true if Veriexec is available, false otherwise
 */
bool
emu_veriexec_available(void)
{

	if (!emu_veriexec_mac_present())
		return (false);

	return (emu_veriexec_get_state(NULL));
}

/*
 * Initialize Veriexec subsystem for the emulator
 *
 * Returns 0 on success, error code on failure
 */
int
emu_veriexec_init(void)
{

	if (g_veriexec_initialized)
		return (0);

	/* Check if MAC veriexec policy is loaded */
	if (!emu_veriexec_mac_present()) {
		if (g_verbose)
			fprintf(stderr, "Veriexec: MAC policy '%s' not loaded\n",
			    MAC_VERIEXEC_NAME);
		return (ENOSYS);
	}

	/* Open the veriexec device and check state */
	if (!emu_veriexec_get_state(&g_veriexec_enforcing)) {
		if (g_verbose)
			fprintf(stderr, "Veriexec: device not available\n");
		return (ENOSYS);
	}

	/* Open veriexec device for subsequent operations */
	g_veriexec_fd = open(_PATH_DEV_VERIEXEC, O_RDONLY);
	if (g_veriexec_fd < 0) {
		if (g_verbose)
			fprintf(stderr, "Veriexec: cannot open device: %s\n",
			    strerror(errno));
		return (errno);
	}

	g_veriexec_available = true;
	g_veriexec_initialized = true;

	if (g_verbose) {
		fprintf(stderr, "Veriexec initialized: %s\n",
		    g_veriexec_enforcing ? "enforcing" : "not enforcing");
	}

	return (0);
}

/*
 * Verify an executable file before loading
 *
 * Parameters:
 *   path - Path to the executable
 *   fd - File descriptor (if already open), or -1
 *
 * Returns 0 if verified (or verification not required),
 *         error code on failure
 */
int
emu_veriexec_check(const char *path, int fd)
{
	int error;

	if (!g_veriexec_initialized) {
		/* Skip verification if not initialized */
		return (0);
	}

	if (!g_veriexec_available)
		return (0);

	/* Check via file descriptor if provided */
	if (fd >= 0) {
		error = veriexec_check_fd(fd);
		if (error != 0) {
			if (g_verbose)
				fprintf(stderr,
				    "Veriexec: check failed for fd %d: %d\n",
				    fd, error);
			return (error);
		}
		return (0);
	}

	/* Check via path */
	if (path == NULL)
		return (EINVAL);

	error = veriexec_check_path(path);
	if (error != 0) {
		if (g_verbose)
			fprintf(stderr,
			    "Veriexec: check failed for '%s': %d\n",
			    path, error);
		return (error);
	}

	return (0);
}

/*
 * Register an executable with Veriexec
 *
 * Parameters:
 *   path - Path to the executable
 *   flags - Veriexec flags (signature requirements, etc.)
 *
 * Returns 0 on success, error code on failure
 *
 * Note: Registration requires root privileges and is typically
 *       done during system initialization via veriexecctl(8).
 *       This function is provided for completeness but generally
 *       requires privileged operations.
 */
int
emu_veriexec_register(const char *path, uint32_t flags)
{
	(void)path;
	(void)flags;

	if (!g_veriexec_initialized)
		return (ENOSYS);

	if (!g_veriexec_available)
		return (ENOSYS);

	/*
	 * Registration is a privileged operation performed via
	 * veriexecctl(8) and the manifest file. Applications
	 * typically do not register executables directly.
	 *
	 * This function exists for API completeness but returns
	 * ENOTSUP as direct registration is not the intended use.
	 */
	return (ENOTSUP);
}

/*
 * Unregister an executable from Veriexec
 *
 * Parameters:
 *   path - Path to the executable
 *
 * Returns 0 on success, error code on failure
 *
 * Note: Unregistration requires root privileges and is rarely
 *       used in production systems.
 */
int
emu_veriexec_unregister(const char *path)
{
	(void)path;

	if (!g_veriexec_initialized)
		return (ENOSYS);

	if (!g_veriexec_available)
		return (ENOSYS);

	/* Unregistration is a privileged operation */
	return (ENOTSUP);
}

/*
 * Get Veriexec status for an executable
 *
 * Parameters:
 *   path - Path to the executable
 *   status - Output: status flags (EMU_VERIEXEC_*)
 *
 * Returns 0 on success, error code on failure
 */
int
emu_veriexec_status(const char *path, uint32_t *status)
{
	struct mac_veriexec_syscall_params params;
	char label[MAXLABELLEN];
	int error;

	if (status == NULL)
		return (EINVAL);

	*status = 0;

	if (!g_veriexec_initialized)
		return (ENOSYS);

	if (!g_veriexec_available)
		return (ENOSYS);

	if (path == NULL)
		return (EINVAL);

	/* Get the fingerprint parameters for this file */
	memset(&params, 0, sizeof(params));
	error = veriexec_get_path_params(path, &params);
	if (error != 0) {
		if (g_verbose)
			fprintf(stderr,
			    "Veriexec: status query failed for '%s': %d\n",
			    path, error);
		/* File not registered - this is not an error */
		return (0);
	}

	/* Get the label if present */
	if (veriexec_get_path_label(path, label, sizeof(label)) != NULL &&
	    params.labellen > 0) {
		*status |= EMU_VERIEXEC_TRUSTED;
	}

	/* Check for signature requirement */
	if (params.flags & VERIEXEC_FILE)
		*status |= EMU_VERIEXEC_SIGNED;

	/* Check for locked state */
	if (params.flags & VERIEXEC_LABEL)
		*status |= EMU_VERIEXEC_LOCKED;

	return (0);
}

/*
 * List all registered executables
 *
 * Parameters:
 *   list - Output: array of paths (caller must free each element)
 *   max_items - Maximum number of paths to return
 *
 * Returns number of registered executables, or -1 on error
 *
 * Note: This is a simplified implementation. A full implementation
 *       would enumerate the kernel's veriexec entries.
 */
int
emu_veriexec_list(char **list, int max_items)
{
	(void)list;
	(void)max_items;

	if (!g_veriexec_initialized)
		return (0);

	if (!g_veriexec_available)
		return (0);

	/*
	 * Enumerating registered executables requires kernel
	 * support that is not currently exposed via the libveriexec
	 * API. This would need a new kernel interface.
	 */
	return (0);
}

/*
 * Check if Veriexec is in enforcing mode
 *
 * Returns 1 if Veriexec is enforcing, 0 otherwise
 */
int
emu_veriexec_is_enforcing(void)
{

	if (!g_veriexec_initialized)
		return (0);

	/* Re-check state in case it changed */
	if (g_veriexec_available)
		emu_veriexec_get_state(&g_veriexec_enforcing);

	return (g_veriexec_enforcing ? 1 : 0);
}

/*
 * Verify the emulator binary itself before execution
 * This is called at startup to ensure the emulator hasn't been tampered with
 *
 * Parameters:
 *   progpath - Path to the emulator binary (or NULL to skip)
 *
 * Returns 0 on success, error code on failure
 *
 * Note: Callers should pass argv[0] or the resolved program path.
 *       This function will skip the check if progpath is NULL.
 */
int
emu_veriexec_self_check(const char *progpath)
{
	int error;

	if (progpath == NULL) {
		if (g_verbose)
			fprintf(stderr,
			    "Veriexec: self-check skipped (no path)\n");
		return (0);
	}

	error = emu_veriexec_check(progpath, -1);
	if (error != 0) {
		if (g_verbose)
			fprintf(stderr,
			    "Veriexec: self-check failed for '%s': %d\n",
			    progpath, error);
		/*
		 * In enforcing mode, this is fatal.
		 * In non-enforcing mode, we warn but continue.
		 */
		if (g_veriexec_enforcing)
			return (error);
	}

	return (0);
}

/*
 * Cleanup veriexec subsystem
 */
void
emu_veriexec_fini(void)
{

	if (g_veriexec_fd >= 0) {
		close(g_veriexec_fd);
		g_veriexec_fd = -1;
	}

	g_veriexec_initialized = false;
	g_veriexec_available = false;
	g_veriexec_enforcing = false;
}
