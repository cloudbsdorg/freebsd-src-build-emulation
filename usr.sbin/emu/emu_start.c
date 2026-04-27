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
#include <unistd.h>

#include "emu.h"

extern int g_verbose;
extern int g_quiet;

#define EMU_BHYVE_PATH		"/usr/sbin/bhyve"
#define EMU_EMULATOR_PATH	"/usr/sbin/emu_engine"

/*
 * emu start - Start an emulated instance
 *
 * Usage: emu start [--name <name>] [--mode <bhyve|emulator>] [-v]
 */
int
emu_cmd_start(int argc, char *argv[])
{
	const char *name = NULL;
	const char *mode_str = NULL;
	enum emu_mode mode = EMU_MODE_AUTO;
	char sysctl_name[PATH_MAX];
	char sysctl_value[PATH_MAX];
	char instance_dir[PATH_MAX];
	char config_file[PATH_MAX];
	char *arch = NULL;
	int ch;
	int error;

	while ((ch = getopt(argc, argv, "m:n:v")) != -1) {
		switch (ch) {
		case 'm':
			mode_str = optarg;
			if (strcmp(mode_str, "bhyve") == 0)
				mode = EMU_MODE_BHYVE;
			else if (strcmp(mode_str, "emulator") == 0)
				mode = EMU_MODE_EMULATOR;
			else if (strcmp(mode_str, "auto") == 0)
				mode = EMU_MODE_AUTO;
			else {
				fprintf(stderr, "Invalid mode '%s' (must be bhyve|emulator|auto)\n",
				    mode_str);
				return (EINVAL);
			}
			break;
		case 'n':
			name = optarg;
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
		fprintf(stderr, "Usage: emu start [--name <name>] [--mode <bhyve|emulator|auto>] [-v]\n");
		return (EINVAL);
	}

	if (strlen(name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	/* Read instance configuration */
	const char *data_dir = getenv("XDG_DATA_HOME");
	if (data_dir == NULL) {
		data_dir = getenv("HOME");
		if (data_dir == NULL) {
			fprintf(stderr, "HOME environment variable not set\n");
			return (ENOENT);
		}
		snprintf(instance_dir, sizeof(instance_dir),
		    "%s/.local/share/emu/instances/%s", data_dir, name);
	} else {
		snprintf(instance_dir, sizeof(instance_dir),
		    "%s/emu/instances/%s", data_dir, name);
	}

	snprintf(config_file, sizeof(config_file), "%s/config", instance_dir);
	FILE *fp = fopen(config_file, "r");
	if (fp == NULL) {
		fprintf(stderr, "Instance '%s' not found (config file: %s)\n",
		    name, config_file);
		return (ENOENT);
	}

	/* Parse configuration file */
	char line[1024];
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		
		char *key = strtok(line, "=");
		char *value = strtok(NULL, "\n");
		
		if (strcmp(key, "arch") == 0)
			arch = strdup(value);
	}
	fclose(fp);

	if (arch == NULL) {
		fprintf(stderr, "Invalid configuration - missing architecture\n");
		return (EINVAL);
	}

	if (g_verbose) {
		printf("Starting instance '%s'\n", name);
		printf("  Architecture: %s\n", arch);
		printf("  Mode: %s\n", mode == EMU_MODE_BHYVE ? "bhyve" :
		    mode == EMU_MODE_EMULATOR ? "emulator" : "auto");
	}

	/* Auto-detect mode if not specified */
	if (mode == EMU_MODE_AUTO) {
		/* Check if target arch matches host arch */
		char host_arch[32];
		size_t len = sizeof(host_arch);
		
		if (sysctlbyname("kern.arch", host_arch, &len, NULL, 0) == 0) {
			if (strcmp(host_arch, arch) == 0) {
				/* Native arch - check if VMM is available */
				int vmm_available = 0;
				len = sizeof(vmm_available);
				if (sysctlbyname("hw.vmm.available", &vmm_available, &len, NULL, 0) == 0 &&
				    vmm_available) {
					mode = EMU_MODE_BHYVE;
				} else {
					mode = EMU_MODE_EMULATOR;
				}
			} else {
				/* Cross-architecture - must use emulator */
				mode = EMU_MODE_EMULATOR;
			}
		} else {
			mode = EMU_MODE_EMULATOR;
		}

		if (g_verbose)
			printf("Auto-detected mode: %s\n", mode == EMU_MODE_BHYVE ? "bhyve" : "emulator");
	}

	/* Start instance via kernel module sysctl */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.start", name);
	snprintf(sysctl_value, sizeof(sysctl_value), "%s",
	    mode == EMU_MODE_BHYVE ? "bhyve" : "emulator");

	error = sysctlbyname(sysctl_name, NULL, NULL, sysctl_value, strlen(sysctl_value));
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not initialized\n", name);
		} else if (errno == EBUSY) {
			fprintf(stderr, "Instance '%s' is already running\n", name);
		} else if (errno == EINVAL) {
			fprintf(stderr, "Invalid mode or architecture mismatch\n");
		} else {
			fprintf(stderr, "Failed to start instance: %s\n", strerror(errno));
		}
		free(arch);
		return (errno);
	}

	if (!g_quiet)
		printf("Instance '%s' started successfully (mode: %s)\n", name,
		    mode == EMU_MODE_BHYVE ? "bhyve" : "emulator");

	free(arch);
	return (0);
}
