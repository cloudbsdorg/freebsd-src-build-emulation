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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <paths.h>

#include "emu.h"
#include "emu_zfs.h"

/*
 * ZFS Snapshot Integration for Emulation Framework
 *
 * This module provides ZFS snapshot and rollback capabilities
 * for emulation instance state management. It allows saving
 * and restoring instance states using ZFS filesystem features.
 *
 * Requirements:
 * - Instance directories must be on ZFS filesystems
 * - User must have snapshot permissions (or be root)
 * - ZFS must be installed and loaded
 */

/* Maximum length for a snapshot name */
#define	EMU_ZFS_SNAP_NAME_MAX		256
#define	EMU_ZFS_DATASET_NAME_MAX	512
#define	EMU_ZFS_PATH_MAX		PATH_MAX

/*
 * Check if ZFS is available on the system
 *
 * Returns true if ZFS is available, false otherwise
 */
static bool
emu_zfs_is_available_internal(void)
{
	struct stat sb;

	/* Check if /dev/zfs exists */
	if (stat(_PATH_DEV "zfs", &sb) != 0)
		return (false);

	/* Check if zfs command is available */
	if (access("/sbin/zfs", X_OK) != 0)
		return (false);

	return (true);
}

/*
 * Check if ZFS is available for snapshots (public API)
 *
 * Parameters:
 *   ctx - ZFS context
 *
 * Returns true if ZFS snapshots are available, false otherwise
 */
bool
emu_zfs_is_available(struct emu_zfs_ctx *ctx)
{
	if (ctx == NULL)
		return (false);

	if (!emu_zfs_is_available_internal())
		return (false);

	ctx->available = true;
	return (true);
}

/*
 * Get the ZFS dataset name for a given path
 *
 * This uses zfs list -H -o name <path> to determine the dataset
 *
 * Parameters:
 *   path        - Path to check
 *   dataset     - Output: dataset name
 *   dataset_len - Size of dataset buffer
 *
 * Returns 0 on success, -1 on failure
 */
static int
emu_zfs_get_dataset(const char *path, char *dataset, size_t dataset_len)
{
	char cmd[PATH_MAX + 64];
	FILE *fp;
	char *newline;

	if (!emu_zfs_is_available_internal()) {
		warnx("ZFS is not available on this system");
		return (-1);
	}

	/* Use zfs list to get the dataset name */
	snprintf(cmd, sizeof(cmd), "/sbin/zfs list -H -o name '%s' 2>/dev/null", path);
	
	fp = popen(cmd, "r");
	if (fp == NULL) {
		warn("popen() failed");
		return (-1);
	}

	if (fgets(dataset, dataset_len, fp) == NULL) {
		pclose(fp);
		warnx("Path %s is not on a ZFS filesystem", path);
		return (-1);
	}

	pclose(fp);

	/* Remove trailing newline */
	newline = strchr(dataset, '\n');
	if (newline != NULL)
		*newline = '\0';

	/* Validate dataset name */
	if (strlen(dataset) == 0 || strchr(dataset, ' ') != NULL) {
		warnx("Invalid dataset name returned");
		return (-1);
	}

	return (0);
}

/*
 * Create a ZFS snapshot
 *
 * Parameters:
 *   dataset    - ZFS dataset name
 *   snap_name  - Name for the snapshot (without @ symbol)
 *   recursive  - True to create recursive snapshots
 *
 * Returns 0 on success, -1 on failure
 */
static int
emu_zfs_snapshot_create(const char *dataset, const char *snap_name, bool recursive)
{
	char cmd[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX + 32];
	char full_snap_name[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX];
	int ret;

	/* Construct full snapshot name: dataset@snap_name */
	snprintf(full_snap_name, sizeof(full_snap_name), "%s@%s", dataset, snap_name);

	/* Create the snapshot */
	if (recursive) {
		snprintf(cmd, sizeof(cmd), "/sbin/zfs snapshot -r '%s' 2>&1", full_snap_name);
	} else {
		snprintf(cmd, sizeof(cmd), "/sbin/zfs snapshot '%s' 2>&1", full_snap_name);
	}

	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to create ZFS snapshot %s", full_snap_name);
		return (-1);
	}

	if (g_verbose)
		printf("Created ZFS snapshot: %s\n", full_snap_name);

	return (0);
}

/*
 * Destroy a ZFS snapshot
 *
 * Parameters:
 *   dataset   - ZFS dataset name
 *   snap_name - Name of the snapshot to destroy
 *
 * Returns 0 on success, -1 on failure
 */
static int
emu_zfs_snapshot_destroy(const char *dataset, const char *snap_name)
{
	char cmd[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX + 32];
	char full_snap_name[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX];
	int ret;

	/* Construct full snapshot name: dataset@snap_name */
	snprintf(full_snap_name, sizeof(full_snap_name), "%s@%s", dataset, snap_name);

	/* Destroy the snapshot */
	snprintf(cmd, sizeof(cmd), "/sbin/zfs destroy '%s' 2>&1", full_snap_name);

	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to destroy ZFS snapshot %s", full_snap_name);
		return (-1);
	}

	if (g_verbose)
		printf("Destroyed ZFS snapshot: %s\n", full_snap_name);

	return (0);
}

/*
 * Rollback to a ZFS snapshot
 *
 * Parameters:
 *   dataset   - ZFS dataset name
 *   snap_name - Name of the snapshot to rollback to
 *   recursive - True to perform recursive rollback
 *
 * Returns 0 on success, -1 on failure
 */
static int
emu_zfs_rollback(const char *dataset, const char *snap_name, bool recursive)
{
	char cmd[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX + 32];
	char full_snap_name[EMU_ZFS_DATASET_NAME_MAX + EMU_ZFS_SNAP_NAME_MAX];
	int ret;

	/* Construct full snapshot name: dataset@snap_name */
	snprintf(full_snap_name, sizeof(full_snap_name), "%s@%s", dataset, snap_name);

	/* Perform rollback */
	if (recursive) {
		snprintf(cmd, sizeof(cmd), "/sbin/zfs rollback -r '%s' 2>&1", full_snap_name);
	} else {
		snprintf(cmd, sizeof(cmd), "/sbin/zfs rollback '%s' 2>&1", full_snap_name);
	}

	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to rollback to ZFS snapshot %s", full_snap_name);
		return (-1);
	}

	if (g_verbose)
		printf("Rolled back to ZFS snapshot: %s\n", full_snap_name);

	return (0);
}

/*
 * List ZFS snapshots for a dataset
 *
 * Parameters:
 *   dataset    - ZFS dataset name
 *   snapshots  - Output: array of snapshot names (caller must free)
 *   max_snaps  - Maximum number of snapshots to return
 *
 * Returns number of snapshots found, or -1 on failure
 */
static int
emu_zfs_list_snapshots_internal(const char *dataset, char ***snapshots, int max_snaps)
{
	char cmd[EMU_ZFS_DATASET_NAME_MAX + 64];
	FILE *fp;
	char line[EMU_ZFS_SNAP_NAME_MAX];
	char *snap_name;
	int count = 0;

	if (!emu_zfs_is_available_internal()) {
		warnx("ZFS is not available on this system");
		return (-1);
	}

	/* Allocate array for snapshot names */
	*snapshots = calloc(max_snaps, sizeof(char *));
	if (*snapshots == NULL)
		return (-1);

	/* Use zfs list to get snapshots */
	snprintf(cmd, sizeof(cmd), "/sbin/zfs list -H -t snapshot -o name '%s' 2>/dev/null", dataset);
	
	fp = popen(cmd, "r");
	if (fp == NULL) {
		free(*snapshots);
		*snapshots = NULL;
		warn("popen() failed");
		return (-1);
	}

	while (fgets(line, sizeof(line), fp) != NULL && count < max_snaps) {
		/* Remove trailing newline */
		char *newline = strchr(line, '\n');
		if (newline != NULL)
			*newline = '\0';

		/* Extract snapshot name (after @) */
		snap_name = strchr(line, '@');
		if (snap_name == NULL)
			continue;
		snap_name++; /* Skip @ */

		/* Check if this snapshot belongs to our dataset */
		if (strncmp(line, dataset, strlen(dataset)) != 0)
			continue;

		/* Allocate and copy snapshot name */
		(*snapshots)[count] = strdup(snap_name);
		if ((*snapshots)[count] == NULL)
			break;

		count++;
	}

	pclose(fp);

	return (count);
}

/* (removed duplicate emu_zfs_list_snapshots wrapper) */

/*
 * Generate a snapshot name based on instance name and timestamp
 *
 * Parameters:
 *   instance_name - Instance name
 *   snap_name     - Output: generated snapshot name
 *   snap_name_len - Size of snap_name buffer
 *
 * Returns 0 on success, -1 on failure
 */
static int
emu_zfs_generate_snap_name(const char *instance_name, char *snap_name, size_t snap_name_len)
{
	time_t now;
	struct tm *tm_info;
	char timestamp[64];

	if (instance_name == NULL || snap_name == NULL || snap_name_len == 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Get current time */
	now = time(NULL);
	tm_info = localtime(&now);
	if (tm_info == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Format timestamp: YYYYMMDD_HHMMSS */
	strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

	/* Generate snapshot name: emu_<instance>_<timestamp> */
	snprintf(snap_name, snap_name_len, "emu_%s_%s", instance_name, timestamp);

	/* Validate snapshot name (no spaces, special chars) */
	if (strpbrk(snap_name, " \t\n\r@") != NULL) {
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

/*
 * Initialize ZFS subsystem for an instance
 *
 * Parameters:
 *   ctx         - ZFS context to initialize
 *   instance_dir - Instance directory path
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_zfs_init(struct emu_zfs_ctx *ctx, const char *instance_dir)
{
	if (ctx == NULL || instance_dir == NULL) {
		errno = EINVAL;
		return (-1);
	}

	memset(ctx, 0, sizeof(struct emu_zfs_ctx));

	/* Check if ZFS is available */
	if (!emu_zfs_is_available_internal()) {
		warnx("ZFS is not available - snapshots will not be supported");
		ctx->available = false;
		return (0); /* Not a fatal error */
	}

	/* Get the dataset for the instance directory */
	if (emu_zfs_get_dataset(instance_dir, ctx->dataset, sizeof(ctx->dataset)) != 0) {
		warnx("Instance directory %s is not on a ZFS filesystem", instance_dir);
		ctx->available = false;
		return (0); /* Not a fatal error */
	}

	ctx->available = true;
	strlcpy(ctx->instance_dir, instance_dir, sizeof(ctx->instance_dir));

	if (g_verbose)
		printf("ZFS initialized for instance: dataset=%s\n", ctx->dataset);

	return (0);
}

/*
 * Create a snapshot of an instance
 *
 * Parameters:
 *   ctx         - ZFS context
 *   instance_name - Instance name
 *   snap_name   - Output: created snapshot name (optional)
 *   snap_name_len - Size of snap_name buffer
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_zfs_snapshot(struct emu_zfs_ctx *ctx, const char *instance_name, 
    char *snap_name, size_t snap_name_len)
{
	char generated_name[EMU_ZFS_SNAP_NAME_MAX];
	int ret;

	if (ctx == NULL || instance_name == NULL || !ctx->available) {
		errno = EINVAL;
		return (-1);
	}

	/* Generate snapshot name if not provided */
	if (snap_name == NULL || snap_name_len == 0) {
		if (emu_zfs_generate_snap_name(instance_name, generated_name, 
		    sizeof(generated_name)) != 0)
			return (-1);
		snap_name = generated_name;
		snap_name_len = sizeof(generated_name);
	}

	/* Create the snapshot */
	ret = emu_zfs_snapshot_create(ctx->dataset, snap_name, true);
	if (ret != 0)
		return (-1);

	/* Copy snapshot name to output if provided */
	if (snap_name != NULL && snap_name_len > 0)
		strlcpy(snap_name, snap_name, snap_name_len);

	ctx->last_snapshot = time(NULL);

	return (0);
}

/*
 * Rollback an instance to a snapshot
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to rollback to
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_zfs_rollback_to_snapshot(struct emu_zfs_ctx *ctx, const char *snap_name)
{
	if (ctx == NULL || snap_name == NULL || !ctx->available) {
		errno = EINVAL;
		return (-1);
	}

	return (emu_zfs_rollback(ctx->dataset, snap_name, true));
}

/*
 * Destroy a snapshot
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to destroy
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_zfs_destroy_snapshot(struct emu_zfs_ctx *ctx, const char *snap_name)
{
	if (ctx == NULL || snap_name == NULL || !ctx->available) {
		errno = EINVAL;
		return (-1);
	}

	return (emu_zfs_snapshot_destroy(ctx->dataset, snap_name));
}

/*
 * List all snapshots for an instance
 *
 * Parameters:
 *   ctx        - ZFS context
 *   snapshots  - Output: array of snapshot names
 *   max_snaps  - Maximum number of snapshots to return
 *
 * Returns number of snapshots found, or -1 on failure
 */
int
emu_zfs_list_snapshots(struct emu_zfs_ctx *ctx, char ***snapshots, int max_snaps)
{
	if (ctx == NULL || snapshots == NULL || max_snaps <= 0 || !ctx->available) {
		errno = EINVAL;
		return (-1);
	}

	return (emu_zfs_list_snapshots_internal(ctx->dataset, snapshots, max_snaps));
}

/*
 * Free snapshot list
 *
 * Parameters:
 *   snapshots - Array of snapshot names to free
 *   count     - Number of snapshots in the array
 */
void
emu_zfs_free_snapshot_list(char **snapshots, int count)
{
	int i;

	if (snapshots == NULL)
		return;

	for (i = 0; i < count; i++) {
		if (snapshots[i] != NULL)
			free(snapshots[i]);
	}

	free(snapshots);
}

/*
 * Check if a snapshot exists
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to check
 *
 * Returns true if snapshot exists, false otherwise
 */
bool
emu_zfs_snapshot_exists(struct emu_zfs_ctx *ctx, const char *snap_name)
{
	char **snapshots;
	int count;
	int i;
	bool found = false;

	if (ctx == NULL || snap_name == NULL || !ctx->available)
		return (false);

	count = emu_zfs_list_snapshots_internal(ctx->dataset, &snapshots, 1024);
	if (count < 0)
		return (false);

	for (i = 0; i < count; i++) {
		if (strcmp(snapshots[i], snap_name) == 0) {
			found = true;
			break;
		}
	}

	emu_zfs_free_snapshot_list(snapshots, count);

	return (found);
}

/*
 * Destroy ZFS context
 *
 * Parameters:
 *   ctx - ZFS context to destroy
 */
void
emu_zfs_destroy(struct emu_zfs_ctx *ctx)
{
	if (ctx == NULL)
		return;

	memset(ctx, 0, sizeof(struct emu_zfs_ctx));
}

/*
 * Get the last snapshot time
 *
 * Parameters:
 *   ctx - ZFS context
 *
 * Returns timestamp of last snapshot, or 0 if no snapshots
 */
time_t
emu_zfs_get_last_snapshot_time(struct emu_zfs_ctx *ctx)
{
	if (ctx == NULL)
		return (0);

	return (ctx->last_snapshot);
}
