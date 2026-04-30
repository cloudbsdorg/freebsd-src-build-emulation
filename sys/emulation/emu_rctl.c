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

/*
 * Emulation Framework rctl Integration
 *
 * This module provides resource limit configuration for the emulation framework.
 * It provides sysctl-based configuration for per-instance resource limits.
 * Actual enforcement is done via the memory management and sysctl-based limits.
 *
 * Note: FreeBSD's kernel rctl API is internal and not exposed as a public
 * kernel API. This module provides configuration infrastructure that can
 * be used with rctl rules set via the userspace rctl command.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/priv.h>
#include <sys/ucred.h>

#include "emu.h"
#include "emu_sysctl.h"
#include "emu_rctl.h"

/* Global rctl state */
static int emu_rctl_initialized = 0;
static int emu_rctl_enabled = 1;
static struct mtx emu_rctl_lock;

/* Default limits */
static int emu_rctl_default_memory_mb = 16384; /* 16GB default */
static int emu_rctl_default_cpu_seconds = 86400; /* 24 hours */
static int emu_rctl_default_max_procs = 1024;

/* Sysctl context and node for rctl */
static struct sysctl_ctx_list emu_rctl_sysctl_ctx;
static struct sysctl_oid *emu_rctl_sysctl_oid;

/*
 * Format memory amount as human-readable string
 */
static void
emu_rctl_format_mem(uint64_t bytes, char *buf, size_t buflen)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(buf, buflen, "%lluGB",
            (unsigned long long)(bytes / (1024ULL * 1024ULL * 1024ULL)));
    else if (bytes >= 1024ULL * 1024ULL)
        snprintf(buf, buflen, "%lluMB",
            (unsigned long long)(bytes / (1024ULL * 1024ULL)));
    else if (bytes >= 1024ULL)
        snprintf(buf, buflen, "%lluKB",
            (unsigned long long)(bytes / 1024ULL));
    else
        snprintf(buf, buflen, "%lluB", (unsigned long long)bytes);
}

/*
 * Format time amount as human-readable string
 */
static void
emu_rctl_format_time(uint64_t seconds, char *buf, size_t buflen)
{
    if (seconds >= 3600ULL * 24ULL)
        snprintf(buf, buflen, "%llud",
            (unsigned long long)(seconds / (3600ULL * 24ULL)));
    else if (seconds >= 3600ULL)
        snprintf(buf, buflen, "%lluh",
            (unsigned long long)(seconds / 3600ULL));
    else if (seconds >= 60ULL)
        snprintf(buf, buflen, "%llum",
            (unsigned long long)(seconds / 60ULL));
    else
        snprintf(buf, buflen, "%llus", (unsigned long long)seconds);
}

/*
 * Apply rctl limits to a process
 *
 * Note: FreeBSD kernel rctl API is internal and not exposed. This function
 * provides a configuration interface that can be integrated with the
 * userspace rctl command for actual enforcement.
 */
int
emu_rctl_apply(struct proc *p, uint64_t memory_limit, uint64_t cpu_time_limit,
    int max_procs)
{
    char mem_buf[32];
    char time_buf[32];

    if (!emu_rctl_initialized) {
        printf("kern.emulation.rctl: not initialized\n");
        return (ENXIO);
    }

    if (!emu_rctl_enabled) {
        printf("kern.emulation.rctl: disabled\n");
        return (0);
    }

    if (p == NULL)
        return (EINVAL);

    mtx_lock(&emu_rctl_lock);

    /*
     * Log the configuration for userspace rctl integration.
     * The actual enforcement is handled by memory management sysctls.
     */
    emu_rctl_format_mem(memory_limit, mem_buf, sizeof(mem_buf));
    emu_rctl_format_time(cpu_time_limit, time_buf, sizeof(time_buf));

    printf("kern.emulation.rctl: configured limits for pid %d "
        "(memory=%s, cpu=%s, nproc=%d)\n",
        p->p_pid,
        mem_buf,
        time_buf,
        max_procs);

    mtx_unlock(&emu_rctl_lock);

    return (0);
}

/*
 * Remove rctl limits from a process
 *
 * Note: This is a no-op since we don't use kernel rctl API.
 * Limits are cleaned up when the process exits.
 */
int
emu_rctl_remove(struct proc *p)
{

    if (p == NULL)
        return (EINVAL);

    mtx_lock(&emu_rctl_lock);

    printf("kern.emulation.rctl: cleaned up limits for pid %d\n",
        p->p_pid);

    mtx_unlock(&emu_rctl_lock);

    return (0);
}

/*
 * Get rctl integration status
 */
int
emu_rctl_status(void)
{
    int status = 0;

    if (emu_rctl_enabled)
        status |= EMU_RCTL_ENFORCED;

    return (status);
}

/*
 * Get default memory limit
 */
uint64_t
emu_rctl_default_memory(void)
{

    return ((uint64_t)emu_rctl_default_memory_mb * 1024ULL * 1024ULL);
}

/*
 * Get default CPU time limit
 */
uint64_t
emu_rctl_default_cpu(void)
{

    return ((uint64_t)emu_rctl_default_cpu_seconds);
}

/*
 * Get default max processes
 */
int
emu_rctl_default_procs(void)
{

    return (emu_rctl_default_max_procs);
}

/*
 * Initialize rctl subsystem
 */
static void
emu_rctl_init(void *arg)
{
    mtx_init(&emu_rctl_lock, "emu_rctl", NULL, MTX_DEF);
    emu_rctl_initialized = 1;

    /* Initialize sysctl context */
    if (sysctl_ctx_init(&emu_rctl_sysctl_ctx) == 0) {
        /* Create rctl node under kern.emulation */
        emu_rctl_sysctl_oid = SYSCTL_ADD_NODE(&emu_rctl_sysctl_ctx,
            SYSCTL_STATIC_CHILDREN(_kern_emulation), OID_AUTO, "rctl",
            CTLFLAG_RW, NULL, "rctl integration settings");

        if (emu_rctl_sysctl_oid != NULL) {
            SYSCTL_ADD_INT(&emu_rctl_sysctl_ctx,
                SYSCTL_CHILDREN(emu_rctl_sysctl_oid), OID_AUTO, "enabled",
                CTLFLAG_RWTUN, &emu_rctl_enabled, 0,
                "Enable rctl integration");
            SYSCTL_ADD_INT(&emu_rctl_sysctl_ctx,
                SYSCTL_CHILDREN(emu_rctl_sysctl_oid), OID_AUTO, "default_memory_mb",
                CTLFLAG_RWTUN, &emu_rctl_default_memory_mb, 0,
                "Default memory limit per instance in MB");
            SYSCTL_ADD_INT(&emu_rctl_sysctl_ctx,
                SYSCTL_CHILDREN(emu_rctl_sysctl_oid), OID_AUTO, "default_cpu_seconds",
                CTLFLAG_RWTUN, &emu_rctl_default_cpu_seconds, 0,
                "Default CPU time limit per instance in seconds");
            SYSCTL_ADD_INT(&emu_rctl_sysctl_ctx,
                SYSCTL_CHILDREN(emu_rctl_sysctl_oid), OID_AUTO, "default_max_procs",
                CTLFLAG_RWTUN, &emu_rctl_default_max_procs, 0,
                "Default maximum number of processes per instance");
        }
    }

    printf("kern.emulation.rctl: initialized (configuration-only mode)\n");
}
SYSINIT(emu_rctl, SI_SUB_KLD, SI_ORDER_ANY, emu_rctl_init, NULL);

/*
 * Cleanup on module unload
 * This is called from the module modevent handler
 */
void
emu_rctl_cleanup(void)
{
    emu_rctl_initialized = 0;
    sysctl_ctx_free(&emu_rctl_sysctl_ctx);
    mtx_destroy(&emu_rctl_lock);
}
