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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Destroy Command
 *
 * This command destroys an emulated instance, cleaning up all associated
 * resources.
 */

#define EMU_INSTANCE_DIR	"/var/emu"

static int g_force = 0;

static void
usage_destroy(void)
{
	fprintf(stderr, "Usage: emu destroy [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n, --name=NAME       Instance name (required)\n");
	fprintf(stderr, "  -f, --force           Force destruction without confirmation\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu destroy --name test-instance\n");
	fprintf(stderr, "  emu destroy -n test-instance --force\n");
	exit(EX_USAGE);
}

static int
stop_instance(const char *name)
{
	char path[MAXPATHLEN];
	char buf[64];
	FILE *fp;
	pid_t pid = 0;

	/* Read current state */
	snprintf(path, sizeof(path), "%s/%s/config/state", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			warnx("Instance '%s' does not exist", name);
			return (-1);
		}
		warn("Failed to read state for %s", name);
		return (-1);
	}

	if (fgets(buf, sizeof(buf), fp) == NULL) {
		fclose(fp);
		warnx("Failed to read state for %s", name);
		return (-1);
	}
	fclose(fp);

	/* Check if instance is running */
	if (strncmp(buf, "RUNNING", 7) == 0) {
		/* Try to read PID file */
		snprintf(path, sizeof(path), "%s/%s/config/pid", EMU_INSTANCE_DIR, name);
		fp = fopen(path, "r");
		if (fp != NULL) {
			if (fscanf(fp, "%d", &pid) == 1) {
				if (g_verbose)
					printf("Stopping instance %s (PID %d)...\n", name, pid);

				/* Send SIGTERM to stop the instance */
				if (kill(pid, SIGTERM) != 0) {
					if (errno == ESRCH) {
						/* Process already dead */
						if (g_verbose)
							printf("Process already dead\n");
					} else {
						warn("Failed to send SIGTERM to %d", pid);
						if (!g_force) {
							/* Try SIGKILL */
							if (g_verbose)
								printf("Sending SIGKILL...\n");
							if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
								warn("Failed to kill %d", pid);
							}
						}
					}
				}

				/* Wait a bit for process to exit */
				usleep(100000); /* 100ms */

				/* Check if process is still running */
				if (kill(pid, 0) == 0) {
					if (g_verbose)
						printf("Process still running, waiting...\n");
					usleep(500000); /* 500ms */

					/* Force kill if still running */
					if (kill(pid, 0) == 0 && !g_force) {
						if (g_verbose)
							printf("Sending SIGKILL...\n");
						kill(pid, SIGKILL);
					}
				}

				fclose(fp);
			}
		}
	}

	return (0);
}

static int
cleanup_instance_directory(const char *name)
{
	char path[MAXPATHLEN];
	char cmd[MAXPATHLEN + 64];
	int error;

	/* Construct path to instance directory */
	snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, name);

	if (g_verbose)
		printf("Removing instance directory: %s\n", path);

	/* Use rm -rf to remove directory and all contents */
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	error = system(cmd);
	if (error != 0) {
		warnx("Failed to remove instance directory (exit code %d)", error);
		return (-1);
	}

	return (0);
}

static int
confirm_destruction(const char *name)
{
	char response[16];

	if (g_force)
		return (1);

	fprintf(stderr, "This will permanently destroy instance '%s' and all its data.\n", name);
	fprintf(stderr, "Are you sure? [y/N] ");

	if (fgets(response, sizeof(response), stdin) == NULL)
		return (0);

	return (response[0] == 'y' || response[0] == 'Y');
}

int
emu_cmd_destroy(int argc, char *argv[])
{
	int ch;
	int option_index;
	char name[EMU_NAME_MAX] = "";
	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'n' },
		{ "force", no_argument, NULL, 'f' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "n:fvh",
	    long_options, &option_index)) != -1) {
		switch (ch) {
		case 'n':
			if (strlen(optarg) >= EMU_NAME_MAX) {
				warnx("Instance name too long (max %d chars)",
				    EMU_NAME_MAX - 1);
				return (EX_USAGE);
			}
			strlcpy(name, optarg, sizeof(name));
			break;

		case 'f':
			g_force = 1;
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_destroy();
		}
	}

	argc -= optind;
	argv += optind;

	/* Validate required parameters */
	if (name[0] == '\0') {
		warnx("Instance name is required (--name)");
		return (EX_USAGE);
	}

	/* Check if instance exists */
	char path[MAXPATHLEN];
	struct stat sb;
	snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, name);
	if (stat(path, &sb) != 0) {
		if (errno == ENOENT) {
			warnx("Instance '%s' does not exist", name);
			return (EX_NOINPUT);
		}
		warn("Failed to stat instance directory: %s", path);
		return (EX_OSERR);
	}

	/* Confirm destruction */
	if (!confirm_destruction(name)) {
		printf("Destruction cancelled\n");
		return (0);
	}

	/* Stop instance if running */
	if (g_verbose)
		printf("Stopping instance %s...\n", name);
	if (stop_instance(name) != 0) {
		if (!g_force) {
			warnx("Failed to stop instance, use --force to destroy anyway");
			return (EX_IOERR);
		}
		if (g_verbose)
			printf("Continuing despite stop failure (--force)\n");
	}

	/* Clean up instance directory */
	if (cleanup_instance_directory(name) != 0) {
		warnx("Failed to clean up instance directory");
		return (EX_CANTCREAT);
	}

	/* Success */
	printf("Destroyed emulated instance '%s'\n", name);

	return (0);
}
