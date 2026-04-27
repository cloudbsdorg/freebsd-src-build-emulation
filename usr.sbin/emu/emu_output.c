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
#include <sys/time.h>

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Output Formatting
 *
 * This module provides structured output formatting for the emu CLI tool.
 * Supports multiple output formats: text (default), JSON, TAP, and JUnit XML.
 */

/* Global output state */
static int g_output_started = 0;

void
emu_set_output_format(enum emu_output_format format)
{
	g_output_format = format;
	g_output_started = 0;
}

enum emu_output_format
emu_get_output_format(void)
{
	return (g_output_format);
}

/*
 * JSON Output Formatting
 */

void
emu_output_json_begin(void)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	printf("{\n");
	g_output_started = 1;
}

void
emu_output_json_end(void)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	printf("}\n");
	g_output_started = 0;
}

void
emu_output_json_object_begin(const char *key)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": {\n", key);
	else
		printf("  {\n");
}

void
emu_output_json_object_end(int more)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	printf("  }%s\n", more ? "," : "");
}

void
emu_output_json_array_begin(const char *key)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": [\n", key);
	else
		printf("  [\n");
}

void
emu_output_json_array_end(int more)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	printf("  ]%s\n", more ? "," : "");
}

void
emu_output_json_string(const char *key, const char *value)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": \"%s\",\n", key, value);
	else
		printf("  \"%s\",\n", value);
}

void
emu_output_json_int(const char *key, int64_t value)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": %lld,\n", key, (long long)value);
	else
		printf("  %lld,\n", (long long)value);
}

void
emu_output_json_uint(const char *key, uint64_t value)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": %llu,\n", key, (unsigned long long)value);
	else
		printf("  %llu,\n", (unsigned long long)value);
}

void
emu_output_json_bool(const char *key, int value)
{
	if (g_output_format != EMU_OUTPUT_JSON)
		return;

	if (key != NULL)
		printf("  \"%s\": %s,\n", key, value ? "true" : "false");
	else
		printf("  %s,\n", value ? "true" : "false");
}

/*
 * TAP (Test Anything Protocol) Output Formatting
 */

void
emu_output_tap_plan(int ntests)
{
	if (g_output_format != EMU_OUTPUT_TAP)
		return;

	printf("1..%d\n", ntests);
}

void
emu_output_tap_ok(int testnum, const char *description, ...)
{
	va_list ap;

	if (g_output_format != EMU_OUTPUT_TAP)
		return;

	printf("ok %d", testnum);
	if (description != NULL) {
		va_start(ap, description);
		printf(" - ");
		vprintf(description, ap);
		va_end(ap);
	}
	printf("\n");
}

void
emu_output_tap_not_ok(int testnum, const char *description, ...)
{
	va_list ap;

	if (g_output_format != EMU_OUTPUT_TAP)
		return;

	printf("not ok %d", testnum);
	if (description != NULL) {
		va_start(ap, description);
		printf(" - ");
		vprintf(description, ap);
		va_end(ap);
	}
	printf("\n");
}

void
emu_output_tap_skip(int testnum, const char *reason)
{
	if (g_output_format != EMU_OUTPUT_TAP)
		return;

	printf("ok %d # SKIP %s\n", testnum, reason);
}

void
emu_output_tap_diag(const char *message)
{
	if (g_output_format != EMU_OUTPUT_TAP)
		return;

	if (message != NULL)
		printf("# %s\n", message);
}

/*
 * JUnit XML Output Formatting
 */

void
emu_output_junit_begin(const char *suite_name, int tests, int failures,
    int errors, double time)
{
	struct timeval tv;
	struct tm *tm;
	char timestamp[64];

	if (g_output_format != EMU_OUTPUT_JUNIT)
		return;

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm);

	printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	printf("<testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" "
	    "errors=\"%d\" time=\"%.3f\" timestamp=\"%s\">\n",
	    suite_name, tests, failures, errors, time, timestamp);
}

void
emu_output_junit_end(void)
{
	if (g_output_format != EMU_OUTPUT_JUNIT)
		return;

	printf("</testsuite>\n");
}

void
emu_output_junit_testcase(const char *name, const char *classname,
    double time, const char *failure_message, const char *failure_type)
{
	if (g_output_format != EMU_OUTPUT_JUNIT)
		return;

	printf("  <testcase name=\"%s\" classname=\"%s\" time=\"%.3f\"",
	    name, classname, time);

	if (failure_message != NULL) {
		printf(">\n    <failure type=\"%s\" message=\"%s\"/>\n  ",
		    failure_type ? failure_type : "AssertionError",
		    failure_message);
	}

	printf("/>\n");
}

/*
 * Text Table Output Formatting
 */

void
emu_output_table_header(const char **headers, int ncols)
{
	int i;

	if (g_output_format != EMU_OUTPUT_TEXT)
		return;

	for (i = 0; i < ncols; i++) {
		if (i > 0)
			printf("  ");
		printf("%-15s", headers[i]);
	}
	printf("\n");

	for (i = 0; i < ncols; i++) {
		if (i > 0)
			printf("  ");
		printf("---------------");
	}
	printf("\n");
}

void
emu_output_table_row(const char **values, int ncols)
{
	int i;

	if (g_output_format != EMU_OUTPUT_TEXT)
		return;

	for (i = 0; i < ncols; i++) {
		if (i > 0)
			printf("  ");
		printf("%-15s", values[i]);
	}
	printf("\n");
}

void
emu_output_table_separator(void)
{
	if (g_output_format != EMU_OUTPUT_TEXT)
		return;

	printf("\n");
}

/*
 * Generic Output Functions
 */

void
emu_output_instance(const struct emu_instance_state *state)
{
	switch (g_output_format) {
	case EMU_OUTPUT_JSON:
		emu_output_json_object_begin(NULL);
		emu_output_json_string("name", state->name);
		emu_output_json_string("arch", emu_arch_to_string(state->arch));
		emu_output_json_string("mode", emu_mode_to_string(state->mode));
		emu_output_json_string("state", emu_state_to_string(state->state));
		emu_output_json_int("pid", state->pid);
		emu_output_json_uint("memory_used", state->memory_used);
		emu_output_json_uint("uptime_sec", state->uptime_sec);
		emu_output_json_object_end(0);
		break;

	case EMU_OUTPUT_TEXT:
		printf("%-15s %-10s %-10s %-12s %8d %10zu %10lu\n",
		    state->name,
		    emu_arch_to_string(state->arch),
		    emu_mode_to_string(state->mode),
		    emu_state_to_string(state->state),
		    state->pid,
		    state->memory_used,
		    (unsigned long)state->uptime_sec);
		break;

	case EMU_OUTPUT_TAP:
	case EMU_OUTPUT_JUNIT:
		/* Not typically used for instance output */
		break;
	}
}

void
emu_output_instance_list_header(void)
{
	if (g_output_format != EMU_OUTPUT_TEXT)
		return;

	const char *headers[] = {
		"NAME", "ARCH", "MODE", "STATE", "PID", "MEMORY", "UPTIME"
	};
	emu_output_table_header(headers, 7);
}

void
emu_output_error_v(const char *fmt, va_list ap)
{
	vwarnx(fmt, ap);
}

void
emu_output_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	emu_output_error_v(fmt, ap);
	va_end(ap);
}

void
emu_output_info(const char *fmt, ...)
{
	va_list ap;

	if (g_quiet)
		return;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

void
emu_output_verbose(const char *fmt, ...)
{
	va_list ap;

	if (!g_verbose)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
