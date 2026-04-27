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
 *   verify <blob_name>       - Verify blob integrity
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

		snprintf(sysctl_name, sizeof(sysctl_name),
		    "kern.emulation.blob.verify");
		snprintf(sysctl_value, sizeof(sysctl_value), "%s", blob_name);

		error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value,
		    strlen(sysctl_value));
		if (error != 0) {
			if (errno == ENOENT) {
				fprintf(stderr, "Blob '%s' not found\n", blob_name);
			} else if (errno == EDOM) {
				fprintf(stderr, "Blob '%s' verification failed - checksum mismatch\n",
				    blob_name);
			} else if (errno == EAUTH) {
				fprintf(stderr, "Blob '%s' verification failed - signature invalid\n",
				    blob_name);
			} else {
				fprintf(stderr, "Failed to verify blob: %s\n", strerror(errno));
			}
			return (errno);
		}

		if (!g_quiet)
			printf("Firmware blob '%s' verified successfully\n", blob_name);

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
