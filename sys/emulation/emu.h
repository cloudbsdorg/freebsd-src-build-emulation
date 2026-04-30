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

#ifndef _SYS_EMU_H_
#define	_SYS_EMU_H_

#include <sys/malloc.h>

/*
 * Emulation Framework Core Header
 *
 * This header provides the core definitions for the kernel emulation
 * framework, including instance management, configuration constants,
 * and public API functions.
 */

/* Maximum number of concurrent emulation instances */
#define	MAXEMUINSTANCES		256

/* Malloc type for emulation framework */
MALLOC_DECLARE(M_EMU);

/*
 * CPU register state structure
 * Architecture-specific register state is defined by backend modules
 */
struct emu_cpu_state {
	uint64_t	rax;
	uint64_t	rbx;
	uint64_t	rcx;
	uint64_t	rdx;
	uint64_t	rsi;
	uint64_t	rdi;
	uint64_t	rbp;
	uint64_t	rsp;
	uint64_t	r8;
	uint64_t	r9;
	uint64_t	r10;
	uint64_t	r11;
	uint64_t	r12;
	uint64_t	r13;
	uint64_t	r14;
	uint64_t	r15;
	uint64_t	rip;
	uint64_t	rflags;
	uint64_t	cr0;
	uint64_t	cr3;
	uint64_t	cr4;
};

/* Instance state flags */
#define	EMU_INST_RUNNING	0x0001
#define	EMU_INST_STOPPED	0x0002
#define	EMU_INST_PAUSED		0x0004
#define	EMU_INST_ERROR		0x0008

/* Access control */
#define	GID_EMU			979	/* Emulation framework group */

/* Instance permissions */
#define	EMU_PERM_CREATE		0x0001
#define	EMU_PERM_DESTROY	0x0002
#define	EMU_PERM_START		0x0003
#define	EMU_PERM_STOP		0x0004
#define	EMU_PERM_PAUSE		0x0005
#define	EMU_PERM_RESUME		0x0006
#define	EMU_PERM_MODIFY		0x0007
#define	EMU_PERM_ADMIN		0x0008

/*
 * Function prototypes for instance management
 */
#ifdef _KERNEL
int	emu_get_instance_count(void);
int	emu_instance_register(void);
void	emu_instance_deregister(void);

/* Instance lifecycle management */
int	emu_instance_create(const char *name, uid_t uid, gid_t gid,
	    uint64_t memory_limit, uint64_t cpu_time_limit, int num_vcpus,
	    int num_sockets, uint64_t *inst_id_out);
int	emu_instance_destroy(uint64_t inst_id);
int	emu_instance_start(uint64_t inst_id);
int	emu_instance_stop(uint64_t inst_id);
int	emu_instance_attach_pid(uint64_t inst_id, pid_t pid);
int	emu_instance_detach_pid(uint64_t inst_id);
int	emu_instance_get_state(uint64_t inst_id);
void	emu_instance_update_memory(uint64_t inst_id, uint64_t memory_used);
void	emu_instance_update_cpu_time(uint64_t inst_id, uint64_t cpu_time);
int	emu_instance_check_cpu_limit(uint64_t inst_id);
int	emu_instance_get_info(uint64_t inst_id, struct sbuf *sb);
uint64_t emu_instance_total_memory(void);
int	emu_instance_total_count(void);
int	emu_instance_visible_count(void);
int	emu_instance_list(uint64_t *inst_ids, int max_count);
int	emu_check_instance_ownership(struct thread *td, uint64_t inst_id);
void	emu_instance_init(void);
void	emu_instance_cleanup(void);

/* Sysctl interface */
void	emu_sysctl_init(void);
void	emu_sysctl_destroy(void);
int	emu_module_register(const char *name, int version);
void	emu_module_deregister(const char *name);
void	emu_module_refcount_inc(const char *name);
void	emu_module_refcount_dec(const char *name);
void	emu_sysctl_register_module(const char *name);

/* Access control helpers */
int	emu_check_priv(struct thread *td, int priv);
int	emu_check_access(struct thread *td, uint64_t inst_id, int perm);
int	emu_check_create(struct thread *td);
int	emu_check_destroy(struct thread *td, uint64_t inst_id);
int	emu_check_share_mac(struct thread *td, uint64_t inst_id, const char *share_path);
int	emu_check_snapshot_mac(struct thread *td, uint64_t inst_id, const char *snapshot_name);
int	emu_validate_share_path(struct thread *td, uint64_t inst_id, const char *path);

/* Securelevel integration */
int	emu_securelevel_check(struct thread *td, int level);
int	emu_securelevel_restricted_op(struct thread *td, const char *op);

/* Stack capture interface */
void	emu_stack_init(void);
void	emu_stack_destroy(void);
int	emu_stack_capture(uint64_t inst_id, const char *arch);
int	emu_stack_get(uint64_t inst_id, struct sbuf *sb);
void	emu_stack_register_instance(uint64_t inst_id);
#endif /* _KERNEL */

#endif /* !_SYS_EMU_H_ */
