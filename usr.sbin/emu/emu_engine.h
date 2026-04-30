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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _EMU_ENGINE_H_
#define	_EMU_ENGINE_H_

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/caprights.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "emu_mem.h"

/*
 * Emulation Engine - Bounds-Checked Memory Access
 *
 * This module provides safe memory access primitives for the custom
 * software emulator. All guest memory accesses are validated against
 * the allocated memory region to prevent out-of-bounds reads/writes.
 */

/* Memory access result codes */
enum emu_mem_access {
	EMU_MEM_ACCESS_OK = 0,		/* Access succeeded */
	EMU_MEM_ACCESSOutOfBounds,	/* Access outside valid memory region */
	EMU_MEM_ACCESS_NULL,		/* NULL pointer or uninitialized region */
	EMU_MEM_ACCESS_ALIGNMENT,	/* Unaligned access (if strict mode) */
	EMU_MEM_ACCESS_PROTECTED	/* Access to protected region */
};

/* Memory region flags */
#define	EMU_MEM_REGION_READ		0x01
#define	EMU_MEM_REGION_WRITE		0x02
#define	EMU_MEM_REGION_EXECUTE		0x04
#define	EMU_MEM_REGION_MMIO		0x08	/* Memory-mapped I/O region */
#define	EMU_MEM_REGION_READONLY		0x10	/* Read-only region */

/* Memory region descriptor */
struct emu_mem_region {
	uint64_t	base;		/* Base address (guest physical) */
	size_t		size;		/* Region size in bytes */
	int		flags;		/* Region flags (READ/WRITE/EXECUTE) */
	void		*host_ptr;	/* Pointer to host memory (if mapped) */
	const char	*name;		/* Region name for debugging */
};

/* Guest memory descriptor */
struct emu_guest_mem {
	void		*base;		/* Base pointer to guest memory */
	size_t		total_size;	/* Total allocated size */
	size_t		used_size;	/* Currently used size */
	struct emu_mem_region *regions;	/* Memory region descriptors */
	int		num_regions;	/* Number of regions */
	bool		strict_align;	/* Enforce strict alignment */
	bool		initialized;	/* Memory subsystem initialized */
	enum emu_endian	guest_endian;	/* Guest endianness - defined in emu_mem.h */
};

/*
 * Core dump prevention
 *
 * Disable core dumps for emulator processes to prevent guest secrets
 * from leaking via core dump files.
 */

/**
 * emu_disable_coredump() - Disable core dumps for emulator process
 *
 * Prevents guest memory contents from being written to core dump files
 * by setting RLIMIT_CORE to 0 and using procctl to disable core dumps.
 *
 * @return 0 on success, -1 on failure with errno set
 */
int emu_disable_coredump(void);

/*
 * Ptrace prevention
 *
 * Disable ptrace attachment to emulator processes to prevent debugger
 * attacks and guest memory inspection.
 */

/**
 * emu_disable_ptrace() - Disable ptrace attachment to emulator process
 *
 * Prevents debuggers from attaching to the emulator process using
 * procctl PROC_TRACE_CTL_DISABLE. This blocks ptrace(PTRACE_ATTACH)
 * and prevents guest memory inspection via debugger.
 *
 * @return 0 on success, -1 on failure with errno set
 */
int emu_disable_ptrace(void);

/*
 * Signal Handling Security - Safe Signal Handlers
 *
 * Flag-only signal handlers that cannot inject unexpected behavior.
 * Signal handlers only set atomic flags, which are then checked in
 * the main execution loop.
 */

/**
 * emu_init_signal_handlers() - Initialize signal handlers for emulator process
 *
 * Sets up flag-only signal handlers for SIGSEGV, SIGPIPE, SIGTERM,
 * SIGINT, SIGHUP, SIGUSR1, and SIGUSR2.
 *
 * @return 0 on success, -1 on failure
 */
int emu_init_signal_handlers(void);

/**
 * emu_check_signals() - Check and handle pending signals
 *
 * Should be called in the main execution loop to process any signals
 * that have been received.
 *
 * @return 0 if no signals pending, signal number if signal needs handling
 */
int emu_check_signals(void);

/**
 * emu_handle_signal() - Handle a signal in the main loop context
 *
 * @param sig Signal number to handle
 * @return 0 to continue execution, -1 to terminate
 */
int emu_handle_signal(int sig);

/*
 * Emulator execution state with security controls
 *
 * This structure tracks execution state for security features:
 * - Instruction count limits (prevent infinite loops)
 * - Watchdog timer (crash detection)
 * - Execution slice tracking
 */
struct emu_exec_state {
	uint64_t	insn_count;		/* Instructions executed in current slice */
	uint64_t	insn_limit;		/* Max instructions per slice */
	uint64_t	total_insns;		/* Total instructions since start */
	time_t		start_time;		/* Execution start time */
	time_t		last_activity;		/* Last guest activity timestamp */
	time_t		watchdog_timeout;	/* Watchdog timeout in seconds */
	bool		watchdog_enabled;	/* Watchdog timer enabled */
	bool		slice_exceeded;		/* Instruction slice exceeded */
	bool		watchdog_triggered;	/* Watchdog timeout triggered */
};

/*
 * Memory access primitives - see emu_mem.h for full declarations
 * All accesses are bounds-checked
 */

/* Bounds-checked read operations */
enum emu_mem_access emu_mem_read8(struct emu_guest_mem *mem, uint64_t guest_addr, uint8_t *value);
enum emu_mem_access emu_mem_read16(struct emu_guest_mem *mem, uint64_t guest_addr, uint16_t *value);
enum emu_mem_access emu_mem_read32(struct emu_guest_mem *mem, uint64_t guest_addr, uint32_t *value);
enum emu_mem_access emu_mem_read64(struct emu_guest_mem *mem, uint64_t guest_addr, uint64_t *value);

/* Bounds-checked write operations */
enum emu_mem_access emu_mem_write8(struct emu_guest_mem *mem, uint64_t guest_addr, uint8_t value);
enum emu_mem_access emu_mem_write16(struct emu_guest_mem *mem, uint64_t guest_addr, uint16_t value);
enum emu_mem_access emu_mem_write32(struct emu_guest_mem *mem, uint64_t guest_addr, uint32_t value);
enum emu_mem_access emu_mem_write64(struct emu_guest_mem *mem, uint64_t guest_addr, uint64_t value);

/* Bulk read/write operations */
enum emu_mem_access emu_mem_read_bytes(struct emu_guest_mem *mem, uint64_t guest_addr,
    void *buffer, size_t len);
enum emu_mem_access emu_mem_write_bytes(struct emu_guest_mem *mem, uint64_t guest_addr,
    const void *buffer, size_t len);

/* Memory isolation and cleanup */
void emu_mem_isolate(struct emu_guest_mem *mem);
void emu_mem_cleanup(struct emu_guest_mem *mem);

/* Bounds check helper - returns true if access is valid */
bool emu_mem_check_bounds(struct emu_guest_mem *mem, uint64_t guest_addr, size_t len);

/* Region management */
int emu_mem_add_region(struct emu_guest_mem *mem, uint64_t base, size_t size, int flags,
    const char *name);
int emu_mem_remove_region(struct emu_guest_mem *mem, uint64_t base);
struct emu_mem_region *emu_mem_find_region(struct emu_guest_mem *mem, uint64_t addr);

/* Memory initialization and cleanup */
int emu_mem_init(struct emu_guest_mem *mem, size_t total_size);
void emu_mem_destroy(struct emu_guest_mem *mem);
void emu_mem_scrub(struct emu_guest_mem *mem);

/* Utility functions */
const char *emu_mem_access_str(enum emu_mem_access access);
void emu_mem_dump_regions(struct emu_guest_mem *mem);

/*
 * Capsicum sandboxing - Limit process capabilities after initialization
 *
 * These functions implement Capsicum capability mode sandboxing to
 * restrict the emulator process after it has opened all necessary
 * file descriptors.
 */

/* Enter Capsicum capability mode sandbox - call after opening all FDs */
int emu_enter_sandbox(void);

/* Limit rights on a specific file descriptor */
int emu_limit_fd_rights(int fd, cap_rights_t *rights);

/* Limit ioctl operations on a specific file descriptor */
int emu_limit_fd_ioctls(int fd, const u_long *cmds, size_t ncmds);

/* Check if FD is essential (kept open during sandboxing) */
bool emu_is_essential_fd(int fd);

/*
 * Execution control - Instruction count limits and watchdog timer
 *
 * These functions implement security controls to prevent guest code
 * from hanging the emulator or consuming excessive resources.
 */

/* Initialize execution state */
int emu_exec_state_init(struct emu_exec_state *state, uint64_t insn_limit,
    time_t watchdog_timeout);

/* Reset instruction counter for new slice */
void emu_exec_reset_slice(struct emu_exec_state *state);

/* Increment instruction counter and check limit */
int emu_exec_insn_increment(struct emu_exec_state *state, uint64_t count);

/* Check if instruction slice exceeded */
bool emu_exec_slice_exceeded(struct emu_exec_state *state);

/* Update last activity timestamp */
void emu_exec_update_activity(struct emu_exec_state *state);

/* Check if watchdog timer expired */
bool emu_exec_watchdog_expired(struct emu_exec_state *state);

/* Get execution statistics */
void emu_exec_get_stats(struct emu_exec_state *state, uint64_t *total_insns,
    time_t *uptime);

/* Reset execution state */
void emu_exec_state_destroy(struct emu_exec_state *state);

/*
 * Inline helpers are defined in emu_mem.h
 */

#endif /* !_EMU_ENGINE_H_ */
