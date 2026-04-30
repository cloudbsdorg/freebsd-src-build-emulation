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
 * HOWEVER CAULD AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
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
 * Features:
 * - Per-instance memory limits via rctl
 * - Per-instance CPU time limits via rctl
 * - Per-instance process count limits via rctl
 * - Per-user resource aggregation via rctl
 * - Login class limits for emulator processes
 */

#ifndef _EMU_RCTL_H_
#define _EMU_RCTL_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/proc.h>

/*
 * rctl integration status
 */
#define EMU_RCTL_AVAILABLE	0x01
#define EMU_RCTL_ENFORCED	0x02

/*
 * Initialize rctl integration
 * Returns 0 on success, error code on failure
 */
int emu_rctl_init(void);

/*
 * Apply rctl limits to an emulator process
 * 
 * Parameters:
 *   p - process to apply limits to
 *   memory_limit - memory limit in bytes (0 = no limit)
 *   cpu_time_limit - CPU time limit in seconds (0 = no limit)
 *   max_procs - maximum number of processes (0 = no limit)
 *
 * Returns 0 on success, error code on failure
 */
int emu_rctl_apply(struct proc *p, uint64_t memory_limit,
    uint64_t cpu_time_limit, int max_procs);

/*
 * Remove rctl limits from a process
 * 
 * Parameters:
 *   p - process to remove limits from
 *
 * Returns 0 on success, error code on failure
 */
int emu_rctl_remove(struct proc *p);

/*
 * Check if rctl is available and enabled
 * 
 * Returns bitmask of EMU_RCTL_* flags
 */
int emu_rctl_status(void);

/*
 * Get current rctl limit for a process
 * 
 * Parameters:
 *   p - process to query
 *   resource - RACCT_* resource type
 *
 * Returns current limit, or 0 if not available
 */
uint64_t emu_rctl_get_limit(struct proc *p, int resource);

/*
 * Get available (remaining) rctl limit for a process
 * 
 * Parameters:
 *   p - process to query
 *   resource - RACCT_* resource type
 *
 * Returns available amount, or 0 if not available
 */
uint64_t emu_rctl_get_available(struct proc *p, int resource);

/*
 * Update per-user rctl limits based on login class
 * 
 * Parameters:
 *   uid - user ID
 *   loginclass - login class name (NULL for default)
 *   max_instances - maximum instances for user
 *   max_memory - maximum memory for user
 *
 * Returns 0 on success, error code on failure
 */
int emu_rctl_set_user_limits(uid_t uid, const char *loginclass,
    int max_instances, uint64_t max_memory);

/*
 * Format rctl limit as human-readable string
 * 
 * Parameters:
 *   resource - RACCT_* resource type
 *   amount - amount to format
 *   buf - output buffer
 *   buflen - buffer size
 */
void emu_rctl_format_limit(int resource, uint64_t amount, char *buf,
    size_t buflen);

/*
 * Get resource name string for RACCT_* type
 */
const char *emu_rctl_resource_name(int resource);

#endif /* _KERNEL */

#endif /* _EMU_RCTL_H_ */
