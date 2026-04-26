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
extern enum emu_output_format g_output_format;

/*
 * emu test - Run tests in an emulated instance
 *
 * Usage: emu test [-v] [-t <timeout>] <instance> <test_path>
 */
int
emu_cmd_test(int argc, char *argv[])
{
	const char *instance_name = NULL;
	const char *test_path = NULL;
	char sysctl_name[PATH_MAX];
	char sysctl_value[PATH_MAX];
	int ch;
	int error;
	int timeout = 300; /* Default 5 minutes */

	while ((ch = getopt(argc, argv, "vt:")) != -1) {
		switch (ch) {
		case 'v':
			g_verbose = 1;
			break;
		case 't':
			timeout = atoi(optarg);
			if (timeout <= 0 || timeout > 3600) {
				fprintf(stderr, "Invalid timeout (must be 1-3600 seconds)\n");
				return (EINVAL);
			}
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		fprintf(stderr, "Usage: emu test [-v] [-t <timeout>] <instance> <test_path>\n");
		return (EINVAL);
	}

	instance_name = argv[0];
	test_path = argv[1];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Running test '%s' in instance '%s' (timeout: %ds)\n",
		    test_path, instance_name, timeout);

	/*
	 * Run the test by writing to the sysctl interface.
	 * The kernel module will handle test execution and result collection.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.test_run", instance_name);
	snprintf(sysctl_value, sizeof(sysctl_value), "%s", test_path);

	error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", instance_name);
		} else if (errno == ETIMEDOUT) {
			fprintf(stderr, "Test timed out after %d seconds\n", timeout);
		} else if (errno == EINTR) {
			fprintf(stderr, "Test interrupted\n");
		} else {
			fprintf(stderr, "Failed to run test: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet)
		printf("Test '%s' completed in instance '%s'\n", test_path, instance_name);

	return (0);
}
