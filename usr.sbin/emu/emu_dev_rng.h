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

#ifndef _EMU_DEV_RNG_H_
#define	_EMU_DEV_RNG_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * virtio-rng Device Emulation
 * 
 * Provides hardware random number generator to guest via virtio interface.
 * Entropy sourced from arc4random_buf() for cryptographically secure randomness.
 * Security: No external dependencies, no predictable sources, no state leakage.
 */

/* virtio-rng configuration space */
struct emu_rng_config {
	uint32_t	rng_max_bytes;	/* Maximum bytes per request */
};

/* virtio-rng device context */
struct emu_rng {
	struct emu_rng_config	rng_config;	/* Configuration space */
	void			*rng_vq;		/* Virtqueue reference */
	bool			rng_initialized;	/* Device initialized */
	
	/* Statistics */
	uint64_t		rng_requests;		/* Total requests handled */
	uint64_t		rng_bytes_provided;	/* Total bytes provided */
	uint64_t		rng_errors;		/* Total errors */
};

/* RNG device operations */
int	emu_rng_init(struct emu_rng *rng);
void	emu_rng_destroy(struct emu_rng *rng);
void	emu_rng_reset(struct emu_rng *rng);

/* Virtio operations */
int	emu_rng_get_config(struct emu_rng *rng, uint64_t offset, int size,
		    uint64_t *value);
int	emu_rng_handle_request(struct emu_rng *rng, void *vq);

/* Status queries */
bool	emu_rng_ready(struct emu_rng *rng);

#endif /* !_EMU_DEV_RNG_H_ */
