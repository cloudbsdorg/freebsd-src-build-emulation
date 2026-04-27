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
#include <sys/time.h>
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
 * emu stack - Examine kernel stack traces from an emulated instance
 *
 * Usage: emu stack [-v] [-n <num_frames>] <instance>
 */
int
emu_cmd_stack(int argc, char *argv[])
{
	const char *instance_name = NULL;
	char sysctl_name[PATH_MAX];
	struct emu_stack_output stack_output;
	int ch;
	int error;
	int num_frames = 0;

	while ((ch = getopt(argc, argv, "vn:")) != -1) {
		switch (ch) {
		case 'v':
			g_verbose = 1;
			break;
		case 'n':
			num_frames = atoi(optarg);
			if (num_frames <= 0 || num_frames > 100) {
				fprintf(stderr, "Invalid number of frames (must be 1-100)\n");
				return (EINVAL);
			}
			break;
		default:
			return (EINVAL);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, "Usage: emu stack [-v] [-n <num_frames>] <instance>\n");
		return (EINVAL);
	}

	instance_name = argv[0];

	if (strlen(instance_name) >= EMU_NAME_MAX) {
		fprintf(stderr, "Instance name too long (max %d)\n", EMU_NAME_MAX - 1);
		return (EINVAL);
	}

	if (g_verbose)
		printf("Examining stack for instance '%s'\n", instance_name);

	/*
	 * Read stack trace from sysctl interface.
	 * The kernel returns a structured stack trace.
	 */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.instance.%s.stack", instance_name);

	/* First, get the size of the stack data */
	size_t len = sizeof(stack_output);
	error = sysctlbyname(sysctl_name, &stack_output, &len, NULL, 0);
	if (error != 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "Instance '%s' not found or not running\n", instance_name);
		} else {
			fprintf(stderr, "Failed to read stack trace: %s\n", strerror(errno));
		}
		return (errno);
	}

	/* Output the stack trace based on the selected format */
	switch (g_output_format) {
	case EMU_OUTPUT_JSON:
		emu_output_json_begin();
		emu_output_json_object_begin("stack");
		emu_output_json_string("instance", instance_name);
		emu_output_json_string("arch", stack_output.arch);
		emu_output_json_int("num_frames", stack_output.num_frames);
		emu_output_json_int("timestamp", (int64_t)stack_output.timestamp);
		emu_output_json_array_begin("frames");
		for (int i = 0; i < stack_output.num_frames; i++) {
			emu_output_json_object_begin(NULL);
			emu_output_json_uint("pc", stack_output.frames[i].pc);
			emu_output_json_uint("sp", stack_output.frames[i].sp);
			emu_output_json_uint("fp", stack_output.frames[i].fp);
			emu_output_json_string("symbol", stack_output.frames[i].symbol);
			emu_output_json_string("module", stack_output.frames[i].module);
			emu_output_json_object_end(i < stack_output.num_frames - 1);
		}
		emu_output_json_array_end(0);
		emu_output_json_object_end(0);
		emu_output_json_end();
		break;

	case EMU_OUTPUT_TEXT:
	default:
		printf("Stack trace for instance '%s' (%s):\n", instance_name, stack_output.arch);
		printf("Timestamp: %ld\n", (long)stack_output.timestamp);
		printf("Number of frames: %d\n\n", stack_output.num_frames);
		printf("%-4s %-20s %-20s %-20s %-20s %s\n",
		    "Frame", "PC", "SP", "FP", "Module", "Symbol");
		printf("%-4s %-20s %-20s %-20s %-20s %s\n",
		    "----", "-------------------", "-------------------",
		    "-------------------", "--------------------", "-------------------");
		for (int i = 0; i < stack_output.num_frames; i++) {
			printf("%-4d 0x%-18lx 0x%-18lx 0x%-18lx %-20s %s\n",
			    i,
			    (u_long)stack_output.frames[i].pc,
			    (u_long)stack_output.frames[i].sp,
			    (u_long)stack_output.frames[i].fp,
			    stack_output.frames[i].module[0] != '\0' ?
			        stack_output.frames[i].module : "<unknown>",
			    stack_output.frames[i].symbol[0] != '\0' ?
			        stack_output.frames[i].symbol : "<unknown>");
		}
		break;
	}

	return (0);
}
