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

#ifndef _EMU_SHARE_H_
#define	_EMU_SHARE_H_

#include <sys/types.h>
#include <sys/param.h>
#include <stdint.h>
#include <stdbool.h>

#include "emu_engine.h"

/*
 * Emulation Framework - Secure Filesystem Sharing
 *
 * This module provides controlled filesystem sharing between host
 * and emulated instances with path validation and access control.
 *
 * Security considerations:
 * - All share paths validated with realpath()
 * - Blocked prefixes (/dev, /proc, /sys, /etc)
 * - Read-only by default
 * - Path translation prevents escapes
 * - No symlink following outside share
 */

/* Maximum number of shares per instance */
#define	EMU_MAX_SHARES		8

/* Maximum share path length */
#define	EMU_SHARE_PATH_MAX	PATH_MAX

/* Share flags */
#define	EMU_SHARE_RDONLY	0x01	/* Read-only share */
#define	EMU_SHARE_RDWR		0x02	/* Read-write share */
#define	EMU_SHARE_NOFOLLOW	0x04	/* Don't follow symlinks */
#define	EMU_SHARE_CREATE	0x08	/* Create if not exists */

/* Share descriptor */
struct emu_share {
	char		host_path[EMU_SHARE_PATH_MAX];	/* Host path */
	char		guest_path[EMU_SHARE_PATH_MAX];	/* Guest path */
	char		resolved_path[EMU_SHARE_PATH_MAX]; /* Resolved host path */
	int		flags;		/* Share flags */
	int		fd;		/* Open FD for share */
	uid_t		owner_uid;	/* Owner UID */
	gid_t		owner_gid;	/* Owner GID */
	mode_t		mode;		/* Access mode */
	bool		active;		/* Share is active */
};

/* Share configuration */
struct emu_share_config {
	struct emu_share	shares[EMU_MAX_SHARES];
	int			num_shares;
	bool			initialized;
};

/*
 * Share lifecycle management
 */

/* Initialize share subsystem */
int emu_share_init(struct emu_share_config *config);

/* Add a share to configuration */
int emu_share_add(struct emu_share_config *config, const char *host_path,
    const char *guest_path, int flags);

/* Remove a share from configuration */
int emu_share_remove(struct emu_share_config *config, const char *guest_path);

/* Activate all shares (open FDs, validate paths) */
int emu_share_activate(struct emu_share_config *config);

/* Deactivate all shares (close FDs) */
int emu_share_deactivate(struct emu_share_config *config);

/*
 * Path validation and translation
 */

/* Validate host path for sharing */
int emu_share_validate_path(const char *path);

/* Translate guest path to host path */
int emu_share_translate_path(struct emu_share_config *config,
    const char *guest_path, char *host_path, size_t host_path_len);

/* Check if path is within share boundaries */
bool emu_share_path_within_share(const char *path, const char *share_root);

/*
 * File operations (intercepted)
 */

/* Intercepted open operation */
int emu_share_open(struct emu_share_config *config, const char *guest_path,
    int flags, mode_t mode);

/* Intercepted read operation */
ssize_t emu_share_read(struct emu_share_config *config, int fd, void *buf,
    size_t count);

/* Intercepted write operation */
ssize_t emu_share_write(struct emu_share_config *config, int fd,
    const void *buf, size_t count);

/* Intercepted close operation */
int emu_share_close(struct emu_share_config *config, int fd);

/* Intercepted stat operation */
int emu_share_stat(struct emu_share_config *config, const char *guest_path,
    struct stat *st);

/* Intercepted unlink operation */
int emu_share_unlink(struct emu_share_config *config, const char *guest_path);

/* Intercepted mkdir operation */
int emu_share_mkdir(struct emu_share_config *config, const char *guest_path,
    mode_t mode);

/*
 * Security checks
 */

/* Check if write is allowed on share */
bool emu_share_write_allowed(struct emu_share *share);

/* Check if path access is allowed */
bool emu_share_access_allowed(struct emu_share *share, int access_mode);

/*
 * Utility functions
 */

/* Get share by guest path */
struct emu_share *emu_share_find(struct emu_share_config *config,
    const char *guest_path);

/* Get share count */
int emu_share_count(struct emu_share_config *config);

/* List all shares */
int emu_share_list(struct emu_share_config *config, char **list, int max_items);

/* Dump share configuration for debugging */
void emu_share_dump(struct emu_share_config *config);

/* Convert share flags to string */
const char *emu_share_flags_str(int flags);

#endif /* !_EMU_SHARE_H_ */
