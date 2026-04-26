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
 * emu snapshot - Create a snapshot of an emulated instance
 *
 * Usage: emu snapshot [-v] <instance> [snapshot_name]
 */
int
emu_cmd_snapshot(int argc, char *argv[])
{
	const char *instance_name = NULL;
	const char *snapshot_name = NULL;
	char sysctl_name[PATH_MAX];
	char sysctl_value[PATH_MAX];
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			g_verbose = 1;
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, "Usage: emu snapshot [-v] <instance> [snapshot_name]\n");
		return (EINVAL);
	}

	instance_name = argv[0];
	if (argc >= 2)
		snapshot_name = argv[1];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Creating snapshot for instance '%s'", instance_name);
	if (snapshot_name)
		printf(" (name: %s)", snapshot_name);
	printf("\n");

	/*
	 * Create snapshot via sysctl interface.
	 * The kernel module will handle the snapshot creation.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.snapshot", instance_name);
	
	if (snapshot_name) {
		snprintf(sysctl_value, sizeof(sysctl_value), "%s", snapshot_name);
		error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	} else {
		/* Auto-generate snapshot name */
		error = sysctlbyname(sysctl_name, NULL, NULL, NULL, 0);
	}

	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", instance_name);
		} else if (errno == ENOSPC) {
			fprintf(stderr, "No space available for snapshot\n");
		} else if (errno == EEXIST) {
			fprintf(stderr, "Snapshot already exists\n");
		} else {
			fprintf(stderr, "Failed to create snapshot: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet) {
		if (snapshot_name)
			printf("Snapshot '%s' created for instance '%s'\n", snapshot_name, instance_name);
		else
			printf("Snapshot created for instance '%s' (auto-generated name)\n", instance_name);
	}

	return (0);
}

/*
 * emu restore - Restore an emulated instance from a snapshot
 *
 * Usage: emu restore [-v] <instance> <snapshot_name>
 */
int
emu_cmd_restore(int argc, char *argv[])
{
	const char *instance_name = NULL;
	const char *snapshot_name = NULL;
	char sysctl_name[PATH_MAX];
	char sysctl_value[PATH_MAX];
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			g_verbose = 1;
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		fprintf(stderr, "Usage: emu restore [-v] <instance> <snapshot_name>\n");
		return (EINVAL);
	}

	instance_name = argv[0];
	snapshot_name = argv[1];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Restoring instance '%s' from snapshot '%s'\n",
		    snapshot_name, instance_name);

	/*
	 * Restore from snapshot via sysctl interface.
	 * The kernel module will handle the snapshot restoration.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.restore", instance_name);
	snprintf(sysctl_value, sizeof(sysctl_value), "%s", snapshot_name);

	error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' or snapshot '%s' not found\n",
			    instance_name, snapshot_name);
		} else if (errno == EINVAL) {
			fprintf(stderr, "Cannot restore - instance must be in STOPPED state\n");
		} else {
			fprintf(stderr, "Failed to restore snapshot: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet)
		printf("Instance '%s' restored from snapshot '%s'\n",
		    instance_name, snapshot_name);

	return (0);
}
