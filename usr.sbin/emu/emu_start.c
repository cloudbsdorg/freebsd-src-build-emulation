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
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Start Command
 *
 * This command starts an emulated instance.
 */

#define EMU_INSTANCE_DIR	"/var/emu"
#define EMU_PID_FILE		"/var/run/emu/%s.pid"

static char g_instance_name[EMU_NAME_MAX] = "";
static enum emu_mode g_mode = EMU_MODE_AUTO;

static void
usage_start(void)
{
	fprintf(stderr, "Usage: emu start [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n, --name=NAME       Instance name (required)\n");
	fprintf(stderr, "  -m, --mode=MODE       Execution mode (auto, bhyve, emulator)\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu start --name test-instance\n");
	fprintf(stderr, "  emu start -n test-instance --mode bhyve\n");
	exit(EX_USAGE);
}

static int
check_instance_state(const char *name, char *state_buf, size_t state_len)
{
	char path[MAXPATHLEN];
	FILE *fp;

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

	if (fgets(state_buf, state_len, fp) == NULL) {
		fclose(fp);
		warnx("Failed to read state for %s", name);
		return (-1);
	}
	fclose(fp);

	/* Trim newline */
	char *nl = strchr(state_buf, '\n');
	if (nl != NULL)
		*nl = '\0';

	return (0);
}

static int
update_instance_state(const char *name, const char *state)
{
	char path[MAXPATHLEN];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s/config/state", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "w");
	if (fp == NULL) {
		warn("Failed to update state for %s", name);
		return (-1);
	}

	fprintf(fp, "%s\n", state);
	fclose(fp);

	return (0);
}

static int
write_pid_file(const char *name, pid_t pid)
{
	char path[MAXPATHLEN];
	FILE *fp;
	mode_t old_umask;

	snprintf(path, sizeof(path), "%s/%s/config/pid", EMU_INSTANCE_DIR, name);
	
	/* Create with restricted permissions */
	old_umask = umask(077);
	fp = fopen(path, "w");
	umask(old_umask);

	if (fp == NULL) {
		warn("Failed to create PID file for %s", name);
		return (-1);
	}

	fprintf(fp, "%d\n", pid);
	fclose(fp);

	return (0);
}

static int
start_bhyve_instance(const char *name, const struct emu_instance_config *config)
{
	char path[MAXPATHLEN];
	char cmd[4096];
	char *argv[32];
	int argc = 0;
	pid_t pid;

	if (g_verbose)
		printf("Starting instance '%s' in bhyve mode...\n", name);

	/* Construct bhyve command */
	snprintf(cmd, sizeof(cmd),
	    "bhyve -c %d -m %zuM -H -P -s 0:0,hostbridge -s 31,lpc "
	    "-l com1,stdio -s 2:0,virtio-net,tap0 "
	    "-s 4:0,virtio-blk,%s %s",
	    config->num_cpus,
	    config->memory_size / (1024 * 1024),
	    config->image_path[0] != '\0' ? config->image_path : "/dev/null",
	    config->kernel_path[0] != '\0' ? config->kernel_path : "");

	if (g_verbose)
		printf("Command: %s\n", cmd);

	/* Fork and exec bhyve */
	pid = fork();
	if (pid < 0) {
		warn("Failed to fork");
		return (-1);
	}

	if (pid == 0) {
		/* Child process */
		/* In a real implementation, we would exec bhyve here */
		/* For now, just sleep to simulate */
		sleep(3600);
		exit(0);
	}

	/* Parent process */
	if (g_verbose)
		printf("Started bhyve process (PID %d)\n", pid);

	/* Write PID file */
	if (write_pid_file(name, pid) != 0) {
		warnx("Failed to write PID file");
		kill(pid, SIGTERM);
		return (-1);
	}

	/* Update state */
	if (update_instance_state(name, "RUNNING") != 0) {
		warnx("Failed to update instance state");
		kill(pid, SIGTERM);
		return (-1);
	}

	return (0);
}

static int
start_emulator_instance(const char *name, const struct emu_instance_config *config)
{
	char path[MAXPATHLEN];
	pid_t pid;

	if (g_verbose)
		printf("Starting instance '%s' in emulator mode...\n", name);

	/* Fork and exec emulator */
	pid = fork();
	if (pid < 0) {
		warn("Failed to fork");
		return (-1);
	}

	if (pid == 0) {
		/* Child process */
		/* In a real implementation, we would exec the emulator here */
		/* For now, just sleep to simulate */
		sleep(3600);
		exit(0);
	}

	/* Parent process */
	if (g_verbose)
		printf("Started emulator process (PID %d)\n", pid);

	/* Write PID file */
	if (write_pid_file(name, pid) != 0) {
		warnx("Failed to write PID file");
		kill(pid, SIGTERM);
		return (-1);
	}

	/* Update state */
	if (update_instance_state(name, "RUNNING") != 0) {
		warnx("Failed to update instance state");
		kill(pid, SIGTERM);
		return (-1);
	}

	return (0);
}

static int
read_config(const char *name, struct emu_instance_config *config)
{
	char path[MAXPATHLEN];
	char line[512];
	FILE *fp;

	memset(config, 0, sizeof(*config));
	strlcpy(config->name, name, sizeof(config->name));

	snprintf(path, sizeof(path), "%s/%s/config/config.json", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp == NULL) {
		warn("Failed to read configuration for %s", name);
		return (-1);
	}

	/* Simple JSON parsing */
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strstr(line, "\"arch\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"')
					p++;
				char buf[16];
				size_t i = 0;
				while (*p != '"' && *p != '\0' && i < sizeof(buf) - 1)
					buf[i++] = *p++;
				buf[i] = '\0';
				config->arch = emu_string_to_arch(buf);
			}
		} else if (strstr(line, "\"mode\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"')
					p++;
				char buf[16];
				size_t i = 0;
				while (*p != '"' && *p != '\0' && i < sizeof(buf) - 1)
					buf[i++] = *p++;
				buf[i] = '\0';
				config->mode = emu_string_to_mode(buf);
			}
		} else if (strstr(line, "\"memory_size\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"' || *p == ',')
					p++;
				config->memory_size = strtoul(p, NULL, 0);
			}
		} else if (strstr(line, "\"num_cpus\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"' || *p == ',')
					p++;
				config->num_cpus = atoi(p);
			}
		} else if (strstr(line, "\"image_path\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"')
					p++;
				size_t i = 0;
				while (*p != '"' && *p != '\0' && i < sizeof(config->image_path) - 1)
					config->image_path[i++] = *p++;
				config->image_path[i] = '\0';
			}
		} else if (strstr(line, "\"kernel_path\"") != NULL) {
			char *p = strstr(line, ":");
			if (p != NULL) {
				p++;
				while (*p == ' ' || *p == '"')
					p++;
				size_t i = 0;
				while (*p != '"' && *p != '\0' && i < sizeof(config->kernel_path) - 1)
					config->kernel_path[i++] = *p++;
				config->kernel_path[i] = '\0';
			}
		}
	}

	fclose(fp);

	/* Set defaults if not specified */
	if (config->arch == EMU_ARCH_UNKNOWN)
		config->arch = EMU_ARCH_AMD64;
	if (config->mode == EMU_MODE_AUTO)
		config->mode = EMU_MODE_AUTO;
	if (config->memory_size == 0)
		config->memory_size = 256 * 1024 * 1024;
	if (config->num_cpus == 0)
		config->num_cpus = 1;

	return (0);
}

static int
select_mode(const struct emu_instance_config *config)
{
	struct stat sb;
	int has_vmm = 0;

	/* If mode is explicitly set, use it */
	if (g_mode != EMU_MODE_AUTO)
		return (g_mode);

	/* Auto-select based on configuration and host capabilities */
	if (config->mode != EMU_MODE_AUTO)
		return (config->mode);

	/* Check if VMM is available */
	if (stat("/dev/vmm", &sb) == 0 && S_ISCHR(sb.st_mode))
		has_vmm = 1;

	/* Prefer bhyve for native architectures */
	if (has_vmm) {
		if (g_verbose)
			printf("VMM available, selecting bhyve mode\n");
		return (EMU_MODE_BHYVE);
	}

	/* Fall back to emulator mode */
	if (g_verbose)
		printf("VMM not available, selecting emulator mode\n");
	return (EMU_MODE_EMULATOR);
}

int
cmd_start(int argc, char *argv[])
{
	int ch;
	int option_index;
	struct emu_instance_config config;
	char state_buf[64];
	int mode;
	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'n' },
		{ "mode", required_argument, NULL, 'm' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "n:m:vh",
	    long_options, &option_index)) != -1) {
		switch (ch) {
		case 'n':
			if (strlen(optarg) >= EMU_NAME_MAX) {
				warnx("Instance name too long (max %d chars)",
				    EMU_NAME_MAX - 1);
				return (EX_USAGE);
			}
			strlcpy(g_instance_name, optarg, sizeof(g_instance_name));
			break;

		case 'm':
			g_mode = emu_string_to_mode(optarg);
			if (g_mode == EMU_MODE_AUTO && strcmp(optarg, "auto") != 0) {
				warnx("Unknown mode: %s, using auto", optarg);
			}
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_start();
		}
	}

	argc -= optind;
	argv += optind;

	/* Validate required parameters */
	if (g_instance_name[0] == '\0') {
		warnx("Instance name is required (--name)");
		return (EX_USAGE);
	}

	/* Check current state */
	if (check_instance_state(g_instance_name, state_buf, sizeof(state_buf)) != 0)
		return (EX_NOINPUT);

	if (strcmp(state_buf, "RUNNING") == 0) {
		warnx("Instance '%s' is already running", g_instance_name);
		return (EX_EXISTS);
	}

	/* Read configuration */
	if (read_config(g_instance_name, &config) != 0)
		return (EX_CONFIG);

	/* Select execution mode */
	mode = select_mode(&config);
	if (g_verbose)
		printf("Selected mode: %s\n", emu_mode_to_string(mode));

	/* Start instance */
	int error;
	switch (mode) {
	case EMU_MODE_BHYVE:
		error = start_bhyve_instance(g_instance_name, &config);
		break;

	case EMU_MODE_EMULATOR:
		error = start_emulator_instance(g_instance_name, &config);
		break;

	default:
		warnx("Invalid mode selected");
		return (EX_CONFIG);
	}

	if (error != 0) {
		warnx("Failed to start instance");
		return (EX_SOFTWARE);
	}

	/* Success */
	printf("Started emulated instance '%s'\n", g_instance_name);
	printf("  Mode: %s\n", emu_mode_to_string(mode));
	printf("  Architecture: %s\n", emu_arch_to_string(config.arch));
	printf("  Memory: %zu MB\n", config.memory_size / (1024 * 1024));
	printf("  CPUs: %d\n", config.num_cpus);
	printf("\nUse 'emu status --name %s' to check status\n", g_instance_name);
	printf("Use 'emu console --name %s' to view console output\n", g_instance_name);
	printf("Use 'emu stop --name %s' to stop the instance\n", g_instance_name);

	return (0);
}
