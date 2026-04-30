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

#include <sys/param.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Main Entry Point
 *
 * This tool provides a unified command-line interface for managing
 * emulated instances across different architectures.
 */

/* Command structure */
struct emu_command {
	const char *name;
	const char *shortdesc;
	int (*func)(int, char *[]);
};

/* Forward declarations for command functions */
static int cmd_init(int argc, char *argv[]);
static int cmd_start(int argc, char *argv[]);
static int cmd_stop(int argc, char *argv[]);
static int cmd_status(int argc, char *argv[]);
static int cmd_list(int argc, char *argv[]);
static int cmd_destroy(int argc, char *argv[]);
static int cmd_console(int argc, char *argv[]);
static int cmd_help(int argc, char *argv[]);
static int cmd_version(int argc, char *argv[]);
static int cmd_load(int argc, char *argv[]);
static int cmd_unload(int argc, char *argv[]);
static int cmd_stack(int argc, char *argv[]);
static int cmd_test(int argc, char *argv[]);
static int cmd_snapshot(int argc, char *argv[]);
static int cmd_restore(int argc, char *argv[]);
static int cmd_blob(int argc, char *argv[]);
static int cmd_image(int argc, char *argv[]);

/* Available commands */
static struct emu_command commands[] = {
	{ "init", "Initialize a new emulated instance", cmd_init },
	{ "start", "Start an emulated instance", cmd_start },
	{ "stop", "Stop an emulated instance", cmd_stop },
	{ "status", "Show status of instances", cmd_status },
	{ "list", "List all emulated instances", cmd_list },
	{ "destroy", "Destroy an emulated instance", cmd_destroy },
	{ "console", "Display instance console output", cmd_console },
	{ "load", "Load kernel module into instance", cmd_load },
	{ "unload", "Unload kernel module from instance", cmd_unload },
	{ "stack", "Examine kernel stack trace", cmd_stack },
	{ "test", "Run tests in emulated instance", cmd_test },
	{ "snapshot", "Create instance snapshot", cmd_snapshot },
	{ "restore", "Restore from snapshot", cmd_restore },
	{ "blob", "Manage firmware blobs", cmd_blob },
	{ "image", "Manage base disk images", cmd_image },
	{ "help", "Show help message", cmd_help },
	{ "version", "Show version information", cmd_version },
	{ NULL, NULL, NULL }
};

/* Global options */
int g_verbose = 0;
int g_quiet = 0;
gid_t g_emu_group = 0; /* 0 means use default GID_EMU */
char *g_emu_group_name = NULL; /* Group name if specified */
enum emu_output_format g_output_format = EMU_OUTPUT_TEXT;

static void
usage(void)
{
	fprintf(stderr, "Usage: emu <command> [options]\n");
	fprintf(stderr, "\nCommands:\n");
	for (int i = 0; commands[i].name != NULL; i++) {
		fprintf(stderr, "  %-12s %s\n", commands[i].name,
		    commands[i].shortdesc);
	}
	fprintf(stderr, "\nGlobal options:\n");
	fprintf(stderr, "  -v, --verbose       Verbose output\n");
	fprintf(stderr, "  -q, --quiet         Quiet output\n");
	fprintf(stderr, "  -o, --output=FMT    Output format (text/json/tap/junit)\n");
	fprintf(stderr, "  -g, --emu-group=GID Group for non-root operation (default: emu group)\n");
	fprintf(stderr, "  -h, --help          Show help message\n");
	fprintf(stderr, "\nRun 'emu help <command>' for more information on a command.\n");
	exit(EX_USAGE);
}

static int
cmd_help(int argc, char *argv[])
{
	if (argc > 1) {
		/* Show help for specific command */
		const char *cmd_name = argv[1];
		for (int i = 0; commands[i].name != NULL; i++) {
			if (strcmp(commands[i].name, cmd_name) == 0) {
				printf("Usage: emu %s [options]\n\n", cmd_name);
				printf("%s\n", commands[i].shortdesc);
				printf("\nDetailed help for '%s' not yet implemented.\n", cmd_name);
				return (0);
			}
		}
		warnx("Unknown command: %s", cmd_name);
		return (1);
	}

	usage();
	return (0);
}

static int
cmd_version(int argc __unused, char *argv[] __unused)
{
	printf("emu FreeBSD Emulation Framework Tool\n");
	printf("Version: 0.1.0\n");
	printf("FreeBSD Emulation Framework for testing kernel modules\n");
	return (0);
}

static int
cmd_init(int argc, char *argv[])
{
	return (emu_cmd_init(argc, argv));
}

static int
cmd_start(int argc, char *argv[])
{
	return (emu_cmd_start(argc, argv));
}

static int
cmd_stop(int argc, char *argv[])
{
	return (emu_cmd_stop(argc, argv));
}

static int
cmd_status(int argc, char *argv[])
{
	return (emu_cmd_status(argc, argv));
}

static int
cmd_list(int argc, char *argv[])
{
	return (emu_cmd_list(argc, argv));
}

static int
cmd_destroy(int argc, char *argv[])
{
	return (emu_cmd_destroy(argc, argv));
}

static int
cmd_console(int argc, char *argv[])
{
	return (emu_cmd_console(argc, argv));
}

static int
cmd_load(int argc, char *argv[])
{
	return (emu_cmd_load(argc, argv));
}

static int
cmd_unload(int argc, char *argv[])
{
	return (emu_cmd_unload(argc, argv));
}

static int
cmd_stack(int argc, char *argv[])
{
	return (emu_cmd_stack(argc, argv));
}

static int
cmd_test(int argc, char *argv[])
{
	return (emu_cmd_test(argc, argv));
}

static int
cmd_snapshot(int argc, char *argv[])
{
	return (emu_cmd_snapshot(argc, argv));
}

static int
cmd_restore(int argc, char *argv[])
{
	return (emu_cmd_restore(argc, argv));
}

static int
cmd_blob(int argc, char *argv[])
{
	return (emu_cmd_blob(argc, argv));
}

static int
cmd_image(int argc, char *argv[])
{
	return (emu_cmd_image(argc, argv));
}

int
main(int argc, char *argv[])
{
	int ch;
	int option_index;
	static struct option long_options[] = {
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "output", required_argument, NULL, 'o' },
		{ "emu-group", required_argument, NULL, 'g' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/*
	 * Perform MAC veriexec binary fingerprint verification.
	 * This checks if the emulator binary has been tampered with
	 * when mac_veriexec is loaded and configured.
	 * Returns 0 on success, -1 on verification failure.
	 */
	if (emu_veriexec_init() != 0) {
		/*
		 * Verification failed - either veriexec is in enforce mode
		 * and our binary is not authorized, or there was an error.
		 * In enforce mode, this should be fatal.
		 */
		if (emu_veriexec_is_enforcing()) {
			errx(EX_SOFTWARE, "MAC veriexec verification failed: "
			    "emulator binary is not authorized or has been "
			    "tampered with");
		}
		/* Non-enforcing mode or other error - warn but continue */
	}

	/* Parse global options */
	while ((ch = getopt_long(argc, argv, "vqo:g:h", long_options,
	    &option_index)) != -1) {
		switch (ch) {
		case 'v':
			g_verbose = 1;
			break;
		case 'q':
			g_quiet = 1;
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
				usage();
			}
			break;
 		case 'g':
			/* Parse group name or GID */
			if (optarg[0] >= '0' && optarg[0] <= '9') {
				/* Numeric GID */
				g_emu_group = (gid_t)strtoul(optarg, NULL, 10);
			} else {
				/* Group name - lookup via getgrnam() */
				struct group *gr = getgrnam(optarg);
				if (gr == NULL) {
					warnx("Unknown group: %s", optarg);
					usage();
				}
				g_emu_group = gr->gr_gid;
				g_emu_group_name = strdup(optarg);
			}
			break;
		case 'h':
			cmd_help(0, NULL);
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage();
	}

	/* Find and execute command */
	const char *cmd_name = argv[0];
	for (int i = 0; commands[i].name != NULL; i++) {
		if (strcmp(commands[i].name, cmd_name) == 0) {
			return (commands[i].func(argc, argv));
		}
	}

	warnx("Unknown command: %s", cmd_name);
	fprintf(stderr, "Run 'emu help' for usage.\n");
	return (EX_USAGE);
}

/* Utility function implementations */

const char *
emu_arch_to_string(enum emu_arch arch)
{
	switch (arch) {
	case EMU_ARCH_AMD64:
		return ("amd64");
	case EMU_ARCH_I386:
		return ("i386");
	case EMU_ARCH_ARM64:
		return ("arm64");
	case EMU_ARCH_ARM:
		return ("arm");
	case EMU_ARCH_POWERPC:
		return ("powerpc");
	case EMU_ARCH_RISCV:
		return ("riscv");
	default:
		return ("unknown");
	}
}

enum emu_arch
emu_string_to_arch(const char *str)
{
	if (strcmp(str, "amd64") == 0)
		return (EMU_ARCH_AMD64);
	if (strcmp(str, "i386") == 0)
		return (EMU_ARCH_I386);
	if (strcmp(str, "arm64") == 0)
		return (EMU_ARCH_ARM64);
	if (strcmp(str, "arm") == 0)
		return (EMU_ARCH_ARM);
	if (strcmp(str, "powerpc") == 0)
		return (EMU_ARCH_POWERPC);
	if (strcmp(str, "riscv") == 0)
		return (EMU_ARCH_RISCV);
	return (EMU_ARCH_UNKNOWN);
}

const char *
emu_mode_to_string(enum emu_mode mode)
{
	switch (mode) {
	case EMU_MODE_AUTO:
		return ("auto");
	case EMU_MODE_BHYVE:
		return ("bhyve");
	case EMU_MODE_EMULATOR:
		return ("emulator");
	default:
		return ("unknown");
	}
}

enum emu_mode
emu_string_to_mode(const char *str)
{
	if (strcmp(str, "auto") == 0)
		return (EMU_MODE_AUTO);
	if (strcmp(str, "bhyve") == 0)
		return (EMU_MODE_BHYVE);
	if (strcmp(str, "emulator") == 0)
		return (EMU_MODE_EMULATOR);
	return (EMU_MODE_AUTO);
}

const char *
emu_state_to_string(enum emu_state state)
{
	switch (state) {
	case EMU_STATE_INITIALIZING:
		return ("initializing");
	case EMU_STATE_STOPPED:
		return ("stopped");
	case EMU_STATE_RUNNING:
		return ("running");
	case EMU_STATE_PAUSED:
		return ("paused");
	case EMU_STATE_ERROR:
		return ("error");
	default:
		return ("unknown");
	}
}

