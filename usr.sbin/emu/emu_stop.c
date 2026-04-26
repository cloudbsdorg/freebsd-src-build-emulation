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

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"

extern int g_verbose;
extern int g_quiet;

/*
 * emu stop - Stop an emulated instance
 *
 * Usage: emu stop [--name <name>] [--force] [--timeout <seconds>] [-v]
 */
int
emu_cmd_stop(int argc, char *argv[])
{
	const char *name = NULL;
	char sysctl_name[PATH_MAX];
	int force = 0;
	int timeout = 30; /* Default 30 seconds */
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "fn:t:v")) != -1) {
		switch (ch) {
		case 'f':
			force = 1;
			break;
		case 'n':
			name = optarg;
			break;
		case 't':
			timeout = atoi(optarg);
			if (timeout < 1 || timeout > 300) {
				fprintf(stderr, "Invalid timeout (must be 1-300 seconds)\n");
				return (EINVAL);
			}
			break;
		case 'v':
			g_verbose = 1;
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (name == NULL) {
		fprintf(stderr, "Usage: emu stop [--name <name>] [--force] [--timeout <seconds>] [-v]\n");
		return (EINVAL);
	}

	if (strlen(name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Stopping instance '%s' (force: %s, timeout: %ds)\n",
		    name, force ? "yes" : "no", timeout);

	/* Stop instance via kernel module sysctl */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.stop", name);

	if (force) {
		/* Force kill - immediate termination */
		error = sysctlbyname(sysctl_name, NULL, NULL, "force", strlen("force"));
	} else {
		/* Graceful shutdown with timeout */
		char timeout_str[16];
		snprintf(timeout_str, sizeof(timeout_str), "%d", timeout);
		error = sysctlbyname(sysctl_name, NULL, NULL, timeout_str, strlen(timeout_str));
	}

	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", name);
		} else if (errno == ETIMEDOUT) {
			fprintf(stderr, "Instance '%s' did not stop within %d seconds\n",
			    name, timeout);
			fprintf(stderr, "Use --force to kill immediately\n");
		} else if (errno == EPERM) {
			fprintf(stderr, "Permission denied - you don't own instance '%s'\n", name);
		} else {
			fprintf(stderr, "Failed to stop instance: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet)
		printf("Instance '%s' stopped successfully\n", name);

	return (0);
}

/*
 * emu status - Show status of emulated instances
 *
 * Usage: emu status [--name <name>] [--output-format <text|json>] [-v]
 */
int
emu_cmd_status(int argc, char *argv[])
{
	const char *name = NULL;
	char sysctl_name[PATH_MAX];
	char sysctl_value[1024];
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "n:o:v")) != -1) {
		switch (ch) {
		case 'n':
			name = optarg;
			break;
		case 'o':
			if (strcmp(optarg, "json") == 0)
				g_output_format = EMU_OUTPUT_JSON;
			else if (strcmp(optarg, "text") == 0)
				g_output_format = EMU_OUTPUT_TEXT;
			else {
				fprintf(stderr, "Invalid output format '%s' (must be json|text)\n",
				    optarg);
				return (EINVAL);
			}
			break;
		case 'v':
			g_verbose = 1;
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (name != NULL && strlen(name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	/* Get instance status via sysctl */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.status", name ? name : "all");

	size_t len = sizeof(sysctl_value);
	error = sysctlbyname(sysctl_name, sysctl_value, &len, NULL, 0);
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found\n", name);
		} else {
			fprintf(stderr, "Failed to get status: %s\n", strerror(errno));
		}
		return (errno);
	}

	/* Output status based on format */
	switch (g_output_format) {
	case EMU_OUTPUT_JSON:
		emu_output_json_begin();
		emu_output_json_object_begin("status");
		/* Parse sysctl_value (format: "state=RUNNING,pid=1234,memory=536870912,...") */
		/* TODO: Implement proper JSON output parsing */
		emu_output_json_string("raw", sysctl_value);
		emu_output_json_object_end(0);
		emu_output_json_end();
		break;

	case EMU_OUTPUT_TEXT:
	default:
		printf("Instance '%s' status:\n", name ? name : "all");
		printf("%s\n", sysctl_value);
		break;
	}

	return (0);
}

/*
 * emu list - List all emulated instances
 *
 * Usage: emu list [--arch <arch>] [--status <status>] [--output-format <text|json>] [-v]
 */
int
emu_cmd_list(int argc, char *argv[])
{
	const char *filter_arch = NULL;
	const char *filter_status = NULL;
	char sysctl_name[PATH_MAX];
	char instance_list[4096];
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "a:s:o:v")) != -1) {
		switch (ch) {
		case 'a':
			filter_arch = optarg;
			break;
		case 's':
			filter_status = optarg;
			break;
		case 'o':
			if (strcmp(optarg, "json") == 0)
				g_output_format = EMU_OUTPUT_JSON;
			else if (strcmp(optarg, "text") == 0)
				g_output_format = EMU_OUTPUT_TEXT;
			else {
				fprintf(stderr, "Invalid output format '%s' (must be json|text)\n",
				    optarg);
				return (EINVAL);
			}
			break;
		case 'v':
			g_verbose = 1;
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (g_verbose) {
		printf("Listing instances");
		if (filter_arch)
			printf(" (arch: %s)", filter_arch);
		if (filter_status)
			printf(" (status: %s)", filter_status);
		printf("\n");
	}

	/* Get instance list via sysctl */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instances");

	size_t len = sizeof(instance_list);
	error = sysctlbyname(sysctl_name, instance_list, &len, NULL, 0);
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Emulation framework not loaded\n");
		} else {
			fprintf(stderr, "Failed to get instance list: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (len == 0) {
		if (!g_quiet)
			printf("No instances found\n");
		return (0);
	}

	/* Output based on format */
	switch (g_output_format) {
	case EMU_OUTPUT_JSON:
		emu_output_json_begin();
		emu_output_json_array_begin("instances");
		/* Parse comma-separated list and output as JSON array */
		/* TODO: Implement proper JSON array output */
		emu_output_json_string("raw", instance_list);
		emu_output_json_array_end(0);
		emu_output_json_end();
		break;

	case EMU_OUTPUT_TEXT:
	default:
		printf("%-20s %-10s %-10s %-10s %s\n",
		    "NAME", "ARCH", "MODE", "STATUS", "UPTIME");
		printf("%-20s %-10s %-10s %-10s %s\n",
		    "--------------------", "----------", "----------", "----------", "----------");
		/* Parse comma-separated list and display as table */
		/* TODO: Implement proper table output */
		printf("%s\n", instance_list);
		break;
	}

	return (0);
}
