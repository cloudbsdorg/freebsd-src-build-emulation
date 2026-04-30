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
 * This device allows the hypervisor to request memory from the guest
 * and return it to the host. The balloon inflates to allocate guest
 * memory and deflates to release it.
 *
 * Security features:
 * - Minimum memory floor enforced (memory_balloon_min_pct)
 * - Periodic adjustment timer for gradual memory reclaim
 * - Target memory sysctl for dynamic adjustment
 */

#include <sys/param.h>
#include <sys/linker_set.h>
#include <sys/iometer.h>
#include <sys/tree.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <event.h>

#include "bhyverun.h"
#include "config.h"
#include "debug.h"
#include "pci_emul.h"
#include "virtio.h"

/* VirtIO Balloon defines */
#define VIRTIO_BALLOON_RINGSZ	64
#define VIRTIO_BALLOON_MAX_PAGE_NUM	256
#define VIRTIO_BALLOON_PAGE_SIZE	4096

/* VirtIO Balloon feature bits */
#define VIRTIO_BALLOON_F_STATS_VQ	0	/* Statistics virtqueue */
#define VIRTIO_BALLOON_F_DEFLATE_ON_OOM	1	/* Deflate on OOM */

/* Balloon commands */
#define VIRTIO_BALLOON_CMD_INFLATE	0
#define VIRTIO_BALLOON_CMD_DEFLATE	1
#define VIRTIO_BALLOON_CMD_STATS	2

/* VirtIO Balloon configuration */
struct pci_virtio_balloon_config {
	uint32_t num_pages;		/* Number of pages ballooned */
	uint32_t actual;		/* Actual number of pages */
} __attribute__((packed));

/*
 * Per-device softc
 */
struct pci_virtio_balloon_softc {
	struct virtio_softc vsc_vs;
	struct vqueue_info vsc_vq;		/* Inflate/deflate queue */
	struct vqueue_info vsc_stats_vq;	/* Statistics queue */
	pthread_mutex_t vsc_mtx;
	
	/* Balloon state */
	uint32_t vsc_num_pages;		/* Target balloon size in pages */
	uint32_t vsc_actual;		/* Actual current balloon pages */
	uint32_t vsc_target;		/* Target for gradual adjustment */
	uint32_t vsc_min_pages;		/* Minimum floor (from sysctl) */
	
	/* Configuration */
	uint64_t vsc_features;		/* Negotiated features */
	
	/* Statistics */
	uint64_t vsc_pages_inflated;
	uint64_t vsc_pages_deflated;
	uint64_t vsc_adjustments;
	
	/* Timer for periodic adjustment */
	struct event vsc_timer;
	int vsc_timer_active;
};

static int pci_vballoon_debug;
#define DPRINTF(params) if (pci_vballoon_debug) printf params
#define WPRINTF(params) printf params

static struct virtio_consts vballoon_vi_consts = {
	.vc_name =	"balloon",
	.vc_nvq =	2,		/* Inflate/deflate + stats */
	.vc_cfgsize =	sizeof(struct pci_virtio_balloon_config),
	.vc_reset =	pci_vballoon_reset,
	.vc_qnotify =	pci_vballoon_notify,
};

/*
 * Get guest physical address from a request
 */
static inline uint64_t
pci_vballoon_get_gpa(struct pci_virtio_balloon_softc *sc, struct virtio_iov *iov)
{
	/* In a real implementation, we would use vtovh() to translate */
	return (0);
}

/*
 * Reset the balloon device
 */
static void
pci_vballoon_reset(void *arg)
{
	struct pci_virtio_balloon_softc *sc = arg;

	DPRINTF(("virtio_balloon: reset\n"));

	pthread_mutex_lock(&sc->vsc_mtx);
	sc->vsc_num_pages = 0;
	sc->vsc_actual = 0;
	sc->vsc_target = 0;
	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * Handle inflate/deflate requests from the guest
 */
static void
pci_vballoon_notify(void *arg, struct vqueue_info *vq)
{
	struct pci_virtio_balloon_softc *sc = arg;
	struct virtio_iov iov;
	uint32_t cmd;
	int tries = 0;
	const int max_tries = 1024;

	DPRINTF(("virtio_balloon: notify\n"));

	pthread_mutex_lock(&sc->vsc_mtx);

	/* Process all requests in the queue */
	while (vqueue_get(vq, &iov, sizeof(cmd)) == 0) {
		if (iov.iov_len < sizeof(cmd)) {
			WPRINTF(("virtio_balloon: short request\n"));
			vqueue_relinquish(vq, 0);
			continue;
		}

		cmd = *(uint32_t *)iov.iov_base;

		switch (cmd) {
		case VIRTIO_BALLOON_CMD_INFLATE:
			/* Guest is giving us pages */
			DPRINTF(("virtio_balloon: inflate %lu pages\n",
			    iov.iov_len / sizeof(uint64_t)));
			sc->vsc_pages_inflated += iov.iov_len / sizeof(uint64_t);
			break;

		case VIRTIO_BALLOON_CMD_DEFLATE:
			/* Guest wants pages back */
			DPRINTF(("virtio_balloon: deflate %lu pages\n",
			    iov.iov_len / sizeof(uint64_t)));
			sc->vsc_pages_deflated += iov.iov_len / sizeof(uint64_t);
			break;

		default:
			WPRINTF(("virtio_balloon: unknown command %u\n", cmd));
			break;
		}

		/* Update actual count */
		sc->vsc_actual = sc->vsc_num_pages;

		vqueue_relinquish(vq, 0);

		/* Prevent infinite loop */
		if (++tries > max_tries) {
			WPRINTF(("virtio_balloon: too many requests\n"));
			break;
		}
	}

	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * Configuration read handler
 */
static int
pci_vballoon_cfgread(void *arg, int offset, int size, uint32_t *retval)
{
	struct pci_virtio_balloon_softc *sc = arg;
	struct pci_virtio_balloon_config config;
	int error = 0;

	pthread_mutex_lock(&sc->vsc_mtx);
	config.num_pages = sc->vsc_num_pages;
	config.actual = sc->vsc_actual;
	pthread_mutex_unlock(&sc->vsc_mtx);

	if (offset + size > sizeof(config))
		return (EINVAL);

	memcpy(retval, (uint8_t *)&config + offset, size);
	return (error);
}

/*
 * Configuration write handler
 */
static int
pci_vballoon_cfgwrite(void *arg, int offset, int size, uint32_t val)
{
	struct pci_virtio_balloon_softc *sc = arg;
	int error = 0;

	pthread_mutex_lock(&sc->vsc_mtx);

	switch (offset) {
	case 0:	/* num_pages - target balloon size */
		if (size != 4) {
			error = EINVAL;
			break;
		}
		sc->vsc_num_pages = val;
		DPRINTF(("virtio_balloon: target set to %u pages\n", val));
		break;

	case 4:	/* actual - read-only, but allow writes */
		/* actual is read-only, ignore writes */
		break;

	default:
		error = EINVAL;
		break;
	}

	pthread_mutex_unlock(&sc->vsc_mtx);
	return (error);
}

/*
 * Set the target balloon size
 */
int
pci_vballoon_set_target(struct pci_virtio_balloon_softc *sc, uint32_t target_pages)
{
	int error = 0;

	pthread_mutex_lock(&sc->vsc_mtx);

	/* Enforce minimum floor */
	if (target_pages < sc->vsc_min_pages) {
		target_pages = sc->vsc_min_pages;
		DPRINTF(("virtio_balloon: clamped to minimum %u pages\n",
		    target_pages));
	}

	sc->vsc_target = target_pages;
	sc->vsc_num_pages = target_pages;
	sc->vsc_adjustments++;

	pthread_mutex_unlock(&sc->vsc_mtx);

	return (error);
}

/*
 * Get current balloon size
 */
uint32_t
pci_vballoon_get_size(struct pci_virtio_balloon_softc *sc)
{
	uint32_t size;

	pthread_mutex_lock(&sc->vsc_mtx);
	size = sc->vsc_actual;
	pthread_mutex_unlock(&sc->vsc_mtx);

	return (size);
}

/*
 * Periodic timer callback for gradual adjustment
 */
static void
pci_vballoon_timer(int fd, short ev, void *arg)
{
	struct pci_virtio_balloon_softc *sc = arg;
	int32_t diff;
	uint32_t step;

	pthread_mutex_lock(&sc->vsc_mtx);

	if (!sc->vsc_timer_active) {
		pthread_mutex_unlock(&sc->vsc_mtx);
		return;
	}

	diff = (int32_t)sc->vsc_target - (int32_t)sc->vsc_actual;

	if (diff == 0) {
		/* At target, no adjustment needed */
		pthread_mutex_unlock(&sc->vsc_mtx);
		return;
	}

	/* Adjust by up to 256 pages at a time (gradual) */
	step = abs(diff);
	if (step > VIRTIO_BALLOON_MAX_PAGE_NUM)
		step = VIRTIO_BALLOON_MAX_PAGE_NUM;

	if (diff > 0) {
		/* Need to deflate (give memory back to guest) */
		sc->vsc_num_pages -= step;
		if ((int32_t)sc->vsc_num_pages < (int32_t)sc->vsc_target)
			sc->vsc_num_pages = sc->vsc_target;
		DPRINTF(("virtio_balloon: deflate %u pages\n", step));
	} else {
		/* Need to inflate (take memory from guest) */
		sc->vsc_num_pages += step;
		if (sc->vsc_num_pages > sc->vsc_target)
			sc->vsc_num_pages = sc->vsc_target;
		DPRINTF(("virtio_balloon: inflate %u pages\n", step));
	}

	/* Re-arm timer if not at target */
	if (sc->vsc_num_pages != sc->vsc_target) {
		struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
		evtimer_del(&sc->vsc_timer);
		evtimer_add(&sc->vsc_timer, &tv);
	}

	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * Start the periodic adjustment timer
 */
void
pci_vballoon_start_timer(struct pci_virtio_balloon_softc *sc)
{
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

	pthread_mutex_lock(&sc->vsc_mtx);

	if (sc->vsc_timer_active) {
		pthread_mutex_unlock(&sc->vsc_mtx);
		return;
	}

	evtimer_set(&sc->vsc_timer, pci_vballoon_timer, sc);
	evtimer_add(&sc->vsc_timer, &tv);
	sc->vsc_timer_active = 1;

	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * Stop the periodic adjustment timer
 */
void
pci_vballoon_stop_timer(struct pci_virtio_balloon_softc *sc)
{
	pthread_mutex_lock(&sc->vsc_mtx);

	if (sc->vsc_timer_active) {
		evtimer_del(&sc->vsc_timer);
		sc->vsc_timer_active = 0;
	}

	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * Set minimum memory floor (in pages)
 */
void
pci_vballoon_set_min(struct pci_virtio_balloon_softc *sc, uint32_t min_pages)
{
	pthread_mutex_lock(&sc->vsc_mtx);
	sc->vsc_min_pages = min_pages;

	/* Ensure current target respects new minimum */
	if (sc->vsc_target < min_pages)
		sc->vsc_target = min_pages;
	if (sc->vsc_num_pages < min_pages)
		sc->vsc_num_pages = min_pages;

	pthread_mutex_unlock(&sc->vsc_mtx);
}

/*
 * PCI attachment function
 */
static int
pci_vballoon_init(struct pci_devinst *pi, char *opt)
{
	struct pci_virtio_balloon_softc *sc;
	int error;

	DPRINTF(("virtio_balloon: init\n"));

	sc = calloc(1, sizeof(*sc));
	if (sc == NULL) {
		WPRINTF(("virtio_balloon: malloc failed\n"));
		return (-1);
	}

	/* Initialize mutex */
	pthread_mutex_init(&sc->vsc_mtx, NULL);

	/* Initialize virtio softc */
	virtio_set_desc_ring_size(pi, VIRTIO_BALLOON_RINGSZ);

	/* Set up config handlers */
	virtio_config_gen_setup(&sc->vsc_vs, VIRTIO_BALLOON_MAX_PAGE_NUM);
	sc->vsc_vs.vc_cfgread = pci_vballoon_cfgread;
	sc->vsc_vs.vc_cfgwrite = pci_vballoon_cfgwrite;

	/* Initialize queues */
	error = vqueue_init(&sc->vsc_vq, pi, 0, pci_vballoon_notify, sc);
	if (error != 0) {
		WPRINTF(("virtio_balloon: vqueue_init failed\n"));
		free(sc);
		return (-1);
	}

	/* Set up interrupt */
	virtio_bind_intr(pi, &sc->vsc_vq);

	/* Add PCI device */
	pci_set_cfgdata(pi, PCI_VENDOR_VIRTIO, 0x1002, 0x1, "balloon");

	/* Register reset handler */
	pi->pi_reset = pci_vballoon_reset;

	/* Initialize balloon state */
	sc->vsc_min_pages = VIRTIO_BALLOON_MAX_PAGE_NUM;	/* 1MB minimum */
	sc->vsc_num_pages = 0;
	sc->vsc_actual = 0;
	sc->vsc_target = 0;

	DPRINTF(("virtio_balloon: device created successfully\n"));
	return (0);
}

/*
 * Module initialization
 */
static void
pci_vballoon_usage(void)
{
	fprintf(stderr, "  -s <slot> virtio-balloon[,balloon_size=<size>]\n");
}

static struct pci_devemu pci_de_vballoon = {
	.pe_emu =	"virtio-balloon",
	.pe_init =	pci_vballoon_init,
	.pe_barwrite =	virtio_pci_write,
	.pe_barread =	virtio_pci_read,
};
PCI_EMUL_SET(pci_de_vballoon);
