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
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Init Command
 *
 * This command initializes a new emulated instance with the specified
 * configuration.
 */

#define EMU_DEFAULT_MEMORY	(256 * 1024 * 1024)	/* 256 MB */
#define EMU_DEFAULT_CPUS	1
#define EMU_INSTANCE_DIR	"/var/emu"

static struct emu_instance_config g_config;

static void
init_default_config(void)
{
	memset(&g_config, 0, sizeof(g_config));
	g_config.arch = EMU_ARCH_AMD64;		/* Default to host arch */
	g_config.mode = EMU_MODE_AUTO;
	g_config.cpu_speed_mhz = 0;		/* Auto-detect */
	g_config.memory_size = EMU_DEFAULT_MEMORY;
	g_config.num_cpus = EMU_DEFAULT_CPUS;
	strlcpy(g_config.cpu_level, "host", sizeof(g_config.cpu_level));
}

static void
usage_init(void)
{
	fprintf(stderr, "Usage: emu init [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -a, --arch=ARCH       Target architecture (amd64, i386, arm64, arm, powerpc, riscv)\n");
	fprintf(stderr, "  -m, --mode=MODE       Execution mode (auto, bhyve, emulator)\n");
	fprintf(stderr, "  -n, --name=NAME       Instance name (required)\n");
	fprintf(stderr, "  -c, --cpu-level=LVL   CPU level (host, max, or specific model)\n");
	fprintf(stderr, "  -s, --cpu-speed=MHZ   CPU speed in MHz (0 = auto-detect)\n");
	fprintf(stderr, "  -M, --memory=SIZE     Memory size in MB (default: 256)\n");
	fprintf(stderr, "  -C, --cpus=N          Number of CPUs (default: 1)\n");
	fprintf(stderr, "  -i, --image=PATH      Path to disk image\n");
	fprintf(stderr, "  -k, --kernel=PATH     Path to kernel image\n");
	fprintf(stderr, "  -b, --blob=PATH       Path to firmware blob (BIOS/UEFI)\n");
	fprintf(stderr, "  -f, --force           Force creation even if instance exists\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu init -n test-instance --arch amd64 --memory 512\n");
	fprintf(stderr, "  emu init -n arm-test --arch arm64 --mode emulator\n");
	exit(EX_USAGE);
}

static int
create_instance_directory(const char *name)
{
	char path[MAXPATHLEN];
	struct stat sb;
	int error;

	/* Check if instance directory already exists */
	snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, name);
	if (stat(path, &sb) == 0) {
		warnx("Instance '%s' already exists", name);
		return (-1);
	}

	/* Create instance directory */
	if (mkdir(path, 0755) != 0) {
		warn("Failed to create instance directory: %s", path);
		return (-1);
	}

	if (g_verbose)
		printf("Created instance directory: %s\n", path);

	/* Create subdirectories */
	const char *subdirs[] = { "config", "console", "snapshots", NULL };
	for (int i = 0; subdirs[i] != NULL; i++) {
		snprintf(path, sizeof(path), "%s/%s/%s", EMU_INSTANCE_DIR, name, subdirs[i]);
		if (mkdir(path, 0755) != 0) {
			warn("Failed to create subdirectory: %s", path);
			/* Try to clean up */
			snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, name);
			rmdir(path);
			return (-1);
		}
		if (g_verbose)
			printf("Created subdirectory: %s\n", path);
	}

	/* Create configuration file */
	snprintf(path, sizeof(path), "%s/%s/config/config.json", EMU_INSTANCE_DIR, name);
	FILE *fp = fopen(path, "w");
	if (fp == NULL) {
		warn("Failed to create configuration file: %s", path);
		/* Try to clean up */
		snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR, name);
		rmdir(path);
		return (-1);
	}

	/* Write configuration as JSON */
	fprintf(fp, "{\n");
	fprintf(fp, "  \"name\": \"%s\",\n", g_config.name);
	fprintf(fp, "  \"arch\": \"%s\",\n", emu_arch_to_string(g_config.arch));
	fprintf(fp, "  \"mode\": \"%s\",\n", emu_mode_to_string(g_config.mode));
	fprintf(fp, "  \"cpu_level\": \"%s\",\n", g_config.cpu_level);
	fprintf(fp, "  \"cpu_speed_mhz\": %d,\n", g_config.cpu_speed_mhz);
	fprintf(fp, "  \"memory_size\": %zu,\n", g_config.memory_size);
	fprintf(fp, "  \"num_cpus\": %d,\n", g_config.num_cpus);
	fprintf(fp, "  \"image_path\": \"%s\",\n", g_config.image_path);
	fprintf(fp, "  \"kernel_path\": \"%s\",\n", g_config.kernel_path);
	fprintf(fp, "  \"blob_path\": \"%s\"\n", g_config.blob_path);
	fprintf(fp, "}\n");
	fclose(fp);

	if (g_verbose)
		printf("Created configuration file: %s\n", path);

	/* Create state file */
	snprintf(path, sizeof(path), "%s/%s/config/state", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "w");
	if (fp == NULL) {
		warn("Failed to create state file: %s", path);
		return (-1);
	}
	fprintf(fp, "STOPPED\n");
	fclose(fp);

	return (0);
}

static int
validate_paths(void)
{
	struct stat sb;

	/* Validate image path if specified */
	if (g_config.image_path[0] != '\0') {
		if (stat(g_config.image_path, &sb) != 0) {
			warnx("Image file not found: %s", g_config.image_path);
			return (-1);
		}
		if (!S_ISREG(sb.st_mode)) {
			warnx("Image path is not a regular file: %s", g_config.image_path);
			return (-1);
		}
		if (g_verbose)
			printf("Image file validated: %s (%ju bytes)\n",
			    g_config.image_path, (uintmax_t)sb.st_size);
	}

	/* Validate kernel path if specified */
	if (g_config.kernel_path[0] != '\0') {
		if (stat(g_config.kernel_path, &sb) != 0) {
			warnx("Kernel file not found: %s", g_config.kernel_path);
			return (-1);
		}
		if (!S_ISREG(sb.st_mode)) {
			warnx("Kernel path is not a regular file: %s", g_config.kernel_path);
			return (-1);
		}
		if (g_verbose)
			printf("Kernel file validated: %s\n", g_config.kernel_path);
	}

	/* Validate blob path if specified */
	if (g_config.blob_path[0] != '\0') {
		if (stat(g_config.blob_path, &sb) != 0) {
			warnx("Blob file not found: %s", g_config.blob_path);
			return (-1);
		}
		if (!S_ISREG(sb.st_mode)) {
			warnx("Blob path is not a regular file: %s", g_config.blob_path);
			return (-1);
		}
		if (g_verbose)
			printf("Blob file validated: %s\n", g_config.blob_path);
	}

	return (0);
}

static int
check_dependencies(void)
{
	struct stat sb;
	int has_vmm = 0;

	/* Check if VMM is available (for bhyve mode) */
	if (stat("/dev/vmm", &sb) == 0 && S_ISCHR(sb.st_mode)) {
		has_vmm = 1;
		if (g_verbose)
			printf("VMM device available: /dev/vmm\n");
	}

	/* Check if vmm.ko is loaded */
	int mib[4];
	size_t len;
	char buf[256];

	mib[0] = CTL_KERN;
	mib[1] = KERN_MODULES;
	mib[2] = CTL_KERN_MODSTAT;
	mib[3] = 0;	/* We'll search for vmm */

	/* Simple check: try to read /boot/kernel/vmm.ko */
	if (stat("/boot/kernel/vmm.ko", &sb) == 0) {
		if (g_verbose)
			printf("VMM kernel module found: /boot/kernel/vmm.ko\n");
	}

	/* Warn if bhyve mode requested but VMM not available */
	if (g_config.mode == EMU_MODE_BHYVE && !has_vmm) {
		warnx("Bhyve mode requested but VMM not available");
		warnx("Load vmm.ko or switch to emulator mode");
		return (-1);
	}

	return (0);
}

static int
validate_architecture(void)
{
	/* Validate architecture is supported */
	switch (g_config.arch) {
	case EMU_ARCH_AMD64:
	case EMU_ARCH_I386:
	case EMU_ARCH_ARM64:
	case EMU_ARCH_ARM:
	case EMU_ARCH_POWERPC:
	case EMU_ARCH_RISCV:
		break;
	default:
		warnx("Unsupported architecture: %d", g_config.arch);
		return (-1);
	}

	if (g_verbose)
		printf("Architecture validated: %s\n",
		    emu_arch_to_string(g_config.arch));

	return (0);
}

static int
validate_memory_size(void)
{
	/* Minimum 64 MB, maximum 1 TB */
	if (g_config.memory_size < (64 * 1024 * 1024)) {
		warnx("Memory size too small (minimum 64 MB)");
		return (-1);
	}
	if (g_config.memory_size > (1024ULL * 1024 * 1024 * 1024)) {
		warnx("Memory size too large (maximum 1 TB)");
		return (-1);
	}

	if (g_verbose)
		printf("Memory size validated: %zu MB\n",
		    g_config.memory_size / (1024 * 1024));

	return (0);
}

static int
validate_cpu_config(void)
{
	/* Validate CPU count */
	if (g_config.num_cpus < 1 || g_config.num_cpus > 256) {
		warnx("Invalid CPU count: %d (must be 1-256)", g_config.num_cpus);
		return (-1);
	}

	/* Validate CPU speed if specified */
	if (g_config.cpu_speed_mhz < 0) {
		warnx("Invalid CPU speed: %d", g_config.cpu_speed_mhz);
		return (-1);
	}

	if (g_verbose) {
		printf("CPU configuration validated:\n");
		printf("  CPU count: %d\n", g_config.num_cpus);
		printf("  CPU speed: %d MHz%s\n", g_config.cpu_speed_mhz,
		    g_config.cpu_speed_mhz == 0 ? " (auto)" : "");
		printf("  CPU level: %s\n", g_config.cpu_level);
	}

	return (0);
}

int
cmd_init(int argc, char *argv[])
{
	int ch;
	int option_index;
	int force = 0;
	static struct option long_options[] = {
		{ "arch", required_argument, NULL, 'a' },
		{ "mode", required_argument, NULL, 'm' },
		{ "name", required_argument, NULL, 'n' },
		{ "cpu-level", required_argument, NULL, 'c' },
		{ "cpu-speed", required_argument, NULL, 's' },
		{ "memory", required_argument, NULL, 'M' },
		{ "cpus", required_argument, NULL, 'C' },
		{ "image", required_argument, NULL, 'i' },
		{ "kernel", required_argument, NULL, 'k' },
		{ "blob", required_argument, NULL, 'b' },
		{ "force", no_argument, NULL, 'f' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	init_default_config();

	while ((ch = getopt_long(argc, argv, "a:m:n:c:s:M:C:i:k:b:fvh",
	    long_options, &option_index)) != -1) {
		switch (ch) {
		case 'a':
			g_config.arch = emu_string_to_arch(optarg);
			if (g_config.arch == EMU_ARCH_UNKNOWN) {
				warnx("Unknown architecture: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 'm':
			g_config.mode = emu_string_to_mode(optarg);
			if (g_config.mode == EMU_MODE_AUTO) {
				/* Auto is valid, but warn if ambiguous */
				if (strcmp(optarg, "auto") != 0) {
					warnx("Unknown mode: %s, using auto", optarg);
				}
			}
			break;

		case 'n':
			if (strlen(optarg) >= EMU_NAME_MAX) {
				warnx("Instance name too long (max %d chars)",
				    EMU_NAME_MAX - 1);
				return (EX_USAGE);
			}
			strlcpy(g_config.name, optarg, sizeof(g_config.name));
			break;

		case 'c':
			if (strlen(optarg) >= EMU_CPU_LEVEL_MAX) {
				warnx("CPU level too long (max %d chars)",
				    EMU_CPU_LEVEL_MAX - 1);
				return (EX_USAGE);
			}
			strlcpy(g_config.cpu_level, optarg,
			    sizeof(g_config.cpu_level));
			break;

		case 's':
			g_config.cpu_speed_mhz = atoi(optarg);
			if (g_config.cpu_speed_mhz < 0) {
				warnx("Invalid CPU speed: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 'M':
			g_config.memory_size = strtoul(optarg, NULL, 0) * 1024 * 1024;
			break;

		case 'C':
			g_config.num_cpus = atoi(optarg);
			if (g_config.num_cpus < 1) {
				warnx("Invalid CPU count: %s", optarg);
				return (EX_USAGE);
			}
			break;

		case 'i':
			strlcpy(g_config.image_path, optarg,
			    sizeof(g_config.image_path));
			break;

		case 'k':
			strlcpy(g_config.kernel_path, optarg,
			    sizeof(g_config.kernel_path));
			break;

		case 'b':
			strlcpy(g_config.blob_path, optarg,
			    sizeof(g_config.blob_path));
			break;

		case 'f':
			force = 1;
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_init();
		}
	}

	argc -= optind;
	argv += optind;

	/* Validate required parameters */
	if (g_config.name[0] == '\0') {
		warnx("Instance name is required (--name)");
		return (EX_USAGE);
	}

	/* Validate configuration */
	if (validate_architecture() != 0)
		return (EX_DATAERR);

	if (validate_memory_size() != 0)
		return (EX_DATAERR);

	if (validate_cpu_config() != 0)
		return (EX_DATAERR);

	if (validate_paths() != 0)
		return (EX_NOINPUT);

	if (check_dependencies() != 0)
		return (EX_UNAVAILABLE);

	/* Check if instance already exists */
	if (!force) {
		char path[MAXPATHLEN];
		struct stat sb;

		snprintf(path, sizeof(path), "%s/%s", EMU_INSTANCE_DIR,
		    g_config.name);
		if (stat(path, &sb) == 0) {
			warnx("Instance '%s' already exists", g_config.name);
			warnx("Use --force to overwrite");
			return (EX_EXISTS);
		}
	}

	/* Create instance directory and configuration */
	if (create_instance_directory(g_config.name) != 0)
		return (EX_CANTCREAT);

	/* Success */
	printf("Initialized emulated instance '%s'\n", g_config.name);
	printf("  Architecture: %s\n", emu_arch_to_string(g_config.arch));
	printf("  Mode: %s\n", emu_mode_to_string(g_config.mode));
	printf("  Memory: %zu MB\n", g_config.memory_size / (1024 * 1024));
	printf("  CPUs: %d\n", g_config.num_cpus);
	if (g_config.image_path[0] != '\0')
		printf("  Image: %s\n", g_config.image_path);
	if (g_config.kernel_path[0] != '\0')
		printf("  Kernel: %s\n", g_config.kernel_path);
	if (g_config.blob_path[0] != '\0')
		printf("  Blob: %s\n", g_config.blob_path);
	printf("\nInstance directory: %s/%s\n", EMU_INSTANCE_DIR, g_config.name);
	printf("Use 'emu start --name %s' to start the instance\n",
	    g_config.name);

	return (0);
}
