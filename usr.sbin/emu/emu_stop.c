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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"

/*
 * Emulation Framework Userland Tool - Stop Command
 *
 * This command stops an emulated instance.
 */

#define EMU_INSTANCE_DIR	"/var/emu"
#define EMU_STOP_TIMEOUT	10	/* Seconds to wait for graceful shutdown */

static char g_instance_name[EMU_NAME_MAX] = "";
static int g_force = 0;

static void
usage_stop(void)
{
	fprintf(stderr, "Usage: emu stop [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -n, --name=NAME       Instance name (required)\n");
	fprintf(stderr, "  -f, --force           Force immediate shutdown\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  emu stop --name test-instance\n");
	fprintf(stderr, "  emu stop -n test-instance --force\n");
	exit(EX_USAGE);
}

static int
read_pid_file(const char *name, pid_t *pid)
{
	char path[MAXPATHLEN];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s/config/pid", EMU_INSTANCE_DIR, name);
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			/* No PID file - instance not running */
			return (0);
		}
		warn("Failed to open PID file for %s", name);
		return (-1);
	}

	if (fscanf(fp, "%d", pid) != 1) {
		warnx("Failed to read PID from file");
		fclose(fp);
		return (-1);
	}

	fclose(fp);
	return (1);
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
remove_pid_file(const char *name)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "%s/%s/config/pid", EMU_INSTANCE_DIR, name);
	if (unlink(path) != 0 && errno != ENOENT) {
		warn("Failed to remove PID file for %s", name);
		return (-1);
	}

	return (0);
}

static int
wait_for_process_exit(pid_t pid, int timeout_sec)
{
	int elapsed = 0;

	while (elapsed < timeout_sec) {
		/* Check if process is still running */
		if (kill(pid, 0) != 0) {
			if (errno == ESRCH) {
				/* Process exited */
				return (0);
			}
			/* Some other error */
			return (-1);
		}

		/* Wait a bit and check again */
		usleep(100000); /* 100ms */
		elapsed++;
		if (elapsed >= timeout_sec * 10)
			break;
	}

	/* Timeout - process still running */
	return (-1);
}

static int
stop_instance(const char *name, int force)
{
	pid_t pid = 0;
	int error;
	struct sigaction sa;
	sigset_t block_mask;
	siginfo_t info;

	/* Read PID file */
	error = read_pid_file(name, &pid);
	if (error < 0)
		return (-1);
	if (error == 0) {
		/* No PID file - check state */
		char path[MAXPATHLEN];
		char buf[64];
		FILE *fp;

		snprintf(path, sizeof(path), "%s/%s/config/state",
		    EMU_INSTANCE_DIR, name);
		fp = fopen(path, "r");
		if (fp != NULL) {
			if (fgets(buf, sizeof(buf), fp) != NULL) {
				if (strncmp(buf, "STOPPED", 7) == 0) {
					fclose(fp);
					if (g_verbose)
						printf("Instance '%s' is already stopped\n", name);
					return (0);
				}
			}
			fclose(fp);
		}

		warnx("Instance '%s' is not running (no PID file)", name);
		return (-1);
	}

	if (g_verbose)
		printf("Stopping instance '%s' (PID %d)...\n", name, pid);

	/* Check if process is actually running */
	if (kill(pid, 0) != 0) {
		if (errno == ESRCH) {
			/* Process not found - clean up stale PID file */
			if (g_verbose)
				printf("Process %d not found, cleaning up stale PID file\n", pid);
			remove_pid_file(name);
			update_instance_state(name, "STOPPED");
			return (0);
		}
		warn("Failed to check process %d", pid);
		return (-1);
	}

	/* Set up SIGCHLD handler to reap child */
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = NULL; /* Default action */
	sigemptyset(&block_mask);
	sigaction(SIGCHLD, &sa, NULL);

	/* Send SIGTERM for graceful shutdown */
	if (!force) {
		if (g_verbose)
			printf("Sending SIGTERM to process %d\n", pid);

		if (kill(pid, SIGTERM) != 0) {
			warn("Failed to send SIGTERM to %d", pid);
			/* Try SIGKILL instead */
			force = 1;
		}

		/* Wait for process to exit gracefully */
		if (g_verbose)
			printf("Waiting for process to exit (timeout %d seconds)...\n",
			    EMU_STOP_TIMEOUT);

		if (wait_for_process_exit(pid, EMU_STOP_TIMEOUT) != 0) {
			if (g_verbose)
				printf("Graceful shutdown timed out, forcing...\n");
			force = 1;
		}
	}

	/* Force kill if needed */
	if (force) {
		if (g_verbose)
			printf("Sending SIGKILL to process %d\n", pid);

		if (kill(pid, SIGKILL) != 0) {
			if (errno == ESRCH) {
				/* Process already exited */
				if (g_verbose)
					printf("Process already exited\n");
			} else {
				warn("Failed to send SIGKILL to %d", pid);
				return (-1);
			}
		} else {
			/* Wait for SIGKILL to take effect */
			usleep(100000); /* 100ms */
		}
	}

	/* Clean up PID file */
	remove_pid_file(name);

	/* Update state */
	if (update_instance_state(name, "STOPPED") != 0) {
		warnx("Failed to update instance state");
		return (-1);
	}

	if (g_verbose)
		printf("Instance '%s' stopped successfully\n", name);

	return (0);
}

int
cmd_stop(int argc, char *argv[])
{
	int ch;
	int option_index;
	static struct option long_options[] = {
		{ "name", required_argument, NULL, 'n' },
		{ "force", no_argument, NULL, 'f' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "n:fvh",
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

		case 'f':
			g_force = 1;
			break;

		case 'v':
			g_verbose = 1;
			break;

		case 'h':
		default:
			usage_stop();
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

	/* Stop the instance */
	int error = stop_instance(g_instance_name, g_force);
	if (error != 0)
		return (EX_IOERR);

	/* Success */
	printf("Stopped emulated instance '%s'\n", g_instance_name);

	return (0);
}
