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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _EMU_DEV_VIRTQUEUE_H_
#define	_EMU_DEV_VIRTQUEUE_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/*
 * Simple Virtqueue Abstraction for Userspace Emulation
 *
 * This provides a simplified virtqueue interface for userspace emulation tools.
 * It mimics the VirtIO virtqueue structure for compatibility but operates
 * entirely in userspace for testing and development.
 *
 * VirtIO Specification References:
 * - Section 2.4: Virtqueues
 * - Section 1.4: Device Configuration Space
 */

/* Virtqueue descriptor flags */
#define EMU_VQ_DESC_F_NEXT		0x01	/* Chained descriptor */
#define EMU_VQ_DESC_F_WRITE		0x02	/* Device writes to buffer */
#define EMU_VQ_DESC_F_INDIRECT		0x04	/* Indirect buffer */

/* Virtqueue available ring flags */
#define EMU_VQ_AVAIL_F_NO_INTERRUPT	0x01	/* Don't notify on used */

/* Virtqueue used ring flags */
#define EMU_VQ_USED_F_NO_NOTIFY		0x01	/* Don't notify on avail */

/* Default virtqueue size */
#define EMU_VQ_SIZE		256

/* Maximum buffers per descriptor chain */
#define EMU_VQ_MAX_CHAIN	32

/*
 * Virtqueue descriptor entry
 * Points to a buffer - either read-only or write-only from device perspective
 */
struct emu_vq_desc {
	uint64_t	vqd_addr;	/* Guest physical address of buffer */
	uint32_t	vqd_len;	/* Length of buffer */
	uint16_t	vqd_flags;	/* Descriptor flags */
	uint16_t	vqd_next;	/* Next descriptor in chain */
};

/*
 * Virtqueue available ring entry
 * Points to a descriptor chain for the device to process
 */
struct emu_vq_avail {
	volatile uint16_t flags;
	volatile uint16_t idx;		/* Next available entry */
	uint16_t ring[EMU_VQ_SIZE];	/* Descriptor indices */
	uint16_t used_event;		/* Event suppression */
};

/*
 * Virtqueue used ring entry
 * Reports completed descriptor chains
 */
struct emu_vq_used_elem {
	uint32_t id;	/* Descriptor chain ID */
	uint32_t len;	/* Length of data written */
};

struct emu_vq_used {
	volatile uint16_t flags;
	volatile uint16_t idx;		/* Next used entry */
	struct emu_vq_used_elem ring[EMU_VQ_SIZE];
	uint16_t avail_event;		/* Event suppression */
};

/*
 * Virtqueue state structure
 */
struct emu_virtqueue {
	pthread_mutex_t vq_lock;
	
	/* Virtqueue metadata */
	uint16_t vq_size;		/* Queue size (power of 2) */
	uint16_t vq_mask;		/* vq_size - 1 for index wrapping */
	
	/* Descriptor table */
	struct emu_vq_desc *vq_desc;
	
	/* Available and used rings */
	struct emu_vq_avail *vq_avail;
	struct emu_vq_used *vq_used;
	
	/* Device notification callback */
	void (*vq_notify)(void *, struct emu_virtqueue *);
	void *vq_notify_arg;
	
	/* State tracking */
	uint16_t vq_avail_idx;		/* Our view of available index */
	uint16_t vq_last_avail_idx;	/* Last used index we processed */
	
	/* Callbacks for host-to-guest notifications */
	bool vq_useguest_notification;
};

/*
 * Virtqueue operations
 */

/* Initialize a virtqueue */
int emu_vq_init(struct emu_virtqueue *vq, uint16_t size,
    void (*notify)(void *, struct emu_virtqueue *), void *notify_arg);

/* Destroy a virtqueue */
void emu_vq_destroy(struct emu_virtqueue *vq);

/* Get the next available descriptor chain for processing */
int emu_vq_get_chain(struct emu_virtqueue *vq, uint16_t *desc_idx,
    uint16_t descs[], uint16_t ndescs);

/* Get buffer information for a descriptor */
int emu_vq_get_desc(struct emu_virtqueue *vq, uint16_t desc_idx,
    uint64_t *addr, uint32_t *len, int *is_write);

/* Return a completed descriptor chain to the used ring */
void emu_vq_return(struct emu_virtqueue *vq, uint16_t desc_idx, uint32_t len);

/* Check if there are available descriptors */
bool emu_vq_has_descs(struct emu_virtqueue *vq);

/* Notify guest of available descriptors (if applicable) */
void emu_vq_notify_guest(struct emu_virtqueue *vq);

/*
 * Simple buffer I/O for common operations
 */

/* Read from guest buffer (copy to local buffer) */
int emu_vq_read_buf(struct emu_virtqueue *vq, uint16_t desc_idx,
    void *buf, size_t buflen);

/* Write to guest buffer (copy from local buffer) */
int emu_vq_write_buf(struct emu_virtqueue *vq, uint16_t desc_idx,
    const void *buf, size_t buflen);

#endif /* !_EMU_DEV_VIRTQUEUE_H_ */
