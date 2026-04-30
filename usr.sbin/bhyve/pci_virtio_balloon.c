/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Emulation Team
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

/*
 * VirtIO Balloon Device for Memory Management
 *
 * This is a stub implementation. Full implementation requires integration
 * with bhyve's virtqueue infrastructure.
 */

#include <sys/param.h>
#include <sys/linker_set.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bhyverun.h"
#include "pci_emul.h"
#include "virtio.h"

/* VirtIO Balloon defines */
#define VIRTIO_BALLOON_RINGSZ	64
#define VIRTIO_BALLOON_MAX_PAGE_NUM	256
#define VIRTIO_BALLOON_PAGE_SIZE	4096

/* VirtIO Balloon feature bits */
#define VIRTIO_BALLOON_F_STATS_VQ	0
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM	1

/* VirtIO Balloon configuration */
struct pci_virtio_balloon_config {
	uint32_t num_pages;
	uint32_t actual;
} __attribute__((packed));

static int pci_vballoon_debug;
#define DPRINTF(params) if (pci_vballoon_debug) printf params
#define WPRINTF(params) printf params

/*
 * VirtIO Balloon stub - full implementation requires bhyve virtqueue integration
 * This stub is a placeholder for future balloon device support
 */
static void
pci_virtio_balloon_stub(void)
{
	DPRINTF(("virtio_balloon: stub implementation\n"));
	WPRINTF(("virtio_balloon: full implementation pending\n"));
}
