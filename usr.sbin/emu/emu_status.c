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
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Status Command
 *
 * This command displays the status of one or all emulated instances.
 */

#define EMU_INSTANCE_DIR	"/var/emu"

static char g_instance_name[EMU_NAME_MAX] = "";

static void
usage_status(void)
{
	fprintf(stderr, "Usage: emu status [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n, --name=NAME       Show status of specific instance\n");
	fprintf(stderr, "  -o, --output=FMT      Output format (text/json/tap/junit)\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu status\n");
	fprintf(stderr, "  emu status --name test-instance\n");
	fprintf(stderr, "  emu status --output json\n");
	exit(EX_USAGE);
}

static int
get_process_uptime(pid_t pid)
{
	struct kinfo_proc *kip;
	size_t kip_size;
	int mib[4];
	time_t now;
	int uptime;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = pid;

	kip_size = sizeof(*kip);
	kip = malloc(kip_size);
	if (kip == NULL)
		return (-1);

	if (sysctl(mib, 4, kip, &kip_size, NULL, 0) != 0) {
		free(kip);
		return (-1);
	}

	time(&now);
	uptime = now - kip->ki_start.tv_sec;
	free(kip);

	return (uptime > 0 ? uptime : 0);
}

static int
read_instance_state_extended(const char *name, struct emu_instance_state *state)
{
	char path[MAXPATHLEN];
	char buf[256];
	FILE *fp;
	pid_t pid = 0;
	int uptime = 0;

	memset(state, 0, sizeof(*state));
	strlcpy(state->name, name, sizeof(state->name));

	/* Read state file */
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
		state->state = EMU_STATE_UNKNOWN;
		return (0);
	}
	fclose(fp);

	/* Parse state string */
	if (strncmp(buf, "INITIALIZING", 12) == 0)
		state->state = EMU_STATE_INITIALIZING;
	else if (strncmp(buf, "STOPPED", 7) == 0)
		state->state = EMU_STATE_STOPPED;
	else if (strncmp(buf, "RUNNING", 7) == 0)
		state->state = EMU_STATE_RUNNING;
	else if (strncmp(buf, "PAUSED", 6) == 0)
		state->state = EMU_STATE_PAUSED;
	else if (strncmp(buf, "ERROR", 5) == 0)
		state->state = EMU_STATE_ERROR;
	else
		state->state = EMU_STATE_UNKNOWN;

	/* Read PID file if running */
	if (state->state == EMU_STATE_RUNNING) {
		snprintf(path, sizeof(path), "%s/%s/config/pid", EMU_INSTANCE_DIR, name);
		fp = fopen(path, "r");
		if (fp != NULL) {
			if (fscanf(fp, "%d", &pid) == 1) {
				state->pid = pid;

				/* Check if process is actually running */
				if (kill(pid, 0) != 0) {
					if (errno == ESRCH) {
						/* Process dead but state says running */
						state->state = EMU_STATE_ERROR;
						if (g_verbose)
							warnx("Instance %s: PID %d not found", name, pid);
					}
				} else {
					/* Process is running, get uptime */
					uptime = get_process_uptime(pid);
					if (uptime >= 0)
						state->uptime_sec = uptime;
				}
			}
			fclose(fp);
		}
	}

	/* Read configuration */
	snprintf(path, sizeof(path), "%s/%s/config/config.json", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp != NULL) {
		char line[256];
		while (fgets(line, sizeof(line), fp) != NULL) {
			if (strstr(line, "\"arch\"") != NULL) {
				char *p = strstr(line, ":");
				if (p != NULL) {
					p++;
					while (*p == ' ' || *p == '"')
						p++;
					char arch_str[16];
					size_t i = 0;
					while (*p != '"' && *p != '\0' && i < sizeof(arch_str) - 1)
						arch_str[i++] = *p++;
					arch_str[i] = '\0';
					state->arch = emu_string_to_arch(arch_str);
				}
				break;
			}
		}
		fclose(fp);
	}

	if (state->arch == EMU_ARCH_UNKNOWN)
		state->arch = EMU_ARCH_AMD64;

	state->mode = EMU_MODE_AUTO;

	/* Read console output size */
	snprintf(path, sizeof(path), "%s/%s/console/output.log", EMU_INSTANCE_DIR, name);
	struct stat sb;
	if (stat(path, &sb) == 0)
		state->console_size = sb.st_size;

	return (0);
}

static void
output_status_detailed(const struct emu_instance_state *state)
{
	switch (g_output_format) {
	case EMU_OUTPUT_JSON:
		emu_output_json_begin();
		emu_output_json_string("name", state->name);
		emu_output_json_string("arch", emu_arch_to_string(state->arch));
		emu_output_json_string("mode", emu_mode_to_string(state->mode));
		emu_output_json_string("state", emu_state_to_string(state->state));
		emu_output_json_int("pid", state->pid);
		emu_output_json_uint("memory_used", state->memory_used);
		emu_output_json_uint("uptime_sec", state->uptime_sec);
		emu_output_json_uint("console_size", state->console_size);
		emu_output_json_end();
		break;

	case EMU_OUTPUT_TEXT:
		printf("Instance: %s\n", state->name);
		printf("  Architecture: %s\n", emu_arch_to_string(state->arch));
		printf("  Mode: %s\n", emu_mode_to_string(state->mode));
		printf("  State: %s\n", emu_state_to_string(state->state));
		if (state->pid != 0)
			printf("  PID: %d\n", state->pid);
		if (state->uptime_sec != 0) {
			int days = state->uptime_sec / 86400;
			int hours = (state->uptime_sec % 86400) / 3600;
			int mins = (state->uptime_sec % 3600) / 60;
			int secs = state->uptime_sec % 60;
			printf("  Uptime: ");
			if (days > 0)
				printf("%dd ", days);
			if (hours > 0 || days > 0)
				printf("%dh ", hours);
			printf("%dm %ds\n", mins, secs);
		}
		if (state->memory_used != 0)
			printf("  Memory: %zu KB\n", state->memory_used / 1024);
		if (state->console_size != 0)
			printf("  Console: %zu bytes\n", state->console_size);
		break;

	case EMU_OUTPUT_TAP:
	case EMU_OUTPUT_JUNIT:
		/* Not typically used for status output */
		break;
	}
}

static int
show_status(const char *name)
{
	struct emu_instance_state state;
	int error;

	if (name != NULL && name[0] != '\0') {
		/* Show status of specific instance */
		error = read_instance_state_extended(name, &state);
		if (error != 0)
			return (error);

		output_status_detailed(&state);
	} else {
		/* Show summary of all instances - delegate to list command */
		if (g_verbose)
			printf("Showing status of all instances...\n");

		/* For now, just call list_instances logic */
		/* In a full implementation, this would show a summary table */
		warnx("Use 'emu list' to see all instances");
		return (0);
	}

	return (0);
}

int
emu_cmd_status(int argc, char *argv[])
{
	int ch;
	int option_index;
	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'n' },
		{ "output", required_argument, NULL, 'o' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "n:o:vh",
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

		case 'o':
			if (strcmp(optarg, "text") == 0)
				g_output_format = EMU_OUTPUT_TEXT;
			else if (strcmp(optarg, "json") == 0)
				g_output_format = EMU_OUTPUT_JSON;
			else if (strcmp(optarg, "tap") == 0)
				g_output_format = EMU_OUTPUT_TAP;
			else if (strcmp(optarg, "junit") == 0)
				g_output_format = EMU_OUTPUT_JUNIT;
			else {
				warnx("Unknown output format: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_status();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		warnx("Unexpected arguments");
		return (EX_USAGE);
	}

	const char *name = (g_instance_name[0] != '\0') ? g_instance_name : NULL;
	int result = show_status(name);
	if (result != 0)
		return (EX_OSERR);

	return (0);
}
