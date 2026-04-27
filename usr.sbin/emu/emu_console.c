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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Console Command
 *
 * This command displays serial console output from an emulated instance.
 */

#define EMU_INSTANCE_DIR	"/var/emu"
#define EMU_CONSOLE_LINES_DEFAULT	25

static char g_instance_name[EMU_NAME_MAX] = "";
static int g_lines = EMU_CONSOLE_LINES_DEFAULT;
static int g_tail = 0;
static int g_follow = 0;
static int g_dump = 0;

static void
usage_console(void)
{
	fprintf(stderr, "Usage: emu console [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n, --name=NAME       Instance name (required)\n");
	fprintf(stderr, "  -l, --lines=N         Number of lines to show (default: 25)\n");
	fprintf(stderr, "  -t, --tail            Show last N lines (same as --lines)\n");
	fprintf(stderr, "  -f, --follow          Follow console output (like tail -f)\n");
	fprintf(stderr, "  -d, --dump            Dump full console buffer\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu console --name test-instance\n");
	fprintf(stderr, "  emu console -n test-instance --lines 50\n");
	fprintf(stderr, "  emu console -n test-instance --follow\n");
	exit(EX_USAGE);
}

static int
read_console_file(const char *path, char **buf, size_t *size)
{
	struct stat sb;
	FILE *fp;
	size_t nread;

	/* Get file size */
	if (stat(path, &sb) != 0) {
		if (errno == ENOENT) {
			/* No console output yet */
			*buf = NULL;
			*size = 0;
			return (0);
		}
		warn("Failed to stat console file: %s", path);
		return (-1);
	}

	/* Allocate buffer */
	*buf = malloc(sb.st_size + 1);
	if (*buf == NULL) {
		warnx("Failed to allocate buffer");
		return (-1);
	}

	/* Read file */
	fp = fopen(path, "r");
	if (fp == NULL) {
		warn("Failed to open console file: %s", path);
		free(*buf);
		*buf = NULL;
		return (-1);
	}

	nread = fread(*buf, 1, sb.st_size, fp);
	if (ferror(fp)) {
		warn("Failed to read console file: %s", path);
		fclose(fp);
		free(*buf);
		*buf = NULL;
		return (-1);
	}

	(*buf)[nread] = '\0';
	*size = nread;
	fclose(fp);

	return (0);
}

static char *
find_line_start(char *buf, int lines_from_end)
{
	char *p;
	int count = 0;

	if (lines_from_end <= 0)
		return (buf);

	/* Start from end and count backwards */
	p = buf + strlen(buf);
	while (p > buf) {
		p--;
		if (*p == '\n') {
			count++;
			if (count >= lines_from_end)
				return (p + 1);
		}
	}

	return (buf);
}

static int
show_console(const char *name)
{
	char path[MAXPATHLEN];
	char *buf = NULL;
	size_t size = 0;
	char *start;
	int error;

	/* Construct console file path */
	snprintf(path, sizeof(path), "%s/%s/console/output.log",
	    EMU_INSTANCE_DIR, name);

	/* Read console output */
	error = read_console_file(path, &buf, &size);
	if (error != 0)
		return (error);

	/* Check if there's any output */
	if (buf == NULL || size == 0) {
		if (g_verbose)
			printf("No console output for instance '%s'\n", name);
		return (0);
	}

	/* Handle --dump (full output) */
	if (g_dump) {
		fwrite(buf, 1, size, stdout);
		free(buf);
		return (0);
	}

	/* Handle --lines / --tail */
	if (g_lines > 0 && g_lines < (int)(size / 80) + 1) {
		start = find_line_start(buf, g_lines);
	} else {
		start = buf;
	}

	/* Output console content */
	fwrite(start, 1, strlen(start), stdout);

	/* Handle --follow */
	if (g_follow) {
		if (g_verbose)
			printf("[Following console output, Ctrl-C to stop]\n");

		/* Simple follow implementation - poll file */
		size_t last_size = size;
		while (1) {
			sleep(1);

			/* Re-read file */
			free(buf);
			error = read_console_file(path, &buf, &size);
			if (error != 0)
				break;

			if (buf == NULL || size == 0)
				continue;

			/* Check if file grew */
			if (size > last_size) {
				fwrite(buf + last_size, 1, size - last_size, stdout);
				fflush(stdout);
				last_size = size;
			}
		}
	}

	free(buf);
	return (0);
}

int
emu_cmd_console(int argc, char *argv[])
{
	int ch;
	int option_index;
	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'n' },
		{ "lines", required_argument, NULL, 'l' },
		{ "tail", no_argument, NULL, 't' },
		{ "follow", no_argument, NULL, 'f' },
		{ "dump", no_argument, NULL, 'd' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "n:l:tfdvh",
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

		case 'l':
			g_lines = atoi(optarg);
			if (g_lines < 0) {
				warnx("Invalid line count: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 't':
			g_tail = 1;
			break;

		case 'f':
			g_follow = 1;
			break;

		case 'd':
			g_dump = 1;
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_console();
		}
	}

	argc -= optind;
	argv += optind;

	/* Validate required parameters */
	if (g_instance_name[0] == '\0') {
		warnx("Instance name is required (--name)");
		return (EX_USAGE);
	}

	/* Check if instance exists */
	char path[MAXPATHLEN];
	struct stat sb;
	snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, g_instance_name);
	if (stat(path, &sb) != 0) {
		if (errno == ENOENT) {
			warnx("Instance '%s' does not exist", g_instance_name);
			return (EX_NOINPUT);
		}
		warn("Failed to stat instance directory: %s", path);
		return (EX_OSERR);
	}

	/* Show console output */
	int result = show_console(g_instance_name);
	if (result != 0)
		return (EX_OSERR);

	return (0);
}
