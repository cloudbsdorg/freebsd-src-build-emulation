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

#ifndef _EMU_DEV_STORAGE_H_
#define _EMU_DEV_STORAGE_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/*
 * Virtio Block Device Emulation
 * 
 * Emulates a virtio-blk device for block storage.
 * Security: I/O only to disk image file, never to host block devices.
 * No raw device access, path traversal protection, size validation.
 */

/* Virtio block device configuration */
#define EMU_BLK_SECTOR_SIZE     512     /* Sector size in bytes */
#define EMU_BLK_MAX_SECTORS     65536   /* Max sectors per request */
#define EMU_BLK_QUEUE_SIZE      256     /* Virtqueue size */

/* Virtio block device types */
#define EMU_BLK_TYPE_DISK       0
#define EMU_BLK_TYPE_CDROM      1

/* Virtio block request types */
#define EMU_BLK_REQ_READ        0
#define EMU_BLK_REQ_WRITE       1
#define EMU_BLK_REQ_FLUSH       4
#define EMU_BLK_REQ_GET_ID      8

/* Virtio block status codes */
#define EMU_BLK_STATUS_OK       0
#define EMU_BLK_STATUS_IOERR    1
#define EMU_BLK_STATUS_UNSUPP   2

/*
 * Block device context
 */
struct emu_blk {
    /* Device configuration */
    int         b_type;                 /* Device type (disk/cdrom) */
    uint64_t    b_capacity;             /* Capacity in bytes */
    uint64_t    b_sectors;              /* Total sectors */
    uint32_t    b_blk_size;             /* Block size (usually 512) */
    
    /* Image file */
    char        *b_image_path;          /* Path to disk image */
    int         b_image_fd;             /* Image file descriptor */
    bool        b_readonly;             /* Read-only mode */
    
    /* Virtqueue */
    void        *b_vq;                  /* Virtqueue handle */
    pthread_mutex_t b_vq_lock;          /* Virtqueue lock */
    
    /* Statistics */
    uint64_t    b_read_ops;             /* Read operations */
    uint64_t    b_write_ops;            /* Write operations */
    uint64_t    b_read_bytes;           /* Bytes read */
    uint64_t    b_write_bytes;          /* Bytes written */
    uint64_t    b_errors;               /* I/O errors */
    
    /* Device identifier */
    char        b_serial[20];           /* Serial number */
};

/* Storage operations */
int     emu_blk_init(struct emu_blk *blk, const char *image_path, 
            bool readonly, uint32_t blk_size);
void    emu_blk_destroy(struct emu_blk *blk);
int     emu_blk_open(struct emu_blk *blk);
void    emu_blk_close(struct emu_blk *blk);

/* Virtio operations */
int     emu_blk_handle_request(struct emu_blk *blk, void *req);
int     emu_blk_get_config(struct emu_blk *blk, void *config);

/* Status queries */
bool    emu_blk_is_ready(struct emu_blk *blk);
uint64_t emu_blk_get_capacity(struct emu_blk *blk);
bool    emu_blk_is_readonly(struct emu_blk *blk);

/* Helper functions */
int     emu_blk_validate_path(const char *path);
int     emu_blk_check_bounds(struct emu_blk *blk, uint64_t sector, 
            uint32_t count);

#endif /* !_EMU_DEV_STORAGE_H_ */
