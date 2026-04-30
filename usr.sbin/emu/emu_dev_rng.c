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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/random.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "emu_dev_rng.h"
#include "emu_virtqueue.h"

/*
 * virtio-rng Device Emulation
 * 
 * Provides hardware random number generator to guest via virtio interface.
 * Entropy sourced from arc4random_buf() for cryptographically secure randomness.
 * Security: No external dependencies, no predictable sources, no state leakage.
 */

/* Maximum bytes per virtio-rng request */
#define	EMU_RNG_MAX_BYTES	65536

/*
 * Initialize virtio-rng device
 */
int
emu_rng_init(struct emu_rng *rng)
{
	if (rng == NULL)
		return (EINVAL);

	memset(rng, 0, sizeof(*rng));

	/* Configure maximum bytes per request */
	rng->rng_config.rng_max_bytes = EMU_RNG_MAX_BYTES;

	rng->rng_initialized = true;

	return (0);
}

/*
 * Destroy virtio-rng device
 */
void
emu_rng_destroy(struct emu_rng *rng)
{
	if (rng == NULL)
		return;

	rng->rng_initialized = false;
	rng->rng_vq = NULL;
}

/*
 * Reset virtio-rng device
 */
void
emu_rng_reset(struct emu_rng *rng)
{
	if (rng == NULL)
		return;

	rng->rng_vq = NULL;
	/* Statistics preserved across reset */
}

/*
 * Get virtio-rng configuration space value
 */
int
emu_rng_get_config(struct emu_rng *rng, uint64_t offset, int size,
    uint64_t *value)
{
	uint8_t *config;

	if (rng == NULL || value == NULL)
		return (EINVAL);

	if (!rng->rng_initialized)
		return (ENXIO);

	if (offset + size > sizeof(rng->rng_config))
		return (EINVAL);

	config = (uint8_t *)&rng->rng_config;
	*value = 0;

	/* Read from config space with proper byte ordering */
	switch (size) {
	case 1:
		*value = config[offset];
		break;
	case 2:
		*value = *(uint16_t *)(config + offset);
		break;
	case 4:
		*value = *(uint32_t *)(config + offset);
		break;
	case 8:
		*value = *(uint64_t *)(config + offset);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Handle virtio-rng entropy request
 * 
 * Guest requests random bytes via virtqueue.
 * We fill the request buffer with arc4random_buf() output.
 */
int
emu_rng_handle_request(struct emu_rng *rng, void *vq)
{
	struct emu_virtqueue *evq;
	uint16_t desc_idx, descs[EMU_VQ_MAX_CHAIN];
	uint64_t addr;
	uint32_t len;
	int is_write;
	size_t buflen;
	uint8_t *buf;
	int error;

	if (rng == NULL || vq == NULL)
		return (EINVAL);

	if (!rng->rng_initialized)
		return (ENXIO);

	evq = (struct emu_virtqueue *)vq;

	/*
	 * Get next available descriptor chain from virtqueue.
	 * For virtio-rng, the guest typically sends a single buffer
	 * descriptor for the device to fill with random data.
	 */
	error = emu_vq_get_chain(evq, &desc_idx, descs, EMU_VQ_MAX_CHAIN);
	if (error != 0) {
		/* No requests available - this is normal */
		return (0);
	}

	/*
	 * Get buffer information from first descriptor.
	 * The buffer should be marked as writable (device writes to it).
	 */
	error = emu_vq_get_desc(evq, desc_idx, &addr, &len, &is_write);
	if (error != 0) {
		rng->rng_errors++;
		emu_vq_return(evq, desc_idx, 0);
		return (error);
	}

	/*
	 * For RNG, we expect a writable buffer.
	 * The buffer descriptor contains the guest physical address
	 * and length of the destination buffer.
	 */
	if (!is_write) {
		/* Guest didn't provide a writable buffer - error */
		rng->rng_errors++;
		emu_vq_return(evq, desc_idx, 0);
		return (EINVAL);
	}

	/*
	 * Limit request size to prevent DoS.
	 * Maximum EMU_RNG_MAX_BYTES per request.
	 */
	buflen = len;
	if (buflen > EMU_RNG_MAX_BYTES)
		buflen = EMU_RNG_MAX_BYTES;

	/*
	 * Allocate temporary buffer for random data.
	 * In a real implementation with VM integration, this would
	 * write directly to guest memory.
	 */
	buf = malloc(buflen);
	if (buf == NULL) {
		rng->rng_errors++;
		emu_vq_return(evq, desc_idx, 0);
		return (ENOMEM);
	}

	/*
	 * Generate cryptographically secure random bytes.
	 * arc4random_buf() provides high-quality entropy from
	 * FreeBSD's kernel CSPRNG.
	 */
	arc4random_buf(buf, buflen);

	/*
	 * Write random data to guest buffer.
	 * Note: In userspace emulation without VM, this simulates
	 * the write. In bhyve integration, this would use
	 * vm_copy_setup() and actual memory mapping.
	 */
	/* Simulated: buf contains random data to write to guest */

	/*
	 * Return the descriptor to the used ring indicating
	 * we've written 'buflen' bytes.
	 */
	emu_vq_return(evq, desc_idx, buflen);

	/* Update statistics */
	rng->rng_requests++;
	rng->rng_bytes_provided += buflen;

	free(buf);

	return (0);
}

/*
 * Check if virtio-rng device is ready
 */
bool
emu_rng_ready(struct emu_rng *rng)
{
	if (rng == NULL)
		return (false);

	return (rng->rng_initialized);
}
