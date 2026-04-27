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
#include <sys/dirent.h>
#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - List Command
 *
 * This command lists all emulated instances, optionally filtered by
 * architecture or status.
 */

#define EMU_INSTANCE_DIR	"/var/emu"

static enum emu_arch g_filter_arch = EMU_ARCH_UNKNOWN;
static enum emu_state g_filter_state = EMU_STATE_UNKNOWN;

static void
usage_list(void)
{
	fprintf(stderr, "Usage: emu list [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -a, --arch=ARCH       Filter by architecture\n");
	fprintf(stderr, "  -s, --status=STATE    Filter by instance state\n");
	fprintf(stderr, "  -o, --output=FMT      Output format (text/json/tap/junit)\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu list\n");
	fprintf(stderr, "  emu list --arch amd64\n");
	fprintf(stderr, "  emu list --status running --output json\n");
	exit(EX_USAGE);
}

static int
read_instance_state(const char *name, struct emu_instance_state *state)
{
	char path[MAXPATHLEN];
	char buf[64];
	FILE *fp;

	memset(state, 0, sizeof(*state));
	strlcpy(state->name, name, sizeof(state->name));

	/* Read state file */
	snprintf(path, sizeof(path), "%s/%s/config/state", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			/* Instance directory exists but no state - corrupted? */
			strlcpy(state->name, name, sizeof(state->name));
			state->state = EMU_STATE_ERROR;
			return (0);
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

	/* Read configuration to get architecture */
	snprintf(path, sizeof(path), "%s/%s/config/config.json", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp != NULL) {
		/* Simple parsing - look for "arch" field */
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

	/* Default to unknown if not found */
	if (state->arch == EMU_ARCH_UNKNOWN)
		state->arch = EMU_ARCH_AMD64;

	/* Default mode to auto */
	state->mode = EMU_MODE_AUTO;

	return (0);
}

static int
should_include_instance(const struct emu_instance_state *state)
{
	/* Apply architecture filter */
	if (g_filter_arch != EMU_ARCH_UNKNOWN &&
	    state->arch != g_filter_arch)
		return (0);

	/* Apply state filter */
	if (g_filter_state != EMU_STATE_UNKNOWN &&
	    state->state != g_filter_state)
		return (0);

	return (1);
}

static void
output_instance_json(const struct emu_instance_state *state)
{
	emu_output_json_object_begin(NULL);
	emu_output_json_string("name", state->name);
	emu_output_json_string("arch", emu_arch_to_string(state->arch));
	emu_output_json_string("mode", emu_mode_to_string(state->mode));
	emu_output_json_string("state", emu_state_to_string(state->state));
	emu_output_json_int("pid", state->pid);
	emu_output_json_uint("memory_used", state->memory_used);
	emu_output_json_uint("uptime_sec", state->uptime_sec);
	emu_output_json_object_end(1);
}

static int
list_instances(void)
{
	DIR *dir;
	struct dirent *entry;
	struct emu_instance_state state;
	int count = 0;
	int first = 1;

	/* Open instance directory */
	dir = opendir(EMU_INSTANCE_DIR);
	if (dir == NULL) {
		if (errno == ENOENT) {
			/* No instances directory - no instances yet */
			if (g_verbose)
				printf("No instances found (directory %s does not exist)\n",
				    EMU_INSTANCE_DIR);
			return (0);
		}
		warn("Failed to open instance directory: %s", EMU_INSTANCE_DIR);
		return (-1);
	}

	/* Start JSON array if needed */
	if (g_output_format == EMU_OUTPUT_JSON) {
		emu_output_json_begin();
		emu_output_json_array_begin("instances");
	}

	/* Iterate through entries */
	while ((entry = readdir(dir)) != NULL) {
		/* Skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		/* Read instance state */
		if (read_instance_state(entry->d_name, &state) != 0)
			continue;

		/* Apply filters */
		if (!should_include_instance(&state))
			continue;

		count++;

		/* Output instance */
		switch (g_output_format) {
		case EMU_OUTPUT_JSON:
			if (!first)
				printf(",\n");
			output_instance_json(&state);
			first = 0;
			break;

		case EMU_OUTPUT_TEXT:
			if (count == 1)
				emu_output_instance_list_header();
			emu_output_instance(&state);
			break;

		case EMU_OUTPUT_TAP:
		case EMU_OUTPUT_JUNIT:
			/* Not typically used for list output */
			break;
		}
	}

	closedir(dir);

	/* Close JSON array if needed */
	if (g_output_format == EMU_OUTPUT_JSON) {
		emu_output_json_array_end(0);
		emu_output_json_end();
	}

	/* Print summary for text output */
	if (g_output_format == EMU_OUTPUT_TEXT) {
		if (count == 0) {
			printf("No instances found\n");
		} else if (g_verbose) {
			printf("\nTotal: %d instance%s\n", count,
			    count == 1 ? "" : "s");
		}
	}

	return (count);
}

int
emu_cmd_list(int argc, char *argv[])
{
	int ch;
	int option_index;
	static struct option long_options[] = {
		{ "arch", required_argument, NULL, 'a' },
		{ "status", required_argument, NULL, 's' },
		{ "output", required_argument, NULL, 'o' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "a:s:o:vh",
	    long_options, &option_index)) != -1) {
		switch (ch) {
		case 'a':
			g_filter_arch = emu_string_to_arch(optarg);
			if (g_filter_arch == EMU_ARCH_UNKNOWN) {
				warnx("Unknown architecture: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 's':
			if (strcmp(optarg, "initializing") == 0)
				g_filter_state = EMU_STATE_INITIALIZING;
			else if (strcmp(optarg, "stopped") == 0)
				g_filter_state = EMU_STATE_STOPPED;
			else if (strcmp(optarg, "running") == 0)
				g_filter_state = EMU_STATE_RUNNING;
			else if (strcmp(optarg, "paused") == 0)
				g_filter_state = EMU_STATE_PAUSED;
			else if (strcmp(optarg, "error") == 0)
				g_filter_state = EMU_STATE_ERROR;
			else {
				warnx("Unknown state: %s", optarg);
				return (EX_USAGE);
			}
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
			usage_list();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		warnx("Unexpected arguments");
		return (EX_USAGE);
	}

	int result = list_instances();
	if (result < 0)
		return (EX_OSERR);

	return (0);
}
