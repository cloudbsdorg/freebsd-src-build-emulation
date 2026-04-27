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

#ifndef _EMU_FILE_SHARING_H_
#define	_EMU_FILE_SHARING_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Filesystem Sharing for Custom Emulator
 *
 * This module provides secure filesystem sharing between host and guest.
 * All file operations are validated and translated to safe host paths.
 */

/* Forward declarations */
struct emu_file_sharing;

/*
 * Initialize file sharing subsystem
 *
 * Parameters:
 *   ctx        - File sharing context to initialize
 *   max_shares - Maximum number of shares to support
 *
 * Returns 0 on success, -1 on failure
 */
int emu_file_sharing_init(struct emu_file_sharing *ctx, int max_shares);

/*
 * Add a file share
 *
 * Parameters:
 *   ctx          - File sharing context
 *   host_path    - Path on host to share
 *   guest_prefix - Guest mount point prefix
 *   read_only    - True if share should be read-only
 *
 * Returns 0 on success, -1 on failure
 */
int emu_file_sharing_add(struct emu_file_sharing *ctx, const char *host_path,
    const char *guest_prefix, bool read_only);

/*
 * Remove a file share
 *
 * Parameters:
 *   ctx          - File sharing context
 *   guest_prefix - Guest mount point prefix to remove
 *
 * Returns 0 on success, -1 on failure
 */
int emu_file_sharing_remove(struct emu_file_sharing *ctx, const char *guest_prefix);

/*
 * Check if a guest path is within a shared directory
 *
 * Parameters:
 *   ctx        - File sharing context
 *   guest_path - Path from guest
 *
 * Returns true if path is within a share, false otherwise
 */
bool emu_is_guest_path_shared(struct emu_file_sharing *ctx, const char *guest_path);

/*
 * Open a file on behalf of the guest
 *
 * Parameters:
 *   ctx        - File sharing context
 *   guest_path - Path from guest
 *   flags      - Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 *   mode       - File mode (for creation)
 *
 * Returns file descriptor on success, -1 on failure
 */
int emu_file_open(struct emu_file_sharing *ctx, const char *guest_path,
    int flags, mode_t mode);

/*
 * Close a file opened on behalf of the guest
 *
 * Parameters:
 *   ctx - File sharing context
 *   fd  - File descriptor to close
 *
 * Returns 0 on success, -1 on failure
 */
int emu_file_close(struct emu_file_sharing *ctx, int fd);

/*
 * Read from a file on behalf of the guest
 *
 * Parameters:
 *   ctx   - File sharing context
 *   fd    - File descriptor
 *   buf   - Buffer to read into
 *   count - Number of bytes to read
 *
 * Returns number of bytes read, or -1 on failure
 */
ssize_t emu_file_read(struct emu_file_sharing *ctx, int fd, void *buf, size_t count);

/*
 * Write to a file on behalf of the guest
 *
 * Parameters:
 *   ctx   - File sharing context
 *   fd    - File descriptor
 *   buf   - Buffer to write
 *   count - Number of bytes to write
 *
 * Returns number of bytes written, or -1 on failure
 */
ssize_t emu_file_write(struct emu_file_sharing *ctx, int fd, const void *buf,
    size_t count);

/*
 * Get file status on behalf of the guest
 *
 * Parameters:
 *   ctx     - File sharing context
 *   fd      - File descriptor
 *   statbuf - Output: stat structure
 *
 * Returns 0 on success, -1 on failure
 */
int emu_file_stat(struct emu_file_sharing *ctx, int fd, struct stat *statbuf);

/*
 * Seek to a position in a file on behalf of the guest
 *
 * Parameters:
 *   ctx    - File sharing context
 *   fd     - File descriptor
 *   offset - Offset to seek to
 *   whence - Seek direction (SEEK_SET, SEEK_CUR, SEEK_END)
 *
 * Returns new offset on success, -1 on failure
 */
off_t emu_file_seek(struct emu_file_sharing *ctx, int fd, off_t offset, int whence);

/*
 * Destroy file sharing subsystem
 *
 * Parameters:
 *   ctx - File sharing context to destroy
 */
void emu_file_sharing_destroy(struct emu_file_sharing *ctx);

/*
 * List all active shares (debugging)
 *
 * Parameters:
 *   ctx - File sharing context
 */
void emu_file_sharing_list(struct emu_file_sharing *ctx);

#endif /* !_EMU_FILE_SHARING_H_ */
