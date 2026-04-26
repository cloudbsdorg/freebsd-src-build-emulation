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
#include <sys/module.h>
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
 * emu load - Load a kernel module into an emulated instance
 *
 * Usage: emu load [-v] <instance> <module_path>
 */
int
emu_cmd_load(int argc, char *argv[])
{
	const char *instance_name = NULL;
	const char *module_path = NULL;
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
		fprintf(stderr, "Usage: emu load [-v] <instance> <module_path>\n");
		return (EINVAL);
	}

	instance_name = argv[0];
	module_path = argv[1];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Loading module '%s' into instance '%s'\n", module_path, instance_name);

	/*
	 * Load the module by writing to the sysctl interface.
	 * The kernel module will handle the actual loading.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.module_load", instance_name);
	snprintf(sysctl_value, sizeof(sysctl_value), "%s", module_path);

	error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", instance_name);
		} else if (errno == EINVAL) {
			fprintf(stderr, "Invalid module path or module already loaded\n");
		} else {
			fprintf(stderr, "Failed to load module: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet)
		printf("Module '%s' loaded successfully into instance '%s'\n",
		    module_path, instance_name);

	return (0);
}

/*
 * emu unload - Unload a kernel module from an emulated instance
 *
 * Usage: emu unload [-v] <instance> <module_name>
 */
int
emu_cmd_unload(int argc, char *argv[])
{
	const char *instance_name = NULL;
	const char *module_name = NULL;
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
		fprintf(stderr, "Usage: emu unload [-v] <instance> <module_name>\n");
		return (EINVAL);
	}

	instance_name = argv[0];
	module_name = argv[1];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Unloading module '%s' from instance '%s'\n", module_name, instance_name);

	/*
	 * Unload the module by writing to the sysctl interface.
	 * The kernel module will handle the actual unloading.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.module_unload", instance_name);
	snprintf(sysctl_value, sizeof(sysctl_value), "%s", module_name);

	error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", instance_name);
		} else if (errno == ENOENT) {
			fprintf(stderr, "Module '%s' not loaded in instance '%s'\n",
			    module_name, instance_name);
		} else if (errno == EBUSY) {
			fprintf(stderr, "Module '%s' is in use and cannot be unloaded\n", module_name);
		} else {
			fprintf(stderr, "Failed to unload module: %s\n", strerror(errno));
		}
		return (errno);
	}

	if (!g_quiet)
		printf("Module '%s' unloaded successfully from instance '%s'\n",
		    module_name, instance_name);

	return (0);
}
