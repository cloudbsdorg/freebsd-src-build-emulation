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
#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Configuration Management
 *
 * This module provides configuration file parsing and management.
 * Supports system-wide (/usr/local/etc/emu/emu.conf) and per-user
 * (~/.config/emu/emu.conf) configuration files following XDG Base Directory spec.
 */

#define EMU_SYSCONF_DIR		"/usr/local/etc/emu"
#define EMU_SYSCONF_FILE	"/usr/local/etc/emu/emu.conf"
#define EMU_USERCONF_DIR	".config/emu"
#define EMU_USERCONF_FILE	".config/emu/emu.conf"

static struct emu_config g_config;

/* Default configuration values */
static const struct emu_config g_defaults = {
	.arch = EMU_ARCH_AMD64,
	.mode = EMU_MODE_AUTO,
	.cpu_speed_mhz = 0,		/* Auto-detect */
	.memory_size = 256 * 1024 * 1024,	/* 256 MB */
	.num_cpus = 1,
	.output_format = EMU_OUTPUT_TEXT,
	.verbose = 0,
	.instance_dir = "/var/emu",
	.image_cache_dir = "/var/cache/emu/images",
};

static char *
get_user_config_path(void)
{
	static char path[MAXPATHLEN];
	const char *home;
	struct passwd *pw;

	/* Try HOME environment variable first */
	home = getenv("HOME");
	if (home == NULL) {
		/* Fall back to passwd database */
		pw = getpwuid(getuid());
		if (pw == NULL)
			return (NULL);
		home = pw->pw_dir;
	}

	snprintf(path, sizeof(path), "%s/%s", home, EMU_USERCONF_FILE);
	return (path);
}

static char *
get_user_config_dir(void)
{
	static char dir[MAXPATHLEN];
	char *path = get_user_config_path();
	if (path == NULL)
		return (NULL);

	/* Return directory part */
	strlcpy(dir, path, sizeof(dir));
	char *last_slash = strrchr(dir, '/');
	if (last_slash != NULL)
		*last_slash = '\0';

	return (dir);
}

static int
ensure_user_config_dir(void)
{
	char *dir = get_user_config_dir();
	struct stat sb;

	if (dir == NULL) {
		warnx("Failed to determine user config directory");
		return (-1);
	}

	/* Check if directory exists */
	if (stat(dir, &sb) == 0) {
		if (!S_ISDIR(sb.st_mode)) {
			warnx("%s exists but is not a directory", dir);
			return (-1);
		}
		return (0);
	}

	/* Create directory */
	if (mkdir(dir, 0700) != 0) {
		warn("Failed to create user config directory: %s", dir);
		return (-1);
	}

	return (0);
}

static int
parse_config_value(const char *key, const char *value)
{
	/* String values */
	if (strcmp(key, "instance_dir") == 0) {
		strlcpy(g_config.instance_dir, value, sizeof(g_config.instance_dir));
		return (0);
	}
	if (strcmp(key, "image_cache_dir") == 0) {
		strlcpy(g_config.image_cache_dir, value, sizeof(g_config.image_cache_dir));
		return (0);
	}

	/* Architecture */
	if (strcmp(key, "default_arch") == 0) {
		enum emu_arch arch = emu_string_to_arch(value);
		if (arch == EMU_ARCH_UNKNOWN) {
			warnx("Unknown architecture: %s", value);
			return (-1);
		}
		g_config.arch = arch;
		return (0);
	}

	/* Mode */
	if (strcmp(key, "default_mode") == 0) {
		enum emu_mode mode = emu_string_to_mode(value);
		if (mode == EMU_MODE_AUTO && strcmp(value, "auto") != 0) {
			warnx("Unknown mode: %s", value);
			return (-1);
		}
		g_config.mode = mode;
		return (0);
	}

	/* CPU level */
	if (strcmp(key, "default_cpu_level") == 0) {
		strlcpy(g_config.cpu_level, value, sizeof(g_config.cpu_level));
		return (0);
	}

	/* CPU speed */
	if (strcmp(key, "default_cpu_speed_mhz") == 0) {
		g_config.cpu_speed_mhz = atoi(value);
		if (g_config.cpu_speed_mhz < 0) {
			warnx("Invalid CPU speed: %s", value);
			return (-1);
		}
		return (0);
	}

	/* Memory */
	if (strcmp(key, "default_memory") == 0) {
		/* Parse with optional unit suffix */
		char *endptr;
		unsigned long val = strtoul(value, &endptr, 0);
		if (*endptr == 'M' || *endptr == 'm')
			g_config.memory_size = val * 1024 * 1024;
		else if (*endptr == 'G' || *endptr == 'g')
			g_config.memory_size = val * 1024 * 1024 * 1024;
		else
			g_config.memory_size = val;
		return (0);
	}

	/* CPUs */
	if (strcmp(key, "default_cpus") == 0) {
		g_config.num_cpus = atoi(value);
		if (g_config.num_cpus < 1) {
			warnx("Invalid CPU count: %s", value);
			return (-1);
		}
		return (0);
	}

	/* Output format */
	if (strcmp(key, "output_format") == 0) {
		if (strcmp(value, "text") == 0)
			g_config.output_format = EMU_OUTPUT_TEXT;
		else if (strcmp(value, "json") == 0)
			g_config.output_format = EMU_OUTPUT_JSON;
		else if (strcmp(value, "tap") == 0)
			g_config.output_format = EMU_OUTPUT_TAP;
		else if (strcmp(value, "junit") == 0)
			g_config.output_format = EMU_OUTPUT_JUNIT;
		else {
			warnx("Unknown output format: %s", value);
			return (-1);
		}
		return (0);
	}

	/* Verbose */
	if (strcmp(key, "verbose") == 0) {
		g_config.verbose = (strcmp(value, "yes") == 0 ||
		    strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
		return (0);
	}

	/* Unknown key - ignore for forward compatibility */
	if (g_config.verbose)
		warnx("Ignoring unknown configuration key: %s", key);

	return (0);
}

static int
parse_config_file(const char *path)
{
	FILE *fp;
	char line[1024];
	int lineno = 0;

	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			/* File doesn't exist - not an error */
			if (g_config.verbose)
				warnx("Configuration file not found: %s", path);
			return (0);
		}
		warn("Failed to open configuration file: %s", path);
		return (-1);
	}

	if (g_config.verbose)
		printf("Reading configuration from: %s\n", path);

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p, *key, *value;
		lineno++;

		/* Skip comments and empty lines */
		p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == '\0')
			continue;

		/* Find key=value separator */
		key = p;
		p = strchr(p, '=');
		if (p == NULL) {
			warnx("%s:%d: Missing '=' in configuration line", path, lineno);
			continue;
		}

		/* Extract key */
		*p = '\0';
		value = p + 1;

		/* Trim whitespace from key */
		char *end = key + strlen(key) - 1;
		while (end > key && (*end == ' ' || *end == '\t'))
			*end-- = '\0';

		/* Trim whitespace and quotes from value */
		while (*value == ' ' || *value == '\t')
			value++;
		end = value + strlen(value) - 1;
		while (end > value && (*end == ' ' || *end == '\t' || *end == '"' || *end == '\''))
			*end-- = '\0';
		if (*value == '"' || *value == '\'')
			value++;

		/* Parse the key=value pair */
		if (parse_config_value(key, value) != 0) {
			warnx("%s:%d: Failed to parse configuration", path, lineno);
			fclose(fp);
			return (-1);
		}
	}

	fclose(fp);
	return (0);
}

static void
init_defaults(void)
{
	memcpy(&g_config, &g_defaults, sizeof(g_config));
}

int
emu_config_load(void)
{
	/* Start with defaults */
	init_defaults();

	/* Load system-wide configuration first */
	if (parse_config_file(EMU_SYSCONF_FILE) != 0) {
		warnx("Failed to load system configuration");
		/* Continue anyway - user config might override */
	}

	/* Load user configuration (overrides system) */
	char *user_path = get_user_config_path();
	if (user_path != NULL) {
		if (parse_config_file(user_path) != 0) {
			warnx("Failed to load user configuration");
			/* Continue with system config and defaults */
		}
	}

	return (0);
}

int
emu_config_save(void)
{
	char *path = get_user_config_path();
	FILE *fp;
	mode_t old_umask;

	if (path == NULL) {
		warnx("Failed to determine user config path");
		return (-1);
	}

	/* Ensure directory exists */
	if (ensure_user_config_dir() != 0)
		return (-1);

	/* Create file with restricted permissions */
	old_umask = umask(077);
	fp = fopen(path, "w");
	umask(old_umask);

	if (fp == NULL) {
		warn("Failed to create configuration file: %s", path);
		return (-1);
	}

	/* Write configuration */
	fprintf(fp, "# Emulation Framework Configuration\n");
	fprintf(fp, "# Generated by emu config save\n");
	fprintf(fp, "\n");
	fprintf(fp, "# Default architecture (amd64, i386, arm64, arm, powerpc, riscv)\n");
	fprintf(fp, "default_arch = %s\n", emu_arch_to_string(g_config.arch));
	fprintf(fp, "\n");
	fprintf(fp, "# Default execution mode (auto, bhyve, emulator)\n");
	fprintf(fp, "default_mode = %s\n", emu_mode_to_string(g_config.mode));
	fprintf(fp, "\n");
	fprintf(fp, "# Default CPU level (host, max, or specific model)\n");
	fprintf(fp, "default_cpu_level = %s\n", g_config.cpu_level);
	fprintf(fp, "\n");
	fprintf(fp, "# Default CPU speed in MHz (0 = auto-detect)\n");
	fprintf(fp, "default_cpu_speed_mhz = %d\n", g_config.cpu_speed_mhz);
	fprintf(fp, "\n");
	fprintf(fp, "# Default memory size (e.g., 256M, 1G)\n");
	fprintf(fp, "default_memory = %zuM\n", g_config.memory_size / (1024 * 1024));
	fprintf(fp, "\n");
	fprintf(fp, "# Default number of CPUs\n");
	fprintf(fp, "default_cpus = %d\n", g_config.num_cpus);
	fprintf(fp, "\n");
	fprintf(fp, "# Instance directory\n");
	fprintf(fp, "instance_dir = %s\n", g_config.instance_dir);
	fprintf(fp, "\n");
	fprintf(fp, "# Image cache directory\n");
	fprintf(fp, "image_cache_dir = %s\n", g_config.image_cache_dir);
	fprintf(fp, "\n");
	fprintf(fp, "# Output format (text, json, tap, junit)\n");
	fprintf(fp, "output_format = %s\n",
	    g_config.output_format == EMU_OUTPUT_TEXT ? "text" :
	    g_config.output_format == EMU_OUTPUT_JSON ? "json" :
	    g_config.output_format == EMU_OUTPUT_TAP ? "tap" : "junit");
	fprintf(fp, "\n");
	fprintf(fp, "# Verbose output (yes/no)\n");
	fprintf(fp, "verbose = %s\n", g_config.verbose ? "yes" : "no");

	fclose(fp);

	printf("Configuration saved to: %s\n", path);
	return (0);
}

const struct emu_config *
emu_config_get(void)
{
	return (&g_config);
}

void
emu_config_set_verbose(int verbose)
{
	g_config.verbose = verbose;
}

int
emu_config_get_verbose(void)
{
	return (g_config.verbose);
}

/*
 * Configuration command - view/edit configuration
 */

static void
usage_config(void)
{
	fprintf(stderr, "Usage: emu config [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  --show                Show current configuration\n");
	fprintf(stderr, "  --save                Save current configuration\n");
	fprintf(stderr, "  --reset               Reset to defaults\n");
	fprintf(stderr, "  --arch=ARCH           Set default architecture\n");
	fprintf(stderr, "  --mode=MODE           Set default mode\n");
	fprintf(stderr, "  --memory=SIZE         Set default memory size\n");
	fprintf(stderr, "  --cpus=N              Set default CPU count\n");
	fprintf(stderr, "  --output-format=FMT   Set default output format\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	exit(EX_USAGE);
}

static int
cmd_config_show(void)
{
	printf("Current configuration:\n");
	printf("  Architecture:      %s\n", emu_arch_to_string(g_config.arch));
	printf("  Mode:              %s\n", emu_mode_to_string(g_config.mode));
	printf("  CPU level:         %s\n", g_config.cpu_level);
	printf("  CPU speed:         %d MHz%s\n", g_config.cpu_speed_mhz,
	    g_config.cpu_speed_mhz == 0 ? " (auto)" : "");
	printf("  Memory:            %zu MB\n", g_config.memory_size / (1024 * 1024));
	printf("  CPUs:              %d\n", g_config.num_cpus);
	printf("  Instance dir:      %s\n", g_config.instance_dir);
	printf("  Image cache:       %s\n", g_config.image_cache_dir);
	printf("  Output format:     %s\n",
	    g_config.output_format == EMU_OUTPUT_TEXT ? "text" :
	    g_config.output_format == EMU_OUTPUT_JSON ? "json" :
	    g_config.output_format == EMU_OUTPUT_TAP ? "tap" : "junit");
	printf("  Verbose:           %s\n", g_config.verbose ? "yes" : "no");
	printf("\nConfiguration files:\n");
	printf("  System:            %s\n", EMU_SYSCONF_FILE);
	char *user_path = get_user_config_path();
	if (user_path != NULL)
		printf("  User:              %s\n", user_path);
	else
		printf("  User:              (not available)\n");

	return (0);
}

static int
cmd_config_reset(void)
{
	init_defaults();
	printf("Configuration reset to defaults\n");
	return (0);
}

int
cmd_config(int argc, char *argv[])
{
	int show = 0;
	int save = 0;
	int reset = 0;
	static struct option long_options[] = {
		{ "show", no_argument, NULL, 1000 },
		{ "save", no_argument, NULL, 1001 },
		{ "reset", no_argument, NULL, 1002 },
		{ "arch", required_argument, NULL, 1003 },
		{ "mode", required_argument, NULL, 1004 },
		{ "memory", required_argument, NULL, 1005 },
		{ "cpus", required_argument, NULL, 1006 },
		{ "output-format", required_argument, NULL, 1007 },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* Load existing configuration */
	emu_config_load();

	int ch;
	int option_index;
	while ((ch = getopt_long(argc, argv, "vh",
	    long_options, &option_index)) != -1) {
		switch (ch) {
		case 1000: /* --show */
			show = 1;
			break;

		case 1001: /* --save */
			save = 1;
			break;

		case 1002: /* --reset */
			reset = 1;
			break;

		case 1003: /* --arch */
			g_config.arch = emu_string_to_arch(optarg);
			if (g_config.arch == EMU_ARCH_UNKNOWN) {
				warnx("Unknown architecture: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 1004: /* --mode */
			g_config.mode = emu_string_to_mode(optarg);
			if (g_config.mode == EMU_MODE_AUTO && strcmp(optarg, "auto") != 0) {
				warnx("Unknown mode: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 1005: /* --memory */
			g_config.memory_size = strtoul(optarg, NULL, 0) * 1024 * 1024;
			break;

		case 1006: /* --cpus */
			g_config.num_cpus = atoi(optarg);
			if (g_config.num_cpus < 1) {
				warnx("Invalid CPU count: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 1007: /* --output-format */
			if (strcmp(optarg, "text") == 0)
				g_config.output_format = EMU_OUTPUT_TEXT;
			else if (strcmp(optarg, "json") == 0)
				g_config.output_format = EMU_OUTPUT_JSON;
			else if (strcmp(optarg, "tap") == 0)
				g_config.output_format = EMU_OUTPUT_TAP;
			else if (strcmp(optarg, "junit") == 0)
				g_config.output_format = EMU_OUTPUT_JUNIT;
			else {
				warnx("Unknown output format: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 'v':
			g_config.verbose = 1;
			break;

		case 'h':
		default:
			usage_config();
		}
	}

	/* If no action specified, show configuration */
	if (!show && !save && !reset && optind >= argc)
		show = 1;

	if (reset)
		cmd_config_reset();

	if (show)
		return (cmd_config_show());

	if (save)
		return (emu_config_save());

	return (0);
}
