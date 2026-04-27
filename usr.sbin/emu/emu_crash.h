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

#ifndef _EMU_CRASH_H_
#define	_EMU_CRASH_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "emu_engine.h"
#include "emu_decoder.h"

/*
 * Emulation Framework - Crash Detection and Containment
 *
 * This module provides crash detection, state capture, and clean
 * termination for emulated instances.
 *
 * Security considerations:
 * - Detect guest crashes/panics without host impact
 * - Capture crash state for analysis
 * - Clean resource termination
 * - Prevent crash propagation to host
 */

/* Maximum crash log size */
#define	EMU_CRASH_LOG_MAX	4096

/* Crash types */
enum emu_crash_type {
	EMU_CRASH_NONE = 0,
	EMU_CRASH_TRIPLE_FAULT,		/* x86 triple fault */
	EMU_CRASH_UNHANDLED_EXCEPTION,	/* Unhandled exception */
	EMU_CRASH_INVALID_OPCODE,	/* Invalid instruction */
	EMU_CRASH_MEMORY_VIOLATION,	/* Memory access violation */
	EMU_CRASH_STACK_OVERFLOW,	/* Stack overflow detected */
	EMU_CRASH_WATCHDOG_TIMEOUT,	/* Watchdog timer expired */
	EMU_CRASH_HOST_SIGNAL,		/* Host signal received */
	EMU_CRASH_RESOURCE_EXHAUSTED,	/* Resource limit hit */
	EMU_CRASH_INTERNAL_ERROR	/* Emulator internal error */
};

/* Crash state structure */
struct emu_crash_state {
	enum emu_crash_type	type;		/* Crash type */
	uint64_t		timestamp;	/* Crash timestamp */
	uint64_t		rip;		/* Instruction pointer at crash */
	uint64_t		rsp;		/* Stack pointer at crash */
	uint64_t		rbp;		/* Base pointer at crash */
	uint64_t		error_code;	/* Architecture-specific error */
	uint64_t		cr2;		/* CR2 register (fault address) */
	uint32_t		cpu_id;		/* CPU that crashed */
	char			log[EMU_CRASH_LOG_MAX];	/* Crash log */
	size_t			log_len;	/* Log length */
	bool			captured;	/* State captured flag */
};

/* Crash capture context */
struct emu_crash_context {
	struct emu_cpu_state	*cpu;		/* CPU state */
	struct emu_guest_mem	*mem;		/* Guest memory */
	struct emu_crash_state	*crash;		/* Crash state */
	void			*platform;	/* Platform-specific data */
};

/*
 * Crash detection API
 */

/* Initialize crash detection subsystem */
int emu_crash_init(void);

/* Enable crash detection for instance */
int emu_crash_enable(struct emu_crash_context *ctx);

/* Disable crash detection for instance */
int emu_crash_disable(struct emu_crash_context *ctx);

/*
 * Crash handlers
 */

/* Handle triple fault (x86) */
int emu_crash_triple_fault(struct emu_crash_context *ctx);

/* Handle unhandled exception */
int emu_crash_exception(struct emu_crash_context *ctx, uint32_t vector,
    uint32_t error_code);

/* Handle invalid opcode */
int emu_crash_invalid_opcode(struct emu_crash_context *ctx, uint64_t rip);

/* Handle memory violation */
int emu_crash_memory_violation(struct emu_crash_context *ctx, uint64_t addr,
    bool is_write);

/* Handle watchdog timeout */
int emu_crash_watchdog(struct emu_crash_context *ctx);

/* Handle host signal */
int emu_crash_signal(struct emu_crash_context *ctx, int sig);

/*
 * State capture
 */

/* Capture crash state */
int emu_crash_capture_state(struct emu_crash_context *ctx);

/* Capture register state */
int emu_crash_capture_registers(struct emu_crash_context *ctx);

/* Capture memory region (for analysis) */
int emu_crash_capture_memory(struct emu_crash_context *ctx, uint64_t addr,
    size_t len, void *buffer);

/* Capture stack trace */
int emu_crash_capture_stack(struct emu_crash_context *ctx,
    uint64_t *frames, int max_frames);

/*
 * Crash logging
 */

/* Add entry to crash log */
int emu_crash_log(struct emu_crash_state *crash, const char *fmt, ...);

/* Get crash log */
const char *emu_crash_get_log(struct emu_crash_state *crash);

/* Clear crash log */
void emu_crash_clear_log(struct emu_crash_state *crash);

/*
 * Cleanup and containment
 */

/* Clean termination after crash */
int emu_crash_cleanup(struct emu_crash_context *ctx);

/* Contain crash (prevent propagation) */
int emu_crash_contain(struct emu_crash_context *ctx);

/* Reset crash state */
int emu_crash_reset(struct emu_crash_state *crash);

/*
 * Utility functions
 */

/* Convert crash type to string */
const char *emu_crash_type_str(enum emu_crash_type type);

/* Check if instance has crashed */
bool emu_crash_has_crashed(struct emu_crash_state *crash);

/* Get crash timestamp as string */
const char *emu_crash_timestamp_str(struct emu_crash_state *crash,
    char *buf, size_t len);

/* Dump crash state to file */
int emu_crash_dump_to_file(struct emu_crash_state *crash, const char *path);

/* Dump crash state to buffer */
int emu_crash_dump_to_buffer(struct emu_crash_state *crash, char *buf,
    size_t len);

#endif /* !_EMU_CRASH_H_ */
