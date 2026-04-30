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
 * This module provides FreeBSD rctl (resource limits) integration for
 * the emulation framework. rctl provides OS-level resource enforcement
 * for processes, users, and login classes.
 *
 * Supported limits:
 * - memoryuse: RSS limit per process
 * - cputime: CPU time limit per process
 * - nproc: Process count limit per process
 * - pcpu: CPU percentage limit per process
 * - readbps/writelps: I/O rate limits
 * - filesize: Maximum file size
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/racct.h>
#include <sys/rctl.h>
#include <sys/syslog.h>
#include <sys/priv.h>
#include <sys/ucred.h>

#include "emu.h"
#include "emu_rctl.h"

/*
 * rctl rule format: subject:id:resource:action=amount
 * Subject types: pid, user, loginclass, jail
 * Actions: deny, log, sig<signal>, devctl
 */

/* Global rctl state */
static int emu_rctl_initialized = 0;
static int emu_rctl_enabled = 1;
static struct mtx emu_rctl_lock;

/* Sysctl handlers */
static SYSCTL_NODE(_kern_emulation, OID_AUTO, rctl, CTLFLAG_RW, 0,
    "rctl integration settings");

static SYSCTL_INT(_kern_emulation_rctl, OID_AUTO, enabled, CTLFLAG_RWTUN,
    &emu_rctl_enabled, 0,
    "Enable rctl integration for emulation framework");

static int emu_rctl_default_memory_mb = 16384; /* 16GB default */
SYSCTL_INT(_kern_emulation_rctl, OID_AUTO, default_memory_mb, CTLFLAG_RWTUN,
    &emu_rctl_default_memory_mb, 0,
    "Default memory limit per instance in MB");

static int emu_rctl_default_cpu_seconds = 86400; /* 24 hours */
SYSCTL_INT(_kern_emulation_rctl, OID_AUTO, default_cpu_seconds, CTLFLAG_RWTUN,
    &emu_rctl_default_cpu_seconds, 0,
    "Default CPU time limit per instance in seconds");

static int emu_rctl_default_max_procs = 1024;
SYSCTL_INT(_kern_emulation_rctl, OID_AUTO, default_max_procs, CTLFLAG_RWTUN,
    &emu_rctl_default_max_procs, 0,
    "Default maximum number of processes per instance");

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
    if (seconds >= 86400) {
        snprintf(buf, buflen, "%llud",
            (unsigned long long)(seconds / 86400));
    } else if (seconds >= 3600) {
        snprintf(buf, buflen, "%lluh",
            (unsigned long long)(seconds / 3600));
    } else if (seconds >= 60) {
        snprintf(buf, buflen, "%llum",
            (unsigned long long)(seconds / 60));
    } else {
        snprintf(buf, buflen, "%llus", (unsigned long long)seconds);
    }
}

void
emu_rctl_format_limit(int resource, uint64_t amount, char *buf, size_t buflen)
{
    switch (resource) {
    case RACCT_MEMORYUSE:
    case RACCT_RSS:
    case RACCT_MEMLOCK:
    case RACCT_VMEM:
        emu_rctl_format_mem(amount, buf, buflen);
        break;
    case RACCT_DATA:
    case RACCT_STACK:
    case RACCT_CORE:
        emu_rctl_format_mem(amount, buf, buflen);
        break;
    case RACCT_CPU:
    case RACCT_WALLCLOCK:
        emu_rctl_format_time(amount, buf, buflen);
        break;
    case RACCT_NPROC:
    case RACCT_NOFILE:
    case RACCT_NPTS:
    case RACCT_NTHR:
        snprintf(buf, buflen, "%llu", (unsigned long long)amount);
        break;
    case RACCT_SWAP:
        emu_rctl_format_mem(amount, buf, buflen);
        break;
    case RACCT_PCTCPU:
        snprintf(buf, buflen, "%llu%%", (unsigned long long)(amount / 100));
        break;
    case RACCT_READBPS:
    case RACCT_WRITEBPS:
        emu_rctl_format_mem(amount, buf, buflen);
        strlcat(buf, "/s", buflen);
        break;
    case RACCT_READIOPS:
    case RACCT_WRITEIOPS:
        snprintf(buf, buflen, "%lluops/s", (unsigned long long)amount);
        break;
    default:
        snprintf(buf, buflen, "%llu", (unsigned long long)amount);
        break;
    }
}

const char *
emu_rctl_resource_name(int resource)
{
    return (rctl_resource_name(resource));
}

/*
 * Apply rctl limits to a process
 */
int
emu_rctl_apply(struct proc *p, uint64_t memory_limit, uint64_t cpu_time_limit,
    int max_procs)
{
    char rule[256];
    int error;

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

    /* Apply memory limit */
    if (memory_limit > 0) {
        /* Use memoryuse:deny for RSS enforcement */
        snprintf(rule, sizeof(rule),
            "pid:%d:memoryuse:deny=%llu",
            p->p_pid, (unsigned long long)memory_limit);
        error = rctl_add_rule(rule, strlen(rule), NULL, 0);
        if (error != 0) {
            printf("kern.emulation.rctl: failed to add memory limit "
                "rule for pid %d: %d\n", p->p_pid, error);
            mtx_unlock(&emu_rctl_lock);
            return (error);
        }

        /* Also set VMEM limit */
        snprintf(rule, sizeof(rule),
            "pid:%d:vmem:deny=%llu",
            p->p_pid, (unsigned long long)(memory_limit * 2));
        error = rctl_add_rule(rule, strlen(rule), NULL, 0);
        if (error != 0) {
            printf("kern.emulation.rctl: warning: failed to add vmem "
                "limit rule for pid %d: %d\n", p->p_pid, error);
            /* Non-fatal - continue with memoryuse limit */
        }
    }

    /* Apply CPU time limit */
    if (cpu_time_limit > 0) {
        snprintf(rule, sizeof(rule),
            "pid:%d:cputime:deny=%llus",
            p->p_pid, (unsigned long long)cpu_time_limit);
        error = rctl_add_rule(rule, strlen(rule), NULL, 0);
        if (error != 0) {
            printf("kern.emulation.rctl: failed to add cputime limit "
                "rule for pid %d: %d\n", p->p_pid, error);
            /* Continue - memory limit is more important */
        }
    }

    /* Apply process count limit */
    if (max_procs > 0) {
        snprintf(rule, sizeof(rule),
            "pid:%d:nproc:deny=%d",
            p->p_pid, max_procs);
        error = rctl_add_rule(rule, strlen(rule), NULL, 0);
        if (error != 0) {
            printf("kern.emulation.rctl: failed to add nproc limit "
                "rule for pid %d: %d\n", p->p_pid, error);
            /* Continue - other limits are more important */
        }
    }

    mtx_unlock(&emu_rctl_lock);

    printf("kern.emulation.rctl: applied limits to pid %d "
        "(memory=%llu, cpu=%llus, nproc=%d)\n",
        p->p_pid,
        (unsigned long long)memory_limit,
        (unsigned long long)cpu_time_limit,
        max_procs);

    return (0);
}

/*
 * Remove rctl limits from a process
 */
int
emu_rctl_remove(struct proc *p)
{
    char rule[256];
    int error;

    if (p == NULL)
        return (EINVAL);

    mtx_lock(&emu_rctl_lock);

    /* Remove memory limit */
    snprintf(rule, sizeof(rule), "pid:%d:memoryuse:*", p->p_pid);
    error = rctl_remove_rule(rule, strlen(rule), NULL, 0);
    if (error != 0 && error != ENOENT) {
        printf("kern.emulation.rctl: warning: failed to remove "
            "memory limit rules for pid %d: %d\n", p->p_pid, error);
    }

    /* Remove vmem limit */
    snprintf(rule, sizeof(rule), "pid:%d:vmem:*", p->p_pid);
    error = rctl_remove_rule(rule, strlen(rule), NULL, 0);
    if (error != 0 && error != ENOENT) {
        printf("kern.emulation.rctl: warning: failed to remove "
            "vmem limit rules for pid %d: %d\n", p->p_pid, error);
    }

    /* Remove cputime limit */
    snprintf(rule, sizeof(rule), "pid:%d:cputime:*", p->p_pid);
    error = rctl_remove_rule(rule, strlen(rule), NULL, 0);
    if (error != 0 && error != ENOENT) {
        printf("kern.emulation.rctl: warning: failed to remove "
            "cputime limit rules for pid %d: %d\n", p->p_pid, error);
    }

    /* Remove nproc limit */
    snprintf(rule, sizeof(rule), "pid:%d:nproc:*", p->p_pid);
    error = rctl_remove_rule(rule, strlen(rule), NULL, 0);
    if (error != 0 && error != ENOENT) {
        printf("kern.emulation.rctl: warning: failed to remove "
            "nproc limit rules for pid %d: %d\n", p->p_pid, error);
    }

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

    if (racct_enable)
        status |= EMU_RCTL_AVAILABLE;

    if (emu_rctl_enabled)
        status |= EMU_RCTL_ENFORCED;

    return (status);
}

/*
 * Get current limit for a process resource
 */
uint64_t
emu_rctl_get_limit(struct proc *p, int resource)
{
    if (p == NULL)
        return (0);

    return (rctl_get_limit(p, resource));
}

/*
 * Get available (remaining) limit for a process resource
 */
uint64_t
emu_rctl_get_available(struct proc *p, int resource)
{
    if (p == NULL)
        return (0);

    return (rctl_get_available(p, resource));
}

/*
 * Set per-user limits using login class
 */
int
emu_rctl_set_user_limits(uid_t uid, const char *loginclass,
    int max_instances, uint64_t max_memory)
{
    char rule[256];
    int error;

    mtx_lock(&emu_rctl_lock);

    if (loginclass != NULL) {
        /* Set login class limits */
        if (max_instances > 0) {
            snprintf(rule, sizeof(rule),
                "loginclass:%s:nproc:deny=%d",
                loginclass, max_instances * 100); /* generous multiplier */
            error = rctl_add_rule(rule, strlen(rule), NULL, 0);
            if (error != 0) {
                printf("kern.emulation.rctl: failed to add loginclass "
                    "nproc rule: %d\n", error);
            }
        }

        if (max_memory > 0) {
            snprintf(rule, sizeof(rule),
                "loginclass:%s:memoryuse:deny=%llu",
                loginclass, (unsigned long long)max_memory);
            error = rctl_add_rule(rule, strlen(rule), NULL, 0);
            if (error != 0) {
                printf("kern.emulation.rctl: failed to add loginclass "
                    "memoryuse rule: %d\n", error);
            }
        }
    } else {
        /* Set user limits */
        if (max_instances > 0) {
            snprintf(rule, sizeof(rule),
                "user:%d:nproc:deny=%d",
                uid, max_instances * 100);
            error = rctl_add_rule(rule, strlen(rule), NULL, 0);
            if (error != 0) {
                printf("kern.emulation.rctl: failed to add user "
                    "nproc rule: %d\n", error);
            }
        }

        if (max_memory > 0) {
            snprintf(rule, sizeof(rule),
                "user:%d:memoryuse:deny=%llu",
                uid, (unsigned long long)max_memory);
            error = rctl_add_rule(rule, strlen(rule), NULL, 0);
            if (error != 0) {
                printf("kern.emulation.rctl: failed to add user "
                    "memoryuse rule: %d\n", error);
            }
        }
    }

    mtx_unlock(&emu_rctl_lock);

    return (0);
}

/*
 * Initialize rctl integration
 */
int
emu_rctl_init(void)
{
    int status;

    mtx_init(&emu_rctl_lock, "emu_rctl", NULL, MTX_DEF);

    status = emu_rctl_status();

    if (status & EMU_RCTL_AVAILABLE) {
        printf("kern.emulation.rctl: initialized (racct enabled)\n");
        if (!(status & EMU_RCTL_ENFORCED)) {
            printf("kern.emulation.rctl: WARNING - rctl enabled but "
                "emulation integration disabled\n");
        }
    } else {
        printf("kern.emulation.rctl: WARNING - racct not enabled in kernel\n");
        printf("kern.emulation.rctl: Resource limits will use fallback "
            "sysctl-based enforcement\n");
    }

    emu_rctl_initialized = 1;

    return (0);
}

SYSINIT(emu_rctl, SI_SUB_EMULATION, SI_ORDER_ANY, emu_rctl_init, NULL);
