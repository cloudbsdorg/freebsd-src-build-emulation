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
 * HOWEVER CAUSED AND ON ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sha256.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <libsecureboot/libsecureboot.h>
#include <libsecureboot/openpgp/packet.h>

#include "emu.h"

extern int g_verbose;
extern int g_quiet;

/*
 * emu blob - Manage firmware blobs for emulated instances
 *
 * Usage: emu blob <command> [options]
 * Commands:
 *   list                     - List available blobs
 *   download <blob_name>     - Download a specific blob
 *   verify <blob_name>       - Verify blob integrity (SHA-256)
 *   delete <blob_name>       - Delete a blob
 *   info <blob_name>         - Show blob information
 */
int
emu_cmd_blob(int argc, char *argv[])
{
	const char *command = NULL;
	const char *blob_name = NULL;
	char sysctl_name[PATH_MAX];
	char sysctl_value[PATH_MAX];
	int error;

	if (argc < 1) {
		fprintf(stderr, "Usage: emu blob <command> [options]\n");
		fprintf(stderr, "Commands:\n");
		fprintf(stderr, "  list                     - List available blobs\n");
		fprintf(stderr, "  download <blob_name>     - Download a specific blob\n");
		fprintf(stderr, "  verify <blob_name>       - Verify blob integrity\n");
		fprintf(stderr, "  delete <blob_name>       - Delete a blob\n");
		fprintf(stderr, "  info <blob_name>         - Show blob information\n");
		return (EINVAL);
	}

	command = argv[0];
	if (argc >= 2)
		blob_name = argv[1];

	/* Handle list command - no blob name required */
	if (strcmp(command, "list") == 0) {
		if (g_verbose)
			printf("Listing available firmware blobs\n");

		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blobs");

		/* Get blob list - buffer size for initial query */
		char blob_list[4096];
		size_t len = sizeof(blob_list);
		error = sysctlbyname(sysctl_name, blob_list, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob sysctl not available\n");
			} else {
				fprintf(stderr, "Failed to list blobs: %s\n", strerror(errno));
			}
			return (errno);
		}

		if (len == 0) {
			printf("No firmware blobs available\n");
			return (0);
		}

		printf("Available firmware blobs:\n");
		/* Parse and display blob list (comma-separated) */
		char *saveptr;
		char *blob = strtok_r(blob_list, ",", &saveptr);
		while (blob != NULL) {
			printf("  - %s\n", blob);
			blob = strtok_r(NULL, ",", &saveptr);
		}

		return (0);
	}

	/* All other commands require a blob name */
	if (blob_name == NULL) {
		fprintf(stderr, "Command '%s' requires a blob name\n", command);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Blob command: %s, blob: %s\n", command, blob_name);

	/* Handle different blob commands */
	if (strcmp(command, "download") == 0) {
		if (g_verbose)
			printf("Downloading firmware blob '%s'\n", blob_name);

		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.download");
		snprintf(sysctl_value, sizeof(sysctl_value), "%s", blob_name);

		error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value,
		    strlen(sysctl_value));
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found in repository\n", blob_name);
			} else if (errno == EEXIST) {
				fprintf(stderr, "Blob '%s' already exists\n", blob_name);
			} else {
				fprintf(stderr, "Failed to download blob: %s\n", strerror(errno));
			}
			return (errno);
		}

		if (!g_quiet)
			printf("Firmware blob '%s' downloaded successfully\n", blob_name);

	} else if (strcmp(command, "verify") == 0) {
		if (g_verbose)
			printf("Verifying firmware blob '%s'\n", blob_name);

		/* S19.1: Implement firmware SHA-256 verification */
		char blob_path[PATH_MAX];
		char expected_hash[65]; /* 64 hex chars + null */
		unsigned char hash[SHA256_DIGEST_LENGTH];
		struct stat sb;
		int fd;
		void *mmap_ptr;
		size_t file_size;
		SHA256_CTX ctx;

		/* Get blob path from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.path", blob_name);
		error = sysctlbyname(sysctl_name, blob_path, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else {
				fprintf(stderr, "Failed to get blob path: %s\n", strerror(errno));
			}
			return (errno);
		}

		/* Get expected hash from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.sha256", blob_name);
		len = sizeof(expected_hash);
		error = sysctlbyname(sysctl_name, expected_hash, &len, NULL, 0);
		if (error != 0) {
			fprintf(stderr, "Expected hash not available for blob '%s'\n", blob_name);
			return (errno);
		}

		/* Open and map blob file */
		fd = open(blob_path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Failed to open blob file: %s\n", strerror(errno));
			return (errno);
		}

		if (fstat(fd, &sb) < 0) {
			fprintf(stderr, "Failed to stat blob file: %s\n", strerror(errno));
			close(fd);
			return (errno);
		}

		file_size = sb.st_size;
		mmap_ptr = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mmap_ptr == MAP_FAILED) {
			fprintf(stderr, "Failed to mmap blob file: %s\n", strerror(errno));
			close(fd);
			return (errno);
		}

		/* Compute SHA-256 hash */
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, mmap_ptr, file_size);
		SHA256_Final(hash, &ctx);

		/* Convert to hex string */
		char computed_hash[65];
		for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
			sprintf(&computed_hash[i * 2], "%02x", hash[i]);
		}
		computed_hash[64] = '\0';

		munmap(mmap_ptr, file_size);
		close(fd);

		/* Compare hashes */
		if (strcmp(computed_hash, expected_hash) != 0) {
			fprintf(stderr, "SHA-256 verification failed for blob '%s'\n", blob_name);
			fprintf(stderr, "Expected: %s\n", expected_hash);
			fprintf(stderr, "Computed: %s\n", computed_hash);
			return (EDOM);
		}

		if (!g_quiet)
			printf("Firmware blob '%s' verified successfully (SHA-256: %s)\n", 
			    blob_name, computed_hash);

		return (0);

	} else if (strcmp(command, "verify-gpg") == 0) {
		if (g_verbose)
			printf("Verifying GPG signature for firmware blob '%s'\n", blob_name);

		/* S19.2: Implement firmware GPG signature verification */
		char blob_path[PATH_MAX];
		char sig_path[PATH_MAX];
		struct stat sb;
		int fd;
		void *mmap_ptr;
		void *sig_ptr;
		size_t file_size;
		size_t sig_size;
		int rc;

		/* Get blob path from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.path", blob_name);
		error = sysctlbyname(sysctl_name, blob_path, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else {
				fprintf(stderr, "Failed to get blob path: %s\n", strerror(errno));
			}
			return (errno);
		}

		/* Get signature path from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.sig", blob_name);
		error = sysctlbyname(sysctl_name, sig_path, &len, NULL, 0);
		if (error != 0) {
			fprintf(stderr, "Signature file not available for blob '%s'\n", blob_name);
			return (errno);
		}

		/* Open and map blob file */
		fd = open(blob_path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Failed to open blob file: %s\n", strerror(errno));
			return (errno);
		}

		if (fstat(fd, &sb) < 0) {
			fprintf(stderr, "Failed to stat blob file: %s\n", strerror(errno));
			close(fd);
			return (errno);
		}

		file_size = sb.st_size;
		mmap_ptr = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mmap_ptr == MAP_FAILED) {
			fprintf(stderr, "Failed to mmap blob file: %s\n", strerror(errno));
			close(fd);
			return (errno);
		}

		/* Open and map signature file */
		int sig_fd = open(sig_path, O_RDONLY);
		if (sig_fd < 0) {
			fprintf(stderr, "Failed to open signature file: %s\n", strerror(errno));
			munmap(mmap_ptr, file_size);
			close(fd);
			return (errno);
		}

		if (fstat(sig_fd, &sb) < 0) {
			fprintf(stderr, "Failed to stat signature file: %s\n", strerror(errno));
			munmap(mmap_ptr, file_size);
			close(fd);
			close(sig_fd);
			return (errno);
		}

		sig_size = sb.st_size;
		sig_ptr = mmap(NULL, sig_size, PROT_READ, MAP_PRIVATE, sig_fd, 0);
		if (sig_ptr == MAP_FAILED) {
			fprintf(stderr, "Failed to mmap signature file: %s\n", strerror(errno));
			munmap(mmap_ptr, file_size);
			close(fd);
			close(sig_fd);
			return (errno);
		}

		/* Verify GPG signature using libsecureboot */
		rc = openpgp_verify(blob_path, mmap_ptr, file_size, 
		    sig_ptr, sig_size, 0);

		munmap(mmap_ptr, file_size);
		munmap(sig_ptr, sig_size);
		close(fd);
		close(sig_fd);

		if (rc != 0) {
			fprintf(stderr, "GPG signature verification failed for blob '%s'\n", 
			    blob_name);
			return (EAUTH);
		}

		if (!g_quiet)
			printf("Firmware blob '%s' GPG signature verified successfully\n", 
			    blob_name);

		return (0);

	} else if (strcmp(command, "check-version") == 0) {
		if (g_verbose)
			printf("Checking firmware version for blob '%s'\n", blob_name);

		/* S19.3: Implement firmware version checking */
		char blob_version[64];
		char vulnerable_versions[256];
		int is_vulnerable = 0;

		/* Get blob version from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.version", blob_name);
		len = sizeof(blob_version);
		error = sysctlbyname(sysctl_name, blob_version, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else {
				fprintf(stderr, "Failed to get blob version: %s\n", strerror(errno));
			}
			return (errno);
		}

		/* Get list of vulnerable versions from sysctl */
		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.vulnerable_versions", blob_name);
		len = sizeof(vulnerable_versions);
		error = sysctlbyname(sysctl_name, vulnerable_versions, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				/* No vulnerable versions list available */
				if (!g_quiet)
					printf("Firmware blob '%s' version %s: no vulnerability data available\n", 
					    blob_name, blob_version);
				return (0);
			} else {
				fprintf(stderr, "Failed to get vulnerable versions list: %s\n", strerror(errno));
				return (errno);
			}
		}

		/* Check if current version matches any vulnerable version */
		/* vulnerable_versions is comma-separated list */
		char *saveptr;
		char *vuln_ver = strtok_r(vulnerable_versions, ",", &saveptr);
		while (vuln_ver != NULL) {
			/* Trim leading/trailing whitespace */
			while (*vuln_ver == ' ') vuln_ver++;
			char *end = vuln_ver + strlen(vuln_ver) - 1;
			while (end > vuln_ver && *end == ' ') *end-- = '\0';

			if (strcmp(blob_version, vuln_ver) == 0) {
				is_vulnerable = 1;
				break;
			}
			vuln_ver = strtok_r(NULL, ",", &saveptr);
		}

		if (is_vulnerable) {
			fprintf(stderr, "WARNING: Firmware blob '%s' version %s is vulnerable!\n", 
			    blob_name, blob_version);
			fprintf(stderr, "Please update to a newer version.\n");
			return (EVETOKENEXP); /* Using EVETOKENEXP as "version expired/vulnerable" */
		}

		if (!g_quiet)
			printf("Firmware blob '%s' version %s: OK (not in vulnerable versions list)\n", 
			    blob_name, blob_version);

		return (0);

	} else if (strcmp(command, "delete") == 0) {
		if (g_verbose)
			printf("Deleting firmware blob '%s'\n", blob_name);

		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.delete");
		snprintf(sysctl_value, sizeof(sysctl_value), "%s", blob_name);

		error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value,
		    strlen(sysctl_value));
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else if (errno == EBUSY) {
				fprintf(stderr, "Blob '%s' is in use by an instance\n", blob_name);
			} else {
				fprintf(stderr, "Failed to delete blob: %s\n", strerror(errno));
			}
			return (errno);
		}

		if (!g_quiet)
			printf("Firmware blob '%s' deleted successfully\n", blob_name);

	} else if (strcmp(command, "info") == 0) {
		if (g_verbose)
			printf("Getting information for firmware blob '%s'\n", blob_name);

		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.%s.info", blob_name);

		char blob_info[1024];
		size_t len = sizeof(blob_info);
		error = sysctlbyname(sysctl_name, blob_info, &len, NULL, 0);
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else {
				fprintf(stderr, "Failed to get blob info: %s\n", strerror(errno));
			}
			return (errno);
		}

		printf("Firmware blob '%s':\n", blob_name);
		printf("%s\n", blob_info);

	} else {
		fprintf(stderr, "Unknown blob command: %s\n", command);
		return (EINVAL);
	}

	return (0);
}
