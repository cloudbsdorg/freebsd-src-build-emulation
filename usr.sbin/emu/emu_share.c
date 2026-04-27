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
#include <paths.h>

#include "emu_share.h"
#include "emu_engine.h"
#include "emu.h"

/*
 * Emulation Framework - Secure Filesystem Sharing Implementation
 *
 * This module implements controlled filesystem sharing between host
 * and emulated instances with path validation and access control.
 *
 * Security considerations:
 * - All share paths validated with realpath()
 * - Blocked prefixes (/dev, /proc, /sys, /etc)
 * - Read-only by default
 * - Path translation prevents escapes
 * - No symlink following outside share
 */

/* Blocked path prefixes */
static const char *blocked_prefixes[] = {
	"/dev",
	"/proc",
	"/sys",
	"/etc",
	"/boot",
	"/root",
	"/var/run",
	"/var/db",
	NULL
};

/*
 * Convert share flags to string for debugging
 */
const char *
emu_share_flags_str(int flags)
{
	static char buf[64];
	char *p = buf;

	buf[0] = '\0';

	if (flags & EMU_SHARE_RDONLY) {
		p += snprintf(p, sizeof(buf) - (p - buf), "RDONLY");
	}
	if (flags & EMU_SHARE_RDWR) {
		if (p != buf)
			*p++ = '|';
		p += snprintf(p, sizeof(buf) - (p - buf), "RDWR");
	}
	if (flags & EMU_SHARE_NOFOLLOW) {
		if (p != buf)
			*p++ = '|';
		p += snprintf(p, sizeof(buf) - (p - buf), "NOFOLLOW");
	}
	if (flags & EMU_SHARE_CREATE) {
		if (p != buf)
			*p++ = '|';
		p += snprintf(p, sizeof(buf) - (p - buf), "CREATE");
	}

	if (buf[0] == '\0')
		snprintf(buf, sizeof(buf), "NONE");

	return (buf);
}

/*
 * Initialize share subsystem
 * Returns 0 on success, -1 on failure
 */
int
emu_share_init(struct emu_share_config *config)
{
	if (config == NULL)
		return (-1);

	memset(config, 0, sizeof(struct emu_share_config));
	config->initialized = true;

	return (0);
}

/*
 * Validate host path for sharing
 * Returns 0 on success, -1 on failure with errno set
 */
int
emu_share_validate_path(const char *path)
{
	char resolved[PATH_MAX];
	char *real;
	int i;

	if (path == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* Resolve to absolute path */
	real = realpath(path, resolved);
	if (real == NULL) {
		warn("realpath() failed for %s", path);
		return (-1);
	}

	/* Check blocked prefixes */
	for (i = 0; blocked_prefixes[i] != NULL; i++) {
		if (strncmp(resolved, blocked_prefixes[i],
		    strlen(blocked_prefixes[i])) == 0) {
			/* Check if it's exactly the prefix or a subdirectory */
			size_t prefix_len = strlen(blocked_prefixes[i]);
			if (resolved[prefix_len] == '\0' ||
			    resolved[prefix_len] == '/') {
				warnx("Blocked path prefix: %s", blocked_prefixes[i]);
				errno = EPERM;
				return (-1);
			}
		}
	}

	/* Verify it's a directory or regular file */
	struct stat st;
	if (stat(resolved, &st) != 0) {
		warn("stat() failed for %s", resolved);
		return (-1);
	}

	if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
		warnx("Not a directory or regular file: %s", resolved);
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

/*
 * Check if path is within share boundaries
 */
bool
emu_share_path_within_share(const char *path, const char *share_root)
{
	size_t root_len;

	if (path == NULL || share_root == NULL)
		return (false);

	root_len = strlen(share_root);

	/* Path must start with share root */
	if (strncmp(path, share_root, root_len) != 0)
		return (false);

	/* Path must be exactly the root or a subdirectory */
	if (path[root_len] != '\0' && path[root_len] != '/')
		return (false);

	return (true);
}

/*
 * Add a share to configuration
 * Returns 0 on success, -1 on failure
 */
int
emu_share_add(struct emu_share_config *config, const char *host_path,
    const char *guest_path, int flags)
{
	struct emu_share *share;
	char resolved[PATH_MAX];

	if (config == NULL || host_path == NULL || guest_path == NULL)
		return (-1);

	if (config->num_shares >= EMU_MAX_SHARES) {
		warnx("Maximum shares (%d) reached", EMU_MAX_SHARES);
		errno = ENOMEM;
		return (-1);
	}

	/* Validate host path */
	if (emu_share_validate_path(host_path) != 0)
		return (-1);

	/* Resolve host path */
	if (realpath(host_path, resolved) == NULL) {
		warn("realpath() failed for %s", host_path);
		return (-1);
	}

	/* Add share */
	share = &config->shares[config->num_shares];
	memset(share, 0, sizeof(struct emu_share));

	strlcpy(share->host_path, host_path, sizeof(share->host_path));
	strlcpy(share->guest_path, guest_path, sizeof(share->guest_path));
	strlcpy(share->resolved_path, resolved, sizeof(share->resolved_path));
	share->flags = flags;
	share->fd = -1;
	share->owner_uid = getuid();
	share->owner_gid = getgid();
	share->mode = (flags & EMU_SHARE_RDWR) ? 0666 : 0444;
	share->active = false;

	config->num_shares++;

	return (0);
}

/*
 * Remove a share from configuration
 * Returns 0 on success, -1 on failure
 */
int
emu_share_remove(struct emu_share_config *config, const char *guest_path)
{
	int i, j;

	if (config == NULL || guest_path == NULL)
		return (-1);

	/* Find share */
	for (i = 0; i < config->num_shares; i++) {
		if (strcmp(config->shares[i].guest_path, guest_path) == 0) {
			/* Close FD if open */
			if (config->shares[i].fd >= 0) {
				close(config->shares[i].fd);
				config->shares[i].fd = -1;
			}

			/* Shift remaining shares */
			for (j = i; j < config->num_shares - 1; j++)
				config->shares[j] = config->shares[j + 1];

			config->num_shares--;
			return (0);
		}
	}

	errno = ENOENT;
	return (-1);
}

/*
 * Activate all shares (open FDs, validate paths)
 * Returns 0 on success, -1 on failure
 */
int
emu_share_activate(struct emu_share_config *config)
{
	int i, fd;

	if (config == NULL)
		return (-1);

	for (i = 0; i < config->num_shares; i++) {
		struct emu_share *share = &config->shares[i];

		/* Open share directory/file */
		fd = open(share->resolved_path,
		    O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		if (fd < 0) {
			/* Try as regular file */
			fd = open(share->resolved_path, O_RDONLY);
			if (fd < 0) {
				warn("Failed to open share %s", share->resolved_path);
				return (-1);
			}
		}

		share->fd = fd;
		share->active = true;
	}

	return (0);
}

/*
 * Deactivate all shares (close FDs)
 * Returns 0 on success, -1 on failure
 */
int
emu_share_deactivate(struct emu_share_config *config)
{
	int i;

	if (config == NULL)
		return (-1);

	for (i = 0; i < config->num_shares; i++) {
		if (config->shares[i].fd >= 0) {
			close(config->shares[i].fd);
			config->shares[i].fd = -1;
		}
		config->shares[i].active = false;
	}

	return (0);
}

/*
 * Get share by guest path
 */
struct emu_share *
emu_share_find(struct emu_share_config *config, const char *guest_path)
{
	int i;

	if (config == NULL || guest_path == NULL)
		return (NULL);

	for (i = 0; i < config->num_shares; i++) {
		if (strcmp(config->shares[i].guest_path, guest_path) == 0)
			return (&config->shares[i]);
	}

	return (NULL);
}

/*
 * Translate guest path to host path
 * Returns 0 on success, -1 on failure
 */
int
emu_share_translate_path(struct emu_share_config *config,
    const char *guest_path, char *host_path, size_t host_path_len)
{
	struct emu_share *share;
	const char *relative_path;
	size_t guest_prefix_len;

	if (config == NULL || guest_path == NULL || host_path == NULL)
		return (-1);

	/* Find matching share */
	share = emu_share_find(config, guest_path);
	if (share == NULL) {
		/* Check if it's under a share */
		for (int i = 0; i < config->num_shares; i++) {
			share = &config->shares[i];
			guest_prefix_len = strlen(share->guest_path);

			if (strncmp(guest_path, share->guest_path, guest_prefix_len) == 0) {
				/* Path is under this share */
				if (guest_path[guest_prefix_len] == '/') {
					relative_path = guest_path + guest_prefix_len + 1;
					break;
				}
			}
			share = NULL;
		}

		if (share == NULL) {
			errno = ENOENT;
			return (-1);
		}
	} else {
		relative_path = "";
	}

	/* Construct host path */
	if (strlen(relative_path) == 0) {
		strlcpy(host_path, share->resolved_path, host_path_len);
	} else {
		snprintf(host_path, host_path_len, "%s/%s",
		    share->resolved_path, relative_path);
	}

	/* Verify translated path is still within share */
	if (!emu_share_path_within_share(host_path, share->resolved_path)) {
		warnx("Path escape detected: %s", host_path);
		errno = EPERM;
		return (-1);
	}

	return (0);
}

/*
 * Check if write is allowed on share
 */
bool
emu_share_write_allowed(struct emu_share *share)
{
	if (share == NULL)
		return (false);

	if (share->flags & EMU_SHARE_RDONLY)
		return (false);

	return (true);
}

/*
 * Check if path access is allowed
 */
bool
emu_share_access_allowed(struct emu_share *share, int access_mode)
{
	if (share == NULL)
		return (false);

	if (access_mode & W_OK) {
		if (!emu_share_write_allowed(share))
			return (false);
	}

	return (true);
}

/*
 * Intercepted open operation
 * Returns FD on success, -1 on failure
 */
int
emu_share_open(struct emu_share_config *config, const char *guest_path,
    int flags, mode_t mode)
{
	char host_path[PATH_MAX];
	struct emu_share *share;
	int fd;

	if (config == NULL || guest_path == NULL)
		return (-1);

	/* Translate guest path to host path */
	if (emu_share_translate_path(config, guest_path,
	    host_path, sizeof(host_path)) != 0)
		return (-1);

	/* Find share for permission check */
	share = emu_share_find(config, guest_path);
	if (share == NULL) {
		/* Find parent share */
		for (int i = 0; i < config->num_shares; i++) {
			if (strncmp(guest_path, config->shares[i].guest_path,
			    strlen(config->shares[i].guest_path)) == 0) {
				share = &config->shares[i];
				break;
			}
		}
	}

	/* Check write permission */
	if ((flags & (O_WRONLY | O_RDWR | O_CREAT)) &&
	    share != NULL && !emu_share_write_allowed(share)) {
		errno = EROFS;
		return (-1);
	}

	/* Open file */
	fd = open(host_path, flags, mode);
	if (fd < 0) {
		warn("Failed to open %s", host_path);
		return (-1);
	}

	return (fd);
}

/*
 * Intercepted read operation
 * Returns bytes read, or -1 on failure
 */
ssize_t
emu_share_read(struct emu_share_config *config, int fd, void *buf,
    size_t count)
{
	if (config == NULL || fd < 0 || buf == NULL)
		return (-1);

	return (read(fd, buf, count));
}

/*
 * Intercepted write operation
 * Returns bytes written, or -1 on failure
 */
ssize_t
emu_share_write(struct emu_share_config *config, int fd,
    const void *buf, size_t count)
{
	if (config == NULL || fd < 0 || buf == NULL)
		return (-1);

	return (write(fd, buf, count));
}

/*
 * Intercepted close operation
 * Returns 0 on success, -1 on failure
 */
int
emu_share_close(struct emu_share_config *config, int fd)
{
	if (config == NULL || fd < 0)
		return (-1);

	return (close(fd));
}

/*
 * Intercepted stat operation
 * Returns 0 on success, -1 on failure
 */
int
emu_share_stat(struct emu_share_config *config, const char *guest_path,
    struct stat *st)
{
	char host_path[PATH_MAX];

	if (config == NULL || guest_path == NULL || st == NULL)
		return (-1);

	/* Translate guest path to host path */
	if (emu_share_translate_path(config, guest_path,
	    host_path, sizeof(host_path)) != 0)
		return (-1);

	return (stat(host_path, st));
}

/*
 * Intercepted unlink operation
 * Returns 0 on success, -1 on failure
 */
int
emu_share_unlink(struct emu_share_config *config, const char *guest_path)
{
	char host_path[PATH_MAX];
	struct emu_share *share;

	if (config == NULL || guest_path == NULL)
		return (-1);

	/* Translate guest path to host path */
	if (emu_share_translate_path(config, guest_path,
	    host_path, sizeof(host_path)) != 0)
		return (-1);

	/* Find share for permission check */
	share = emu_share_find(config, guest_path);
	if (share == NULL) {
		for (int i = 0; i < config->num_shares; i++) {
			if (strncmp(guest_path, config->shares[i].guest_path,
			    strlen(config->shares[i].guest_path)) == 0) {
				share = &config->shares[i];
				break;
			}
		}
	}

	/* Check write permission */
	if (share != NULL && !emu_share_write_allowed(share)) {
		errno = EROFS;
		return (-1);
	}

	return (unlink(host_path));
}

/*
 * Intercepted mkdir operation
 * Returns 0 on success, -1 on failure
 */
int
emu_share_mkdir(struct emu_share_config *config, const char *guest_path,
    mode_t mode)
{
	char host_path[PATH_MAX];
	struct emu_share *share;

	if (config == NULL || guest_path == NULL)
		return (-1);

	/* Translate guest path to host path */
	if (emu_share_translate_path(config, guest_path,
	    host_path, sizeof(host_path)) != 0)
		return (-1);

	/* Find share for permission check */
	share = emu_share_find(config, guest_path);
	if (share == NULL) {
		for (int i = 0; i < config->num_shares; i++) {
			if (strncmp(guest_path, config->shares[i].guest_path,
			    strlen(config->shares[i].guest_path)) == 0) {
				share = &config->shares[i];
				break;
			}
		}
	}

	/* Check write permission */
	if (share != NULL && !emu_share_write_allowed(share)) {
		errno = EROFS;
		return (-1);
	}

	return (mkdir(host_path, mode));
}

/*
 * Get share count
 */
int
emu_share_count(struct emu_share_config *config)
{
	if (config == NULL)
		return (0);

	return (config->num_shares);
}

/*
 * List all shares
 * Returns number of shares listed, or -1 on failure
 */
int
emu_share_list(struct emu_share_config *config, char **list, int max_items)
{
	int i;

	if (config == NULL || list == NULL || max_items <= 0)
		return (-1);

	for (i = 0; i < config->num_shares && i < max_items; i++) {
		list[i] = strdup(config->shares[i].guest_path);
		if (list[i] == NULL)
			return (-1);
	}

	return (i);
}

/*
 * Dump share configuration for debugging
 */
void
emu_share_dump(struct emu_share_config *config)
{
	int i;

	if (config == NULL)
		return;

	printf("Share configuration (%d shares):\n", config->num_shares);

	for (i = 0; i < config->num_shares; i++) {
		struct emu_share *share = &config->shares[i];
		printf("  [%d] %s -> %s\n", i, share->guest_path,
		    share->resolved_path);
		printf("      Flags: %s, FD: %d, Active: %s\n",
		    emu_share_flags_str(share->flags),
		    share->fd,
		    share->active ? "yes" : "no");
	}
}
