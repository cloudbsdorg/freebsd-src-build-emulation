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

/*
 * MAC veriexec integration for emulator binary verification.
 *
 * Note: Full veriexec integration requires the MAC framework to be enabled
 * and libveriexec to be available. This stub provides graceful degradation
 * when veriexec is not available.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/errno.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"

/* Default path for emu binary if getprogname() fails */
#ifndef _PATH_EMU
#define	_PATH_EMU	"/usr/sbin/emu"
#endif

/*
 * MAC veriexec integration for emulator binary verification.
 *
 * When mac_veriexec is loaded and configured, this function verifies
 * that the emulator binary has not been tampered with by checking
 * its fingerprint against the registered fingerprint database.
 *
 * This provides supply-chain security by ensuring only approved
 * versions of the emulator binary can be executed.
 */

/*
 * Check if MAC veriexec is available and enabled.
 * Returns:
 *   0 = veriexec is not available or not enforcing (OK to proceed)
 *   1 = veriexec is active and enforcing
 *  -1 = error (should abort)
 */
static int
emu_veriexec_check_available(void)
{
	/* TODO: Implement veriexec check when libveriexec is available */
	/* For now, always return 0 (not enforcing) */
	return (0);
}

/*
 * Verify the emulator binary fingerprint using veriexec.
 *
 * Returns:
 *   0 = Verification passed or veriexec not configured (OK to proceed)
 *  -1 = Verification failed (binary not in fingerprint database or mismatch)
 */
static int
emu_veriexec_verify_binary(const char *progname)
{
	/* TODO: Implement veriexec binary verification when libveriexec is available */
	/* For now, always return 0 (verification passed) */
	(void)progname;
	return (0);
}

/*
 * Check if veriexec should be bypassed based on environment.
 * Set EMU_NO_VERIEXEC=1 to bypass verification for testing.
 */
static int
emu_veriexec_should_bypass(void)
{
	const char *env_val;

	env_val = getenv("EMU_NO_VERIEXEC");
	if (env_val != NULL && strcmp(env_val, "1") == 0) {
		return (1);
	}

	return (0);
}

/*
 * Perform MAC veriexec verification for the emulator binary.
 *
 * This function should be called early in main() before any significant
 * operations. It verifies the binary's fingerprint if veriexec is
 * configured and in enforce mode.
 *
 * Returns:
 *   0 = Verification passed or not required (OK to proceed)
 *  -1 = Verification failed or should not proceed
 */
int
emu_veriexec_init(void)
{
	const char *progname;
	char resolved_path[PATH_MAX];
	int veriexec_active;
	int error;

	/* Check if we should bypass veriexec (for testing) */
	if (emu_veriexec_should_bypass()) {
		return (0);
	}

	/* Get our program name */
	progname = getprogname();
	if (progname == NULL || progname[0] == '\0') {
		progname = _PATH_EMU;
	}

	/* Check if veriexec is active and in enforce mode */
	veriexec_active = emu_veriexec_check_available();
	if (veriexec_active == 0) {
		/* Veriexec not active or not enforcing - OK to proceed */
		return (0);
	}

	/* Veriexec is active - verify our binary */
	if (progname[0] != '/') {
		/*
		 * Program name is not an absolute path.
		 * Try to resolve it to get a full path for verification.
		 */
		if (realpath(progname, resolved_path) == NULL) {
			/* Can't resolve path - use original */
			strlcpy(resolved_path, progname, sizeof(resolved_path));
		}
		progname = resolved_path;
	}

	error = emu_veriexec_verify_binary(progname);
	if (error != 0) {
		/*
		 * Verification failed. The binary may have been tampered with
		 * or is not authorized for execution.
		 *
		 * In a production environment, this should be fatal.
		 * We return -1 and let the caller decide whether to abort.
		 */
		return (-1);
	}

	return (0);
}

/*
 * Check if veriexec is in enforce mode (binary fingerprint verification required).
 *
 * Returns:
 *   0 = Not in enforce mode
 *   1 = In enforce mode
 */
int
emu_veriexec_is_enforcing(void)
{
	return (emu_veriexec_check_available() == 1);
}
