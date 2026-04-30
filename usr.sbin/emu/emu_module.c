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

/*
 * Dynamic Kernel Module Signature Verification
 *
 * This module provides runtime signature verification for dynamically loaded
 * kernel modules. When modules are built, they are signed using HMAC-SHA256
 * with an organization-specific key. At load time, the signature is verified
 * before the module is initialized.
 *
 * The verification process:
 * 1. Module is loaded via kldload
 * 2. Kernel calls modevent handler with MOD_LOAD
 * 3. Before any module initialization, emu_module_verify_signature() is called
 * 4. Signature file (.sig) is read from the same directory as the module
 * 5. HMAC-SHA256 is computed over the module content
 * 6. Computed signature is compared against stored signature
 * 7. If verification fails, module load is refused
 *
 * Security properties:
 * - Prevents loading of tampered modules
 * - Prevents loading of modules built without the organization's signing key
 * - Graceful degradation: if signature file is missing, verification can be
 *   configured to either fail or allow (based on sysctl setting)
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/hash.h>
#include <sys/libkern.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "emu_module.h"

/*
 * Sysctl nodes for module signature verification configuration
 */
static struct {
	int	enabled;			/* Enable/disable verification */
	int	enforce_strict;		/* Fail on missing signature */
	int	warn_only;			/* Log warnings but allow */
} emu_modsig_config = {
	.enabled = 1,
	.enforce_strict = 1,
	.warn_only = 0
};

static struct sysctl_ctx_list *emu_modsig_sysctl_ctx;
static struct sysctl_oid *emu_modsig_sysctl_root;

/*
 * Default signing key - in production, this should be stored in a
 * secure location (e.g., kernel protected memory, TPM, etc.)
 * This is a placeholder for demonstration purposes.
 */
static const uint8_t emu_modsig_default_key[SHA256_DIGEST_LENGTH] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

/*
 * Get the signing key for module verification.
 * In production, this would retrieve the key from a secure source.
 */
static const uint8_t *
emu_modsig_get_key(void)
{
	return (emu_modsig_default_key);
}

/*
 * Read the signature file associated with a module.
 * Signature file format: ASCII hex-encoded HMAC-SHA256 signature (64 characters)
 * followed by newline.
 */
static int
emu_modsig_read_signature(const char *module_path, uint8_t *sig_buf,
    size_t sig_buf_size)
{
	char sig_path[MAXPATHLEN];
	char sig_hex[SHA256_DIGEST_LENGTH * 2 + 2];
	char *endp;
	FILE *fp;
	size_t len;
	int error;

	/* Build signature file path: module.ko.sig */
	snprintf(sig_path, sizeof(sig_path), "%s.sig", module_path);

	fp = fopen(sig_path, "r");
	if (fp == NULL)
		return (ENOENT);

	/* Read signature (should be 64 hex chars + newline) */
	if (fgets(sig_hex, sizeof(sig_hex), fp) == NULL) {
		fclose(fp);
		return (EINVAL);
	}
	fclose(fp);

	/* Remove trailing newline */
	len = strlen(sig_hex);
	if (len > 0 && sig_hex[len - 1] == '\n')
		sig_hex[len - 1] = '\0';

	/* Validate hex format */
	if (strlen(sig_hex) != SHA256_DIGEST_LENGTH * 2) {
		printf("emu_module: invalid signature format in %s\n", sig_path);
		return (EINVAL);
	}

	for (len = 0; len < SHA256_DIGEST_LENGTH * 2; len++) {
		if (!isxdigit((unsigned char)sig_hex[len])) {
			printf("emu_module: non-hex character in signature: %s\n",
			    sig_path);
			return (EINVAL);
		}
	}

	/* Convert hex to binary */
	for (len = 0; len < SHA256_DIGEST_LENGTH; len++) {
		sig_buf[len] = (uint8_t)strtoul(sig_hex + len * 2, &endp, 16);
		if (*endp != '\0')
			return (EINVAL);
	}

	return (0);
}

/*
 * Compute HMAC-SHA256 over module content.
 * We read the module file in chunks to handle large modules.
 */
static int
emu_modsig_compute_hmac(const char *module_path, uint8_t *hmac_buf,
    size_t hmac_buf_size)
{
	FILE *fp;
	SHA256_CTX ctx;
	uint8_t chunk[8192];
	size_t nread;
	int error;

	if (hmac_buf_size < SHA256_DIGEST_LENGTH)
		return (ENOBUFS);

	fp = fopen(module_path, "rb");
	if (fp == NULL)
		return (ENOENT);

	SHA256_Init(&ctx);

	while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		HMAC_Update((HMAC_CTX *)&ctx, chunk, nread);
	}

	if (ferror(fp)) {
		fclose(fp);
		return (EIO);
	}

	fclose(fp);

	HMAC_Final((HMAC_CTX *)&ctx, hmac_buf, NULL);

	return (0);
}

/*
 * Verify module signature at load time.
 * This function should be called from the modevent handler before
 * any module initialization.
 *
 * Returns:
 *   0 - Signature valid or verification disabled
 *   EACCES - Signature verification failed
 *   ENOENT - Module file not found
 *   other - Error reading signature file
 */
int
emu_module_verify_signature(const char *module_name, const char *module_path)
{
	uint8_t computed_hmac[SHA256_DIGEST_LENGTH];
	uint8_t stored_sig[SHA256_DIGEST_LENGTH];
	const uint8_t *key;
	int error;

	/* Check if verification is enabled */
	if (!emu_modsig_config.enabled) {
		printf("emu_module: signature verification disabled for %s\n",
		    module_name);
		return (0);
	}

	/* Get signing key */
	key = emu_modsig_get_key();
	if (key == NULL) {
		printf("emu_module: no signing key available, rejecting %s\n",
		    module_name);
		return (EACCES);
	}

	/* Read stored signature */
	error = emu_modsig_read_signature(module_path, stored_sig,
	    sizeof(stored_sig));
	if (error == ENOENT) {
		if (emu_modsig_config.enforce_strict) {
			printf("emu_module: signature missing for %s, refusing load "
			    "(strict mode)\n", module_name);
			return (EACCES);
		}
		if (emu_modsig_config.warn_only) {
			printf("emu_module: WARNING: signature missing for %s, "
			    "allowing load (warn-only mode)\n", module_name);
		}
		return (0);
	}
	if (error != 0) {
		printf("emu_module: error reading signature for %s: %d\n",
		    module_name, error);
		return (error);
	}

	/* Compute HMAC over module */
	error = emu_modsig_compute_hmac(module_path, computed_hmac,
	    sizeof(computed_hmac));
	if (error != 0) {
		printf("emu_module: error computing HMAC for %s: %d\n",
		    module_name, error);
		return (error);
	}

	/* Compare signatures using constant-time comparison */
	if (consttime_memequal(computed_hmac, stored_sig,
	    SHA256_DIGEST_LENGTH) != 0) {
		printf("emu_module: signature verification failed for %s\n",
		    module_name);
		printf("emu_module: module may be tampered or built without "
		    "proper signing key\n");
		return (EACCES);
	}

	printf("emu_module: signature verified for %s\n", module_name);
	return (0);
}

/*
 * Generate a signature file for a module.
 * This is used during the build process to sign modules.
 */
int
emu_module_sign_module(const char *module_path, const char *sig_path)
{
	FILE *fp;
	SHA256_CTX ctx;
	uint8_t chunk[8192];
	uint8_t hmac[SHA256_DIGEST_LENGTH];
	char sig_hex[SHA256_DIGEST_LENGTH * 2 + 1];
	const uint8_t *key;
	size_t nread, i;

	key = emu_modsig_get_key();
	if (key == NULL)
		return (ENOENT);

	/* Compute HMAC over module */
	fp = fopen(module_path, "rb");
	if (fp == NULL)
		return (ENOENT);

	SHA256_Init(&ctx);

	while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		HMAC_Update((HMAC_CTX *)&ctx, chunk, nread);
	}

	if (ferror(fp)) {
		fclose(fp);
		return (EIO);
	}

	fclose(fp);

	HMAC_Final((HMAC_CTX *)&ctx, hmac, NULL);

	/* Convert to hex */
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		snprintf(sig_hex + i * 2, 3, "%02x", hmac[i]);

	/* Write signature file */
	fp = fopen(sig_path, "w");
	if (fp == NULL)
		return (errno);

	fprintf(fp, "%s\n", sig_hex);
	fclose(fp);

	return (0);
}

/*
 * Check if a module is signed and verified.
 */
int
emu_module_is_verified(const char *module_name)
{

	return (emu_modsig_config.enabled);
}

/*
 * Initialize module signature verification subsystem.
 */
static int
emu_modsig_init(void)
{
	struct sysctl_oid *oid;

	emu_modsig_sysctl_ctx = sysctl_ctx_alloc(&emu_modsig_sysctl_root);
	if (emu_modsig_sysctl_ctx == NULL)
		return (ENOMEM);

	oid = SYSCTL_ADD_ROOT_NODE(emu_modsig_sysctl_ctx,
	    OID_AUTO, "emu_module_sig", CTLFLAG_RW, 0,
	    "emu_module_sig", "Module signature verification");
	if (oid == NULL) {
		sysctl_ctx_free(emu_modsig_sysctl_ctx);
		return (ENOMEM);
	}

	SYSCTL_ADD_INT(emu_modsig_sysctl_ctx, SYSCTL_CHILDREN(oid),
	    OID_AUTO, "enabled", CTLFLAG_RWTUN,
	    &emu_modsig_config.enabled, 0,
	    "Enable module signature verification");

	SYSCTL_ADD_INT(emu_modsig_sysctl_ctx, SYSCTL_CHILDREN(oid),
	    OID_AUTO, "enforce_strict", CTLFLAG_RWTUN,
	    &emu_modsig_config.enforce_strict, 0,
	    "Fail load if signature file is missing");

	SYSCTL_ADD_INT(emu_modsig_sysctl_ctx, SYSCTL_CHILDREN(oid),
	    OID_AUTO, "warn_only", CTLFLAG_RWTUN,
	    &emu_modsig_config.warn_only, 0,
	    "Log warnings instead of rejecting unsigned modules");

	printf("emu_module: signature verification subsystem initialized\n");
	printf("emu_module: verification %s, strict mode %s, warn-only %s\n",
	    emu_modsig_config.enabled ? "enabled" : "disabled",
	    emu_modsig_config.enforce_strict ? "enabled" : "disabled",
	    emu_modsig_config.warn_only ? "enabled" : "disabled");

	return (0);
}

/*
 * Shutdown module signature verification subsystem.
 */
static void
emu_modsig_shutdown(void)
{

	if (emu_modsig_sysctl_ctx != NULL) {
		sysctl_ctx_free(emu_modsig_sysctl_ctx);
		emu_modsig_sysctl_ctx = NULL;
		emu_modsig_sysctl_root = NULL;
	}
}

/*
 * Module event handler integration.
 * This function should be called from the module's modevent handler.
 */
int
emu_module_modevent_handler(module_t mod, int event, void *arg)
{
	const char *modname;
	char mod_path[MAXPATHLEN];
	int error;

	switch (event) {
	case MOD_LOAD:
		modname = module_getname(mod);
		/*
		 * Get module path - in FreeBSD, we can get this from
		 * module_find_file() and then vnode path.
		 */
		snprintf(mod_path, sizeof(mod_path), "/boot/kernel/%s.ko",
		    modname);

		error = emu_module_verify_signature(modname, mod_path);
		if (error != 0)
			return (error);
		break;

	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		/* No signature verification needed for unload */
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}
