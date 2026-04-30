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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_EMU_MEMMGMT_H_
#define	_SYS_EMU_MEMMGMT_H_

/*
 * Emulation Framework Memory Management Header
 *
 * This header provides memory policy definitions and overcommit
 * safety mechanisms for the emulation framework.
 */

/* Memory allocation policies */
#define	EMU_MEM_POLICY_PREALLOC		0	/* Pre-allocate all memory */
#define	EMU_MEM_POLICY_DEMAND		1	/* Demand-paged (MAP_NORESERVE) */

#ifdef _KERNEL

/* Memory management functions */
void	emu_memmgmt_init(void);
void	emu_memmgmt_destroy(void);
int	emu_memmgmt_check_overcommit(uint64_t requested_memory);
int	emu_memmgmt_get_policy(void);
int	emu_memmgmt_get_balloon_min_pct(void);
int	emu_memmgmt_get_balloon_interval(void);

/* Balloon sysctl interface */
void	emu_balloon_sysctl_create(uint64_t inst_id, const char *inst_name,
    uint64_t *balloon_target);

#endif /* _KERNEL */

#endif /* !_SYS_EMU_MEMMGMT_H_ */
