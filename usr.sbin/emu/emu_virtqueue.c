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
 *    notice, this list of this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_virtqueue.h"

/*
 * Simple Virtqueue Implementation for Userspace Emulation
 *
 * This provides a simplified virtqueue interface that can be used by
 * userspace emulation tools. The actual memory mapping would be handled
 * by the VM management layer (e.g., bhyve integration).
 */

/*
 * Initialize a virtqueue
 */
int
emu_vq_init(struct emu_virtqueue *vq, uint16_t size,
    void (*notify)(void *, struct emu_virtqueue *), void *notify_arg)
{
	size_t desc_size, avail_size, used_size, total_size;
	void *base;

	if (vq == NULL)
		return (EINVAL);

	/* Size must be power of 2 */
	if (size == 0 || (size & (size - 1)) != 0)
		return (EINVAL);

	vq->vq_size = size;
	vq->vq_mask = size - 1;
	vq->vq_notify = notify;
	vq->vq_notify_arg = notify_arg;
	vq->vq_last_avail_idx = 0;
	vq->vq_avail_idx = 0;
	vq->vq_useguest_notification = true;

	/* Calculate memory layout:
	 * - Descriptor table (size * sizeof(struct emu_vq_desc))
	 * - Available ring (6 bytes + size * 2 bytes + 2 bytes)
	 * - Used ring (6 bytes + size * 6 bytes + 2 bytes)
	 * All aligned to page boundaries
	 */
	desc_size = size * sizeof(struct emu_vq_desc);
	avail_size = sizeof(uint16_t) * 2 + sizeof(uint16_t) * size +
	    sizeof(uint16_t);
	used_size = sizeof(uint16_t) * 2 +
	    size * sizeof(struct emu_vq_used_elem) + sizeof(uint16_t);

	/* Round up to page size */
	total_size = desc_size + avail_size + used_size;
	total_size = (total_size + PAGE_MASK) & ~PAGE_MASK;

	/* Allocate contiguous memory for virtqueue */
	base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
	    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (base == MAP_FAILED)
		return (ENOMEM);

	/* Set up pointers */
	vq->vq_desc = (struct emu_vq_desc *)base;
	vq->vq_avail = (struct emu_vq_avail *)
	    ((char *)base + desc_size);
	vq->vq_used = (struct emu_vq_used *)
	    ((char *)base + desc_size + avail_size);

	/* Initialize available ring */
	vq->vq_avail->flags = 0;
	vq->vq_avail->idx = 0;
	vq->vq_avail->used_event = 0;

	/* Initialize used ring */
	vq->vq_used->flags = 0;
	vq->vq_used->idx = 0;
	vq->vq_used->avail_event = 0;

	pthread_mutex_init(&vq->vq_lock, NULL);

	return (0);
}

/*
 * Destroy a virtqueue
 */
void
emu_vq_destroy(struct emu_virtqueue *vq)
{
	size_t desc_size, avail_size, total_size;

	if (vq == NULL)
		return;

	desc_size = vq->vq_size * sizeof(struct emu_vq_desc);
	avail_size = sizeof(uint16_t) * 2 + sizeof(uint16_t) * vq->vq_size +
	    sizeof(uint16_t);
	total_size = desc_size + avail_size +
	    vq->vq_size * sizeof(struct emu_vq_used_elem) +
	    sizeof(uint16_t);
	total_size = (total_size + PAGE_MASK) & ~PAGE_MASK;

	pthread_mutex_destroy(&vq->vq_lock);

	munmap(vq->vq_desc, total_size);

	memset(vq, 0, sizeof(*vq));
}

/*
 * Get the next available descriptor chain for processing
 *
 * Returns 0 on success with desc_idx and array of descriptor indices
 * in the chain. Returns ENOENT if no descriptors available.
 */
int
emu_vq_get_chain(struct emu_virtqueue *vq, uint16_t *desc_idx,
    uint16_t descs[], uint16_t ndescs)
{
	uint16_t avail_idx, i;
	int count = 0;

	if (vq == NULL || desc_idx == NULL || descs == NULL)
		return (EINVAL);

	pthread_mutex_lock(&vq->vq_lock);

	/* Check if there are available descriptors */
	avail_idx = vq->vq_avail->idx;
	if (vq->vq_last_avail_idx == avail_idx) {
		pthread_mutex_unlock(&vq->vq_lock);
		return (ENOENT);
	}

	/* Get the first descriptor index */
	*desc_idx = vq->vq_avail->ring[vq->vq_last_avail_idx & vq->vq_mask];
	vq->vq_last_avail_idx++;

	/* Build the descriptor chain */
	descs[count++] = *desc_idx;
	i = *desc_idx;

	/* Follow chained descriptors */
	while (count < ndescs &&
	    (vq->vq_desc[i].vqd_flags & EMU_VQ_DESC_F_NEXT) != 0) {
		i = vq->vq_desc[i].vqd_next;
		descs[count++] = i;

		/* Check for infinite loop */
		if (count >= EMU_VQ_MAX_CHAIN)
			break;
	}

	pthread_mutex_unlock(&vq->vq_lock);

	return (0);
}

/*
 * Get buffer information for a descriptor
 */
int
emu_vq_get_desc(struct emu_virtqueue *vq, uint16_t desc_idx,
    uint64_t *addr, uint32_t *len, int *is_write)
{
	if (vq == NULL || addr == NULL || len == NULL || is_write == NULL)
		return (EINVAL);

	if (desc_idx >= vq->vq_size)
		return (EINVAL);

	pthread_mutex_lock(&vq->vq_lock);

	*addr = vq->vq_desc[desc_idx].vqd_addr;
	*len = vq->vq_desc[desc_idx].vqd_len;
	*is_write = (vq->vq_desc[desc_idx].vqd_flags & EMU_VQ_DESC_F_WRITE) != 0;

	pthread_mutex_unlock(&vq->vq_lock);

	return (0);
}

/*
 * Return a completed descriptor chain to the used ring
 */
void
emu_vq_return(struct emu_virtqueue *vq, uint16_t desc_idx, uint32_t len)
{
	uint16_t used_idx;

	if (vq == NULL)
		return;

	pthread_mutex_lock(&vq->vq_lock);

	used_idx = vq->vq_used->idx & vq->vq_mask;
	vq->vq_used->ring[used_idx].id = desc_idx;
	vq->vq_used->ring[used_idx].len = len;
	vq->vq_used->idx++;

	pthread_mutex_unlock(&vq->vq_lock);
}

/*
 * Check if there are available descriptors
 */
bool
emu_vq_has_descs(struct emu_virtqueue *vq)
{
	if (vq == NULL)
		return (false);

	return (vq->vq_last_avail_idx != vq->vq_avail->idx);
}

/*
 * Notify guest of available descriptors
 *
 * In a real implementation, this would trigger a VM exit or
 * interrupt to notify the guest that descriptors are available.
 */
void
emu_vq_notify_guest(struct emu_virtqueue *vq)
{
	if (vq == NULL || !vq->vq_useguest_notification)
		return;

	/* In userspace emulation, this is a no-op.
	 * In bhyve integration, this would trigger a VM exit or
	 * interrupt delivery to the guest.
	 */
}

/*
 * Read from guest buffer (simplified - actual implementation
 * would need proper memory mapping/translation)
 */
int
emu_vq_read_buf(struct emu_virtqueue *vq, uint16_t desc_idx,
    void *buf, size_t buflen)
{
	uint64_t addr;
	uint32_t len;
	int is_write;

	if (vq == NULL || buf == NULL)
		return (EINVAL);

	/* Get descriptor info */
	if (emu_vq_get_desc(vq, desc_idx, &addr, &len, &is_write) != 0)
		return (EINVAL);

	/* Can only read from writeable buffers */
	if (!is_write)
		return (EACCES);

	/* Don't read more than available */
	if (buflen > len)
		buflen = len;

	/* In userspace emulation without actual VM,
	 * we simulate by using a local buffer.
	 * Real implementation would use addr for VM memory.
	 */
	memset(buf, 0, buflen);

	return (0);
}

/*
 * Write to guest buffer (simplified - actual implementation
 * would need proper memory mapping/translation)
 */
int
emu_vq_write_buf(struct emu_virtqueue *vq, uint16_t desc_idx,
    const void *buf, size_t buflen)
{
	uint64_t addr;
	uint32_t len;
	int is_write;

	if (vq == NULL || buf == NULL)
		return (EINVAL);

	/* Get descriptor info */
	if (emu_vq_get_desc(vq, desc_idx, &addr, &len, &is_write) != 0)
		return (EINVAL);

	/* Can only write to readable buffers */
	if (is_write)
		return (EACCES);

	/* Don't write more than available */
	if (buflen > len)
		buflen = len;

	/* In userspace emulation without actual VM,
	 * we simulate by using a local buffer.
	 * Real implementation would use addr for VM memory.
	 */
	/* Actual write would go to guest memory */

	return (0);
}
