/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
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

/*
 * Dynamic Kernel Module Signature Verification
 *
 * Provides runtime signature verification for dynamically loaded kernel modules.
 * Modules are signed using HMAC-SHA256 and verified at load time.
 */

/*
 * Verify module signature at load time.
 *
 * Parameters:
 *   module_name - Name of the module (e.g., "emu_core", "emu_amd64")
 *   module_path - Full path to the module file (e.g., "/boot/kernel/emu_core.ko")
 *
 * Returns:
 *   0 - Signature valid or verification disabled
 *   EACCES - Signature verification failed (tampered module or missing key)
 *   ENOENT - Module file not found
 *   EINVAL - Invalid signature format
 *   other - Error reading signature file
 */
int emu_module_verify_signature(const char *module_name, const char *module_path);

/*
 * Generate a signature file for a module.
 * This is used during the build process to sign modules.
 *
 * Parameters:
 *   module_path - Path to the module file
 *   sig_path - Path where signature file should be written
 *
 * Returns:
 *   0 - Success
 *   ENOENT - Module file not found or key not available
 *   EIO - Error writing signature file
 */
int emu_module_sign_module(const char *module_path, const char *sig_path);

/*
 * Check if module signature verification is enabled.
 *
 * Returns:
 *   1 - Verification enabled
 *   0 - Verification disabled
 */
int emu_module_is_verified(const char *module_name);

/*
 * Module event handler integration.
 * Call this from your module's modevent handler.
 *
 * Parameters:
 *   mod - The module_t passed to your modevent handler
 *   event - The event type (MOD_LOAD, MOD_UNLOAD, MOD_SHUTDOWN)
 *   arg - The arg passed to your modevent handler
 *
 * Returns:
 *   0 - Success
 *   Error code on failure
 */
int emu_module_modevent_handler(module_t mod, int event, void *arg);

#endif /* _EMU_MODULE_H */
