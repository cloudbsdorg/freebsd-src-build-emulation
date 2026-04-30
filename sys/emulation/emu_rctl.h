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
 * Get default memory limit
 *
 * Returns default memory limit in bytes
 */
uint64_t emu_rctl_default_memory(void);

/*
 * Get default CPU time limit
 *
 * Returns default CPU time limit in seconds
 */
uint64_t emu_rctl_default_cpu(void);

/*
 * Get default max processes
 *
 * Returns default max processes
 */
int emu_rctl_default_procs(void);

/*
 * Cleanup on module unload
 * Called from module modevent handler
 */
void emu_rctl_cleanup(void);

#endif /* _KERNEL */

#endif /* !_EMU_RCTL_H_ */
