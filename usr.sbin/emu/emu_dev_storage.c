/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 * Copyright (c) 2026 JetBrains s.r.o.
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <paths.h>

#include "emu_dev_storage.h"

/*
 * Virtio Block Device Emulation
 * 
 * Security: I/O only to disk image file, never to host block devices.
 * Path validation prevents access to raw devices (/dev/).
 * Bounds checking prevents out-of-range I/O.
 */

/*
 * Validate image path - security critical
 * Returns 0 on success, -1 on failure with errno set
 */
int
emu_blk_validate_path(const char *path)
{
    struct stat sb;
    char *realpath_buf;
    const char *blocked_prefixes[] = {
        "/dev/",
        "/proc/",
        "/sys/",
        NULL
    };
    int i;
    
    if (path == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Check for blocked prefixes */
    for (i = 0; blocked_prefixes[i] != NULL; i++) {
        if (strncmp(path, blocked_prefixes[i], strlen(blocked_prefixes[i])) == 0) {
            errno = EPERM;
            return (-1);
        }
    }
    
    /* Resolve to absolute path */
    realpath_buf = realpath(path, NULL);
    if (realpath_buf == NULL) {
        /* File might not exist yet, that's OK for new images */
        if (errno != ENOENT)
            return (-1);
        
        /* Verify parent directory exists and is writable */
        char *path_copy = strdup(path);
        if (path_copy == NULL) {
            errno = ENOMEM;
            return (-1);
        }
        
        char *dir = dirname(path_copy);
        if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
            free(path_copy);
            errno = ENOENT;
            return (-1);
        }
        free(path_copy);
        
        return (0);
    }
    
    /* Check resolved path doesn't point to blocked location */
    for (i = 0; blocked_prefixes[i] != NULL; i++) {
        if (strncmp(realpath_buf, blocked_prefixes[i], strlen(blocked_prefixes[i])) == 0) {
            free(realpath_buf);
            errno = EPERM;
            return (-1);
        }
    }
    
    /* Verify it's a regular file */
    if (stat(realpath_buf, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            free(realpath_buf);
            errno = EINVAL;
            return (-1);
        }
    }
    
    free(realpath_buf);
    return (0);
}

/*
 * Check I/O bounds
 * Returns 0 if access is within bounds, -1 if out of bounds
 */
int
emu_blk_check_bounds(struct emu_blk *blk, uint64_t sector, uint32_t count)
{
    uint64_t end_sector;
    
    if (blk == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Check for overflow */
    if (sector + count < sector) {
        errno = EOVERFLOW;
        return (-1);
    }
    
    end_sector = sector + count;
    
    /* Check against device capacity */
    if (sector >= blk->b_sectors || end_sector > blk->b_sectors) {
        errno = EIO;
        return (-1);
    }
    
    return (0);
}

/*
 * Initialize block device structure
 */
int
emu_blk_init(struct emu_blk *blk, const char *image_path, 
    bool readonly, uint32_t blk_size)
{
    struct stat sb;
    
    if (blk == NULL || image_path == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    memset(blk, 0, sizeof(*blk));
    pthread_mutex_init(&blk->b_vq_lock, NULL);
    
    /* Validate path */
    if (emu_blk_validate_path(image_path) != 0)
        return (-1);
    
    /* Copy path */
    blk->b_image_path = strdup(image_path);
    if (blk->b_image_path == NULL) {
        errno = ENOMEM;
        return (-1);
    }
    
    blk->b_readonly = readonly;
    blk->b_blk_size = blk_size > 0 ? blk_size : EMU_BLK_SECTOR_SIZE;
    blk->b_type = EMU_BLK_TYPE_DISK;
    blk->b_image_fd = -1;
    
    /* Generate serial number */
    snprintf(blk->b_serial, sizeof(blk->b_serial), "EMU%015lu", 
        (unsigned long)time(NULL) % 100000000000000UL);
    
    /* Get image size if it exists */
    if (stat(image_path, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            errno = EINVAL;
            return (-1);
        }
        
        blk->b_capacity = sb.st_size;
        blk->b_sectors = blk->b_capacity / blk->b_blk_size;
    }
    
    return (0);
}

/*
 * Open block device image file
 */
int
emu_blk_open(struct emu_blk *blk)
{
    int flags;
    
    if (blk == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (blk->b_image_fd >= 0) {
        errno = EBUSY;
        return (-1);
    }
    
    /* Open with appropriate flags */
    flags = blk->b_readonly ? O_RDONLY : O_RDWR;
    
    blk->b_image_fd = open(blk->b_image_path, flags);
    if (blk->b_image_fd < 0)
        return (-1);
    
    /* Re-verify file type after opening (TOCTOU protection) */
    struct stat sb;
    if (fstat(blk->b_image_fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        close(blk->b_image_fd);
        blk->b_image_fd = -1;
        errno = EINVAL;
        return (-1);
    }
    
    /* Update capacity */
    blk->b_capacity = sb.st_size;
    blk->b_sectors = blk->b_capacity / blk->b_blk_size;
    
    return (0);
}

/*
 * Close block device image file
 */
void
emu_blk_close(struct emu_blk *blk)
{
    if (blk == NULL)
        return;
    
    if (blk->b_image_fd >= 0) {
        fsync(blk->b_image_fd);
        close(blk->b_image_fd);
        blk->b_image_fd = -1;
    }
}

/*
 * Destroy block device and free resources
 */
void
emu_blk_destroy(struct emu_blk *blk)
{
    if (blk == NULL)
        return;
    
    emu_blk_close(blk);
    
    if (blk->b_image_path != NULL) {
        free(blk->b_image_path);
        blk->b_image_path = NULL;
    }
    
    pthread_mutex_destroy(&blk->b_vq_lock);
}

/*
 * Handle virtio block request
 * This is a simplified implementation - full virtio would need
 * proper virtqueue handling
 */
int
emu_blk_handle_request(struct emu_blk *blk, void *req)
{
    /* Simplified request handling - would need full virtio implementation */
    if (blk == NULL || req == NULL) {
        errno = EINVAL;
        return (EMU_BLK_STATUS_IOERR);
    }
    
    pthread_mutex_lock(&blk->b_vq_lock);
    
    if (blk->b_image_fd < 0) {
        pthread_mutex_unlock(&blk->b_vq_lock);
        return (EMU_BLK_STATUS_IOERR);
    }
    
    /* Request handling would go here */
    /* For now, just return unsupported */
    
    pthread_mutex_unlock(&blk->b_vq_lock);
    return (EMU_BLK_STATUS_UNSUPP);
}

/*
 * Get block device configuration
 */
int
emu_blk_get_config(struct emu_blk *blk, void *config)
{
    if (blk == NULL || config == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Return capacity in 512-byte sectors */
    uint64_t *capacity = (uint64_t *)config;
    *capacity = blk->b_sectors;
    
    return (0);
}

/*
 * Check if device is ready
 */
bool
emu_blk_is_ready(struct emu_blk *blk)
{
    if (blk == NULL)
        return (false);
    
    return (blk->b_image_fd >= 0 && blk->b_sectors > 0);
}

/*
 * Get device capacity in bytes
 */
uint64_t
emu_blk_get_capacity(struct emu_blk *blk)
{
    if (blk == NULL)
        return (0);
    
    return (blk->b_capacity);
}

/*
 * Check if device is read-only
 */
bool
emu_blk_is_readonly(struct emu_blk *blk)
{
    if (blk == NULL)
        return (true);
    
    return (blk->b_readonly);
}
