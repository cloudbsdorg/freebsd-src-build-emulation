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

#ifndef _EMU_ZFS_H_
#define	_EMU_ZFS_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * ZFS Snapshot Integration for Emulation Framework
 *
 * This module provides ZFS snapshot and rollback capabilities
 * for emulation instance state management.
 */

/* Maximum dataset name length */
#define	EMU_ZFS_DATASET_MAX	512
#define	EMU_ZFS_SNAP_MAX	256

/*
 * ZFS context for an instance
 */
struct emu_zfs_ctx {
	char		dataset[EMU_ZFS_DATASET_MAX];	/* ZFS dataset name */
	char		instance_dir[PATH_MAX];		/* Instance directory */
	bool		available;			/* ZFS available */
	time_t		last_snapshot;			/* Last snapshot time */
};

/*
 * Initialize ZFS subsystem for an instance
 *
 * Parameters:
 *   ctx         - ZFS context to initialize
 *   instance_dir - Instance directory path
 *
 * Returns 0 on success, -1 on failure
 */
int emu_zfs_init(struct emu_zfs_ctx *ctx, const char *instance_dir);

/*
 * Create a snapshot of an instance
 *
 * Parameters:
 *   ctx           - ZFS context
 *   instance_name - Instance name
 *   snap_name     - Output: created snapshot name (optional)
 *   snap_name_len - Size of snap_name buffer
 *
 * Returns 0 on success, -1 on failure
 */
int emu_zfs_snapshot(struct emu_zfs_ctx *ctx, const char *instance_name,
    char *snap_name, size_t snap_name_len);

/*
 * Rollback an instance to a snapshot
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to rollback to
 *
 * Returns 0 on success, -1 on failure
 */
int emu_zfs_rollback_to_snapshot(struct emu_zfs_ctx *ctx, const char *snap_name);

/*
 * Destroy a snapshot
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to destroy
 *
 * Returns 0 on success, -1 on failure
 */
int emu_zfs_destroy_snapshot(struct emu_zfs_ctx *ctx, const char *snap_name);

/*
 * List all snapshots for an instance
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snapshots - Output: array of snapshot names (caller must free)
 *   max_snaps - Maximum number of snapshots to return
 *
 * Returns number of snapshots found, or -1 on failure
 */
int emu_zfs_list_snapshots(struct emu_zfs_ctx *ctx, char ***snapshots, int max_snaps);

/*
 * Free snapshot list
 *
 * Parameters:
 *   snapshots - Array of snapshot names to free
 *   count     - Number of snapshots in the array
 */
void emu_zfs_free_snapshot_list(char **snapshots, int count);

/*
 * Check if a snapshot exists
 *
 * Parameters:
 *   ctx       - ZFS context
 *   snap_name - Name of the snapshot to check
 *
 * Returns true if snapshot exists, false otherwise
 */
bool emu_zfs_snapshot_exists(struct emu_zfs_ctx *ctx, const char *snap_name);

/*
 * Destroy ZFS context
 *
 * Parameters:
 *   ctx - ZFS context to destroy
 */
void emu_zfs_destroy(struct emu_zfs_ctx *ctx);

/*
 * Get the last snapshot time
 *
 * Parameters:
 *   ctx - ZFS context
 *
 * Returns timestamp of last snapshot, or 0 if no snapshots
 */
time_t emu_zfs_get_last_snapshot_time(struct emu_zfs_ctx *ctx);

/*
 * Check if ZFS is available for snapshots
 *
 * Parameters:
 *   ctx - ZFS context
 *
 * Returns true if ZFS snapshots are available, false otherwise
 */
bool emu_zfs_is_available(struct emu_zfs_ctx *ctx);

#endif /* !_EMU_ZFS_H_ */
