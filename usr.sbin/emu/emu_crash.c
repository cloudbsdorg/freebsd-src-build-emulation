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

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>

#include "emu_crash.h"
#include "emu_engine.h"
#include "emu_decoder.h"
#include "emu.h"

/*
 * Emulation Framework - Crash Detection and Containment Implementation
 *
 * This module implements crash detection, state capture, and clean
 * termination for emulated instances.
 *
 * Security considerations:
 * - Detect guest crashes/panics without host impact
 * - Capture crash state for analysis
 * - Clean resource termination
 * - Prevent crash propagation to host
 */

/* Global crash detection initialized flag */
static bool g_crash_initialized = false;

/*
 * Convert crash type to string for debugging
 */
const char *
emu_crash_type_str(enum emu_crash_type type)
{
	switch (type) {
	case EMU_CRASH_NONE:
		return "None";
	case EMU_CRASH_TRIPLE_FAULT:
		return "Triple Fault";
	case EMU_CRASH_UNHANDLED_EXCEPTION:
		return "Unhandled Exception";
	case EMU_CRASH_INVALID_OPCODE:
		return "Invalid Opcode";
	case EMU_CRASH_MEMORY_VIOLATION:
		return "Memory Violation";
	case EMU_CRASH_STACK_OVERFLOW:
		return "Stack Overflow";
	case EMU_CRASH_WATCHDOG_TIMEOUT:
		return "Watchdog Timeout";
	case EMU_CRASH_HOST_SIGNAL:
		return "Host Signal";
	case EMU_CRASH_RESOURCE_EXHAUSTED:
		return "Resource Exhausted";
	case EMU_CRASH_INTERNAL_ERROR:
		return "Internal Error";
	default:
		return "Unknown";
	}
}

/*
 * Initialize crash detection subsystem
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_init(void)
{
	if (g_crash_initialized)
		return (0);

	/* Initialize crash detection subsystem */
	/* TODO: Set up signal handlers, watchdog timers, etc. */

	g_crash_initialized = true;
	return (0);
}

/*
 * Enable crash detection for instance
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_enable(struct emu_crash_context *ctx)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	/* Reset crash state */
	emu_crash_reset(ctx->crash);

	/* TODO: Set up instance-specific crash detection */

	return (0);
}

/*
 * Disable crash detection for instance
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_disable(struct emu_crash_context *ctx)
{
	if (ctx == NULL)
		return (-1);

	/* TODO: Tear down instance-specific crash detection */

	return (0);
}

/*
 * Reset crash state
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_reset(struct emu_crash_state *crash)
{
	if (crash == NULL)
		return (-1);

	memset(crash, 0, sizeof(struct emu_crash_state));
	crash->type = EMU_CRASH_NONE;

	return (0);
}

/*
 * Check if instance has crashed
 */
bool
emu_crash_has_crashed(struct emu_crash_state *crash)
{
	if (crash == NULL)
		return (false);

	return (crash->type != EMU_CRASH_NONE);
}

/*
 * Get current timestamp
 */
static uint64_t
emu_get_timestamp(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return (0);

	return ((uint64_t)tv.tv_sec * 1000000 + tv.tv_usec);
}

/*
 * Add entry to crash log
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_log(struct emu_crash_state *crash, const char *fmt, ...)
{
	va_list ap;
	int remaining;
	char *buf;

	if (crash == NULL || fmt == NULL)
		return (-1);

	remaining = EMU_CRASH_LOG_MAX - crash->log_len - 1;
	if (remaining <= 0)
		return (-1); /* Log full */

	buf = crash->log + crash->log_len;

	va_start(ap, fmt);
	int written = vsnprintf(buf, remaining, fmt, ap);
	va_end(ap);

	if (written > 0)
		crash->log_len += written;

	return (0);
}

/*
 * Get crash log
 */
const char *
emu_crash_get_log(struct emu_crash_state *crash)
{
	if (crash == NULL)
		return ("");

	return (crash->log);
}

/*
 * Clear crash log
 */
void
emu_crash_clear_log(struct emu_crash_state *crash)
{
	if (crash == NULL)
		return;

	crash->log[0] = '\0';
	crash->log_len = 0;
}

/*
 * Get crash timestamp as string
 */
const char *
emu_crash_timestamp_str(struct emu_crash_state *crash, char *buf, size_t len)
{
	time_t t;
	struct tm *tm;

	if (crash == NULL || buf == NULL || len == 0)
		return ("");

	t = crash->timestamp / 1000000;
	tm = localtime(&t);

	if (tm == NULL) {
		snprintf(buf, len, "invalid");
		return (buf);
	}

	strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm);
	return (buf);
}

/*
 * Capture register state
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_capture_registers(struct emu_crash_context *ctx)
{
	if (ctx == NULL || ctx->cpu == NULL || ctx->crash == NULL)
		return (-1);

	/* Capture key registers */
	ctx->crash->rip = ctx->cpu->rip;
	ctx->crash->rsp = ctx->cpu->rsp;
	ctx->crash->rbp = ctx->cpu->rbp;

	/* Log register state */
	emu_crash_log(ctx->crash, "RIP=0x%016lx RSP=0x%016lx RBP=0x%016lx\n",
	    ctx->crash->rip, ctx->crash->rsp, ctx->crash->rbp);

	return (0);
}

/*
 * Capture memory region for analysis
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_capture_memory(struct emu_crash_context *ctx, uint64_t addr,
    size_t len, void *buffer)
{
	enum emu_mem_access ret;

	if (ctx == NULL || ctx->mem == NULL || buffer == NULL)
		return (-1);

	/* Read memory using bounds-checked access */
	ret = emu_mem_read_bytes(ctx->mem, addr, buffer, len);
	if (ret != EMU_MEM_ACCESS_OK) {
		emu_crash_log(ctx->crash, "Failed to capture memory at 0x%lx: %s\n",
		    addr, emu_mem_access_str(ret));
		return (-1);
	}

	return (0);
}

/*
 * Capture stack trace
 * Returns number of frames captured, or -1 on failure
 */
int
emu_crash_capture_stack(struct emu_crash_context *ctx,
    uint64_t *frames, int max_frames)
{
	uint64_t fp, next_fp;
	int count = 0;

	if (ctx == NULL || ctx->cpu == NULL || ctx->mem == NULL ||
	    frames == NULL || max_frames <= 0)
		return (-1);

	/* Start from current frame pointer */
	fp = ctx->cpu->rbp;

	/* Walk the stack */
	while (count < max_frames) {
		/* Read saved frame pointer */
		if (emu_mem_read64(ctx->mem, fp, &next_fp) != EMU_MEM_ACCESS_OK)
			break;

		/* Read return address */
		if (emu_mem_read64(ctx->mem, fp + 8, &frames[count]) != EMU_MEM_ACCESS_OK)
			break;

		/* Stop if we hit zero or invalid address */
		if (frames[count] == 0 || next_fp <= fp)
			break;

		fp = next_fp;
		count++;
	}

	return (count);
}

/*
 * Capture full crash state
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_capture_state(struct emu_crash_context *ctx)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	/* Set timestamp */
	ctx->crash->timestamp = emu_get_timestamp();

	/* Mark as captured */
	ctx->crash->captured = true;

	/* Capture registers */
	emu_crash_capture_registers(ctx);

	/* Capture stack trace */
	uint64_t stack_frames[32];
	int num_frames = emu_crash_capture_stack(ctx, stack_frames, 32);

	if (num_frames > 0) {
		emu_crash_log(ctx->crash, "Stack trace (%d frames):\n", num_frames);
		for (int i = 0; i < num_frames; i++) {
			emu_crash_log(ctx->crash, "  [%d] 0x%016lx\n", i, stack_frames[i]);
		}
	}

	return (0);
}

/*
 * Handle triple fault (x86)
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_triple_fault(struct emu_crash_context *ctx)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_TRIPLE_FAULT;
	emu_crash_log(ctx->crash, "Triple fault detected\n");

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Handle unhandled exception
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_exception(struct emu_crash_context *ctx, uint32_t vector,
    uint32_t error_code)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_UNHANDLED_EXCEPTION;
	ctx->crash->error_code = error_code;
	emu_crash_log(ctx->crash, "Unhandled exception: vector=%u, error=0x%x\n",
	    vector, error_code);

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Handle invalid opcode
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_invalid_opcode(struct emu_crash_context *ctx, uint64_t rip)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_INVALID_OPCODE;
	ctx->crash->rip = rip;
	emu_crash_log(ctx->crash, "Invalid opcode at RIP=0x%016lx\n", rip);

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Handle memory violation
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_memory_violation(struct emu_crash_context *ctx, uint64_t addr,
    bool is_write)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_MEMORY_VIOLATION;
	ctx->crash->cr2 = addr;
	emu_crash_log(ctx->crash, "Memory violation at 0x%016lx (%s)\n",
	    addr, is_write ? "write" : "read");

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Handle watchdog timeout
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_watchdog(struct emu_crash_context *ctx)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_WATCHDOG_TIMEOUT;
	emu_crash_log(ctx->crash, "Watchdog timeout\n");

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Handle host signal
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_signal(struct emu_crash_context *ctx, int sig)
{
	if (ctx == NULL || ctx->crash == NULL)
		return (-1);

	ctx->crash->type = EMU_CRASH_HOST_SIGNAL;
	ctx->crash->error_code = sig;
	emu_crash_log(ctx->crash, "Host signal received: %d\n", sig);

	/* Capture state */
	emu_crash_capture_state(ctx);

	return (0);
}

/*
 * Contain crash (prevent propagation to host)
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_contain(struct emu_crash_context *ctx)
{
	if (ctx == NULL)
		return (-1);

	/* TODO: Implement crash containment */
	/* - Isolate guest memory */
	/* - Prevent further guest execution */
	/* - Block guest-initiated host operations */

	emu_crash_log(ctx->crash, "Crash containment activated\n");

	return (0);
}

/*
 * Clean termination after crash
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_cleanup(struct emu_crash_context *ctx)
{
	if (ctx == NULL)
		return (-1);

	emu_crash_log(ctx->crash, "Cleaning up after crash\n");

	/* TODO: Implement clean resource termination */
	/* - Free allocated resources */
	/* - Close file descriptors */
	/* - Release memory mappings */

	return (0);
}

/*
 * Dump crash state to file
 * Returns 0 on success, -1 on failure
 */
int
emu_crash_dump_to_file(struct emu_crash_state *crash, const char *path)
{
	FILE *fp;
	char timestamp_buf[64];

	if (crash == NULL || path == NULL)
		return (-1);

	fp = fopen(path, "w");
	if (fp == NULL)
		return (-1);

	fprintf(fp, "=== Emulation Crash Report ===\n\n");
	fprintf(fp, "Type: %s\n", emu_crash_type_str(crash->type));
	fprintf(fp, "Timestamp: %s\n",
	    emu_crash_timestamp_str(crash, timestamp_buf, sizeof(timestamp_buf)));
	fprintf(fp, "RIP: 0x%016lx\n", crash->rip);
	fprintf(fp, "RSP: 0x%016lx\n", crash->rsp);
	fprintf(fp, "RBP: 0x%016lx\n", crash->rbp);
	fprintf(fp, "Error Code: 0x%lx\n", crash->error_code);
	fprintf(fp, "CR2: 0x%lx\n", crash->cr2);
	fprintf(fp, "CPU: %u\n", crash->cpu_id);
	fprintf(fp, "Captured: %s\n", crash->captured ? "yes" : "no");
	fprintf(fp, "\n=== Crash Log ===\n%s\n", crash->log);

	fclose(fp);
	return (0);
}

/*
 * Dump crash state to buffer
 * Returns bytes written, or -1 on failure
 */
int
emu_crash_dump_to_buffer(struct emu_crash_state *crash, char *buf, size_t len)
{
	char timestamp_buf[64];
	int written;

	if (crash == NULL || buf == NULL || len == 0)
		return (-1);

	written = snprintf(buf, len,
	    "=== Emulation Crash Report ===\n\n"
	    "Type: %s\n"
	    "Timestamp: %s\n"
	    "RIP: 0x%016lx\n"
	    "RSP: 0x%016lx\n"
	    "RBP: 0x%016lx\n"
	    "Error Code: 0x%lx\n"
	    "CR2: 0x%lx\n"
	    "CPU: %u\n"
	    "Captured: %s\n"
	    "\n=== Crash Log ===\n%s\n",
	    emu_crash_type_str(crash->type),
	    emu_crash_timestamp_str(crash, timestamp_buf, sizeof(timestamp_buf)),
	    crash->rip, crash->rsp, crash->rbp,
	    crash->error_code, crash->cr2, crash->cpu_id,
	    crash->captured ? "yes" : "no",
	    crash->log);

	return (written);
}
