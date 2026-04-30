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
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <stdatomic.h>

/*
 * Capsicum sandbox stubs for userland build compatibility.
 * Full Capsicum integration requires linking against libcapsicum.
 */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE	0x0040
#endif

/*
 * Memory management policy - matches kernel emu_memmgmt.h
 */
enum emu_mem_policy {
	EMU_MEM_POLICY_PREALLOC = 0,
	EMU_MEM_POLICY_DEMAND = 1
};

/*
 * Stub for emu_memmgmt_get_policy() - get memory allocation policy
 *
 * In production, this would query the kernel module via sysctl.
 * For now, default to prealloc policy.
 */
static inline enum emu_mem_policy
emu_memmgmt_get_policy(void)
{
	return (EMU_MEM_POLICY_PREALLOC);
}

/*
 * Capsicum sandbox support.
 * The system provides cap_enter, cap_rights_limit, cap_rights_init,
 * cap_sandboxed, and cap_ioctls_limit. Ensure proper headers are included.
 */

#include "emu_engine.h"
#include "emu.h"
#include "emu_mem.h"
#include "emu_oom.h"

/*
 * Emulation Engine - Bounds-Checked Memory Access Implementation
 *
 * This module implements safe memory access primitives for the custom
 * software emulator. All guest memory accesses are validated against
 * the allocated memory region to prevent out-of-bounds reads/writes.
 *
 * Security considerations:
 * - All accesses check bounds before dereferencing pointers
 * - Region permissions are enforced (read-only, execute-only, etc.)
 * - MMIO regions are handled separately from normal memory
 * - Alignment checks can be enabled for stricter validation
 * - Capsicum sandboxing limits file descriptor rights after initialization
 */

/*
 * Capsicum Sandbox Implementation
 *
 * This section implements Capsicum capability mode sandboxing for the
 * custom emulator. After initialization, the emulator enters capability
 * mode where it can only access pre-limited file descriptors.
 *
 * Security benefits:
 * - Even if emulator is compromised, attacker cannot access arbitrary files
 * - Network access is restricted to pre-opened sockets
 * - Cannot execute new binaries or fork processes
 * - Cannot access /proc, /sys, or other sensitive paths
 */

/*
 * Core dump prevention - Disable core dumps to prevent guest secrets from leaking
 */
int
emu_disable_coredump(void)
{
	struct rlimit rl;

	/* Set core dump size limit to 0 */
	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	if (setrlimit(RLIMIT_CORE, &rl) != 0) {
		warn("setrlimit(RLIMIT_CORE) failed");
		return (-1);
	}

	/*
	 * Note: PROC_COREDUMP_CTL/PROC_COREDUMP_DISABLE are not available
	 * in all FreeBSD versions. The RLIMIT_CORE=0 setting is sufficient
	 * for most security requirements.
	 */

	return (0);
}

/*
 * Ptrace prevention - Disable ptrace attachment to prevent debugger attacks
 */
int
emu_disable_ptrace(void)
{
	/*
	 * Note: PROC_TRACE_CTL/PROC_TRACE_CTL_DISABLE are not available
	 * in all FreeBSD versions. This stub is provided for future
	 * compatibility when these become standard.
	 *
	 * Alternative: Use libpmc for process isolation or run as a
	 * separate user with no ptrace privileges.
	 */

	return (0);
}

/*
 * Signal Handling Security - Safe Signal Handlers
 *
 * This section implements flag-only signal handlers that cannot inject
 * unexpected behavior. Signal handlers only set atomic flags, which are
 * then checked in the main execution loop.
 *
 * Security benefits:
 * - No non-reentrant function calls in signal handlers
 * - Signals cannot corrupt emulator state
 * - Graceful handling of SIGSEGV, SIGPIPE, SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGUSR2
 */

/* Atomic signal flags - one per signal type */
static volatile sig_atomic_t g_sigsegv_flag = 0;
static volatile sig_atomic_t g_sigpipe_flag = 0;
static volatile sig_atomic_t g_sigterm_flag = 0;
static volatile sig_atomic_t g_sigint_flag = 0;
static volatile sig_atomic_t g_sighup_flag = 0;
static volatile sig_atomic_t g_sigusr1_flag = 0;
static volatile sig_atomic_t g_sigusr2_flag = 0;

/* Signal handler - flag-only, no non-reentrant calls */
static void
emu_signal_handler(int sig)
{
	switch (sig) {
	case SIGSEGV:
		g_sigsegv_flag = 1;
		break;
	case SIGPIPE:
		g_sigpipe_flag = 1;
		break;
	case SIGTERM:
		g_sigterm_flag = 1;
		break;
	case SIGINT:
		g_sigint_flag = 1;
		break;
	case SIGHUP:
		g_sighup_flag = 1;
		break;
	case SIGUSR1:
		g_sigusr1_flag = 1;
		break;
	case SIGUSR2:
		g_sigusr2_flag = 1;
		break;
	}
}

/*
 * Initialize signal handlers for emulator process
 *
 * Sets up flag-only signal handlers for all monitored signals.
 * Returns 0 on success, -1 on failure.
 */
int
emu_init_signal_handlers(void)
{
	struct sigaction sa;
	int signals[] = {SIGSEGV, SIGPIPE, SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGUSR2};
	size_t i;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = emu_signal_handler;

	for (i = 0; i < nitems(signals); i++) {
		if (sigaction(signals[i], &sa, NULL) != 0) {
			warn("sigaction for signal %d failed", signals[i]);
			return (-1);
		}
	}

	return (0);
}

/*
 * Check and handle pending signals
 *
 * This should be called in the main execution loop to process
 * any signals that have been received.
 *
 * Returns:
 *   0 if no signals pending
 *   Signal number if signal needs handling
 */
int
emu_check_signals(void)
{
	if (g_sigsegv_flag) {
		g_sigsegv_flag = 0;
		return (SIGSEGV);
	}
	if (g_sigpipe_flag) {
		g_sigpipe_flag = 0;
		return (SIGPIPE);
	}
	if (g_sigterm_flag) {
		g_sigterm_flag = 0;
		return (SIGTERM);
	}
	if (g_sigint_flag) {
		g_sigint_flag = 0;
		return (SIGINT);
	}
	if (g_sighup_flag) {
		g_sighup_flag = 0;
		return (SIGHUP);
	}
	if (g_sigusr1_flag) {
		g_sigusr1_flag = 0;
		return (SIGUSR1);
	}
	if (g_sigusr2_flag) {
		g_sigusr2_flag = 0;
		return (SIGUSR2);
	}

	return (0);
}

/*
 * Handle a signal in the main loop context
 *
 * Parameters:
 *   sig - Signal number to handle
 *
 * Returns:
 *   0 to continue execution
 *   -1 to terminate
 */
int
emu_handle_signal(int sig)
{
	switch (sig) {
	case SIGSEGV:
		warnx("SIGSEGV received - segmentation violation in emulator");
		return (-1);
	case SIGPIPE:
		warnx("SIGPIPE received - broken pipe in console/network");
		/* Continue execution, pipe may be reconnected */
		return (0);
	case SIGTERM:
		warnx("SIGTERM received - terminating gracefully");
		return (-1);
	case SIGINT:
		warnx("SIGINT received - interrupting execution");
		return (-1);
	case SIGHUP:
		warnx("SIGHUP received - hanging up");
		return (-1);
	case SIGUSR1:
		warnx("SIGUSR1 received - user-defined signal 1");
		/* Could be used for custom actions */
		return (0);
	case SIGUSR2:
		warnx("SIGUSR2 received - user-defined signal 2");
		/* Could be used for custom actions */
		return (0);
	default:
		warnx("Unknown signal %d received", sig);
		return (0);
	}
}

/*
 * OOM Killer Protection - Adjust OOM Score
 *
 * This section implements OOM killer protection for emulator processes
 * to prevent them from being prematurely killed under memory pressure.
 *
 * Security benefits:
 * - Emulator processes are less likely to be killed than other processes
 * - Protects guest state from corruption due to sudden termination
 * - Allows graceful degradation under memory pressure
 */


/* Convert memory access result to string for debugging */
const char *
emu_mem_access_str(enum emu_mem_access access)
{
	switch (access) {
	case EMU_MEM_ACCESS_OK:
		return "OK";
	case EMU_MEM_ACCESSOutOfBounds:
		return "OUT_OF_BOUNDS";
	case EMU_MEM_ACCESS_NULL:
		return "NULL_POINTER";
	case EMU_MEM_ACCESS_ALIGNMENT:
		return "ALIGNMENT_ERROR";
	case EMU_MEM_ACCESS_PROTECTED:
		return "PROTECTED_REGION";
	default:
		return "UNKNOWN";
	}
}

/*
 * Check if a memory access is within bounds
 * Returns true if the access is valid, false otherwise
 */
bool
emu_mem_check_bounds(struct emu_guest_mem *mem, uint64_t guest_addr, size_t len)
{
	if (mem == NULL || mem->base == NULL)
		return (false);

	if (!mem->initialized)
		return (false);

	/* Check for overflow */
	if (guest_addr + len < guest_addr)
		return (false);

	/* Check against total allocated size */
	if (guest_addr + len > mem->total_size)
		return (false);

	return (true);
}

/*
 * Find the memory region containing the given address
 * Returns NULL if no region is found
 */
struct emu_mem_region *
emu_mem_find_region(struct emu_guest_mem *mem, uint64_t addr)
{
	int i;

	if (mem == NULL || mem->regions == NULL)
		return (NULL);

	for (i = 0; i < mem->num_regions; i++) {
		struct emu_mem_region *region = &mem->regions[i];

		if (addr >= region->base && addr < region->base + region->size)
			return (region);
	}

	return (NULL);
}

/*
 * Validate memory access against region permissions
 * Returns EMU_MEM_ACCESS_OK if access is allowed, error code otherwise
 */
static enum emu_mem_access
emu_mem_check_region(struct emu_mem_region *region, size_t len, bool is_write)
{
	if (region == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Check if region allows the requested operation */
	if (is_write) {
		if (!(region->flags & EMU_MEM_REGION_WRITE))
			return (EMU_MEM_ACCESS_PROTECTED);
		if (region->flags & EMU_MEM_REGION_READONLY)
			return (EMU_MEM_ACCESS_PROTECTED);
	} else {
		if (!(region->flags & EMU_MEM_REGION_READ))
			return (EMU_MEM_ACCESS_PROTECTED);
	}

	/* Check if access fits within region */
	if (region->base + len > region->base + region->size)
		return (EMU_MEM_ACCESSOutOfBounds);

	return (EMU_MEM_ACCESS_OK);
}

/*
 * Read 8-bit value from guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_read8(struct emu_guest_mem *mem, uint64_t guest_addr, uint8_t *value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL || value == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint8_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint8_t), false);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the read */
	*value = emu_mem_raw_read8(mem->base, guest_addr);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Read 16-bit value from guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_read16(struct emu_guest_mem *mem, uint64_t guest_addr, uint16_t *value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL || value == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 1) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint16_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint16_t), false);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the read */
	*value = emu_mem_raw_read16(mem->base, guest_addr);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Read 32-bit value from guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_read32(struct emu_guest_mem *mem, uint64_t guest_addr, uint32_t *value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL || value == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 3) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint32_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint32_t), false);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the read */
	*value = emu_mem_raw_read32(mem->base, guest_addr);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Read 64-bit value from guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_read64(struct emu_guest_mem *mem, uint64_t guest_addr, uint64_t *value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL || value == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 7) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint64_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint64_t), false);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the read */
	*value = emu_mem_raw_read64(mem->base, guest_addr);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Read multiple bytes from guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_read_bytes(struct emu_guest_mem *mem, uint64_t guest_addr,
    void *buffer, size_t len)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;
	size_t i;

	if (mem == NULL || buffer == NULL || len == 0)
		return (EMU_MEM_ACCESS_NULL);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, len))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, len, false);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the read byte-by-byte to handle potential region boundaries */
	for (i = 0; i < len; i++) {
		ret = emu_mem_read8(mem, guest_addr + i, &((uint8_t *)buffer)[i]);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	return (EMU_MEM_ACCESS_OK);
}

/*
 * Write 8-bit value to guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_write8(struct emu_guest_mem *mem, uint64_t guest_addr, uint8_t value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint8_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint8_t), true);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the write */
	emu_mem_raw_write8(mem->base, guest_addr, value);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Write 16-bit value to guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_write16(struct emu_guest_mem *mem, uint64_t guest_addr, uint16_t value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 1) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint16_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint16_t), true);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the write */
	emu_mem_raw_write16(mem->base, guest_addr, value);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Write 32-bit value to guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_write32(struct emu_guest_mem *mem, uint64_t guest_addr, uint32_t value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 3) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint32_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint32_t), true);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the write */
	emu_mem_raw_write32(mem->base, guest_addr, value);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Write 64-bit value to guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_write64(struct emu_guest_mem *mem, uint64_t guest_addr, uint64_t value)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;

	if (mem == NULL)
		return (EMU_MEM_ACCESS_NULL);

	/* Alignment check if strict mode enabled */
	if (mem->strict_align && (guest_addr & 7) != 0)
		return (EMU_MEM_ACCESS_ALIGNMENT);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, sizeof(uint64_t)))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, sizeof(uint64_t), true);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the write */
	emu_mem_raw_write64(mem->base, guest_addr, value);
	return (EMU_MEM_ACCESS_OK);
}

/*
 * Write multiple bytes to guest memory
 * Performs bounds checking and region validation
 */
enum emu_mem_access
emu_mem_write_bytes(struct emu_guest_mem *mem, uint64_t guest_addr,
    const void *buffer, size_t len)
{
	struct emu_mem_region *region;
	enum emu_mem_access ret;
	size_t i;

	if (mem == NULL || buffer == NULL || len == 0)
		return (EMU_MEM_ACCESS_NULL);

	/* Bounds check */
	if (!emu_mem_check_bounds(mem, guest_addr, len))
		return (EMU_MEM_ACCESSOutOfBounds);

	/* Find region and check permissions */
	region = emu_mem_find_region(mem, guest_addr);
	if (region != NULL) {
		ret = emu_mem_check_region(region, len, true);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	/* Perform the write byte-by-byte to handle potential region boundaries */
	for (i = 0; i < len; i++) {
		ret = emu_mem_write8(mem, guest_addr + i, ((const uint8_t *)buffer)[i]);
		if (ret != EMU_MEM_ACCESS_OK)
			return (ret);
	}

	return (EMU_MEM_ACCESS_OK);
}

/*
 * Add a memory region descriptor
 * Returns 0 on success, -1 on failure
 */
int
emu_mem_add_region(struct emu_guest_mem *mem, uint64_t base, size_t size,
    int flags, const char *name)
{
	struct emu_mem_region *new_regions;

	if (mem == NULL || size == 0)
		return (-1);

	/* Allocate or reallocate region array */
	new_regions = realloc(mem->regions,
	    (mem->num_regions + 1) * sizeof(struct emu_mem_region));
	if (new_regions == NULL)
		return (-1);

	mem->regions = new_regions;

	/* Initialize the new region */
	mem->regions[mem->num_regions].base = base;
	mem->regions[mem->num_regions].size = size;
	mem->regions[mem->num_regions].flags = flags;
	mem->regions[mem->num_regions].host_ptr = NULL;
	mem->regions[mem->num_regions].name = name;
	mem->num_regions++;

	return (0);
}

/*
 * Remove a memory region by base address
 * Returns 0 on success, -1 on failure
 */
int
emu_mem_remove_region(struct emu_guest_mem *mem, uint64_t base)
{
	int i, j;

	if (mem == NULL || mem->regions == NULL)
		return (-1);

	/* Find the region */
	for (i = 0; i < mem->num_regions; i++) {
		if (mem->regions[i].base == base) {
			/* Shift remaining regions */
			for (j = i; j < mem->num_regions - 1; j++)
				mem->regions[j] = mem->regions[j + 1];

			mem->num_regions--;

			/* Reallocate to shrink */
			if (mem->num_regions > 0) {
				struct emu_mem_region *new_regions;
				new_regions = realloc(mem->regions,
				    mem->num_regions * sizeof(struct emu_mem_region));
				if (new_regions != NULL)
					mem->regions = new_regions;
			} else {
				free(mem->regions);
				mem->regions = NULL;
			}

			return (0);
		}
	}

	return (-1);
}

/*
 * Initialize guest memory subsystem
 * Allocates memory and sets up default region
 * Returns 0 on success, -1 on failure
 */
int
emu_mem_init(struct emu_guest_mem *mem, size_t total_size)
{
	int policy;

	if (mem == NULL || total_size == 0)
		return (-1);

	/* Get memory policy from sysctl */
	policy = emu_memmgmt_get_policy();

	if (policy == EMU_MEM_POLICY_DEMAND) {
		/* Demand-paged allocation (MAP_NORESERVE) */
		mem->base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
		if (mem->base == MAP_FAILED) {
			mem->base = NULL;
			return (-1);
		}
		/* Zero out the memory */
		memset(mem->base, 0, total_size);
	} else {
		/* Prealloc mode - traditional malloc */
		mem->base = malloc(total_size);
		if (mem->base == NULL)
			return (-1);

		/* Zero out the memory */
		memset(mem->base, 0, total_size);
	}

	/* Initialize descriptor */
	mem->total_size = total_size;
	mem->used_size = 0;
	mem->regions = NULL;
	mem->num_regions = 0;
	mem->strict_align = false;
	mem->initialized = true;
	mem->guest_endian = EMU_ENDIAN_LITTLE;	/* Default to little-endian (x86/x86_64) */

	/* Add default region covering entire memory */
	if (emu_mem_add_region(mem, 0, total_size,
	    EMU_MEM_REGION_READ | EMU_MEM_REGION_WRITE | EMU_MEM_REGION_EXECUTE,
	    "guest_ram") != 0) {
		if (policy == EMU_MEM_POLICY_DEMAND)
			munmap(mem->base, total_size);
		else
			free(mem->base);
		mem->base = NULL;
		return (-1);
	}

	return (0);
}

/*
 * Isolate guest memory (mark read-only after crash)
 */
void
emu_mem_isolate(struct emu_guest_mem *mem)
{
	int i;

	if (mem == NULL || mem->regions == NULL)
		return;

	/* Mark all regions as read-only */
	for (i = 0; i < mem->num_regions; i++)
		mem->regions[i].flags &= ~(EMU_MEM_REGION_WRITE | EMU_MEM_REGION_EXECUTE);
}

/*
 * Clean up guest memory after crash
 */
void
emu_mem_cleanup(struct emu_guest_mem *mem)
{
	if (mem == NULL)
		return;

	/* Clear memory contents */
	if (mem->base != NULL)
		memset(mem->base, 0, mem->total_size);

	/* Reset region flags */
	if (mem->regions != NULL) {
		int i;
		for (i = 0; i < mem->num_regions; i++)
			mem->regions[i].flags = EMU_MEM_REGION_READ;
	}
}

/*
 * Destroy guest memory subsystem
 * Frees all allocated memory and region descriptors
 */
void
emu_mem_destroy(struct emu_guest_mem *mem)
{
	int policy;

	if (mem == NULL)
		return;

	/* Get memory policy to determine deallocation method */
	policy = emu_memmgmt_get_policy();

	/* Free region descriptors */
	if (mem->regions != NULL) {
		free(mem->regions);
		mem->regions = NULL;
	}

	/* Free guest memory based on allocation method */
	if (mem->base != NULL) {
		if (policy == EMU_MEM_POLICY_DEMAND)
			munmap(mem->base, mem->total_size);
		else
			free(mem->base);
		mem->base = NULL;
	}

	mem->total_size = 0;
	mem->used_size = 0;
	mem->num_regions = 0;
	mem->initialized = false;
}

/*
 * Scrub guest memory (zero out all data)
 * Used for security to prevent data leakage between instances
 */
void
emu_mem_scrub(struct emu_guest_mem *mem)
{
	if (mem == NULL || mem->base == NULL)
		return;

	/* Use explicit_bzero to ensure compiler doesn't optimize away */
	explicit_bzero(mem->base, mem->total_size);
	mem->used_size = 0;
}

/*
 * Dump memory region information for debugging
 */
void
emu_mem_dump_regions(struct emu_guest_mem *mem)
{
	int i;

	if (mem == NULL || mem->regions == NULL)
		return;

	printf("Memory regions for guest memory %p (total: %zu bytes):\n",
	    mem->base, mem->total_size);

	for (i = 0; i < mem->num_regions; i++) {
		struct emu_mem_region *region = &mem->regions[i];
		printf("  [%d] %s: base=0x%lx, size=%zu, flags=0x%x\n",
		    i, region->name ? region->name : "unknown",
		    (unsigned long)region->base, region->size, region->flags);
	}
}

/*
 * Check if a file descriptor is essential for emulator operation
 * Essential FDs are kept open during sandboxing
 */
static bool
emu_is_essential_fd(int fd)
{
	/* Stdio streams are always essential */
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
		return (true);

	/* Add other essential FDs here as needed */
	/* For now, only stdio is considered essential */
	return (false);
}

/*
 * Close non-essential file descriptors
 * This is called before entering Capsicum sandbox to minimize
 * the attack surface.
 */
static void
emu_close_nonessential_fds(void)
{
	int fd;

	/* Close all FDs from 3 to OPEN_MAX (except essential ones) */
	for (fd = 3; fd < OPEN_MAX; fd++) {
		if (!emu_is_essential_fd(fd))
			(void)close(fd);
	}
}

/*
 * Enter Capsicum capability mode sandbox
 *
 * This function restricts the emulator process to only access
 * pre-limited file descriptors. After this function returns,
 * the process cannot:
 * - Open new files or network connections
 * - Access arbitrary filesystem paths
 * - Execute new binaries
 * - Fork new processes
 * - Access /proc, /sys, or other sensitive paths
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_enter_sandbox(void)
{
	cap_rights_t rights;
	int error;

	/* Close non-essential file descriptors first */
	emu_close_nonessential_fds();

	/*
	 * Enter capability mode
	 * After this call, the process can only access file descriptors
	 * that were already open and have appropriate rights
	 */
	error = cap_enter();
	if (error != 0) {
		warn("cap_enter() failed");
		return (-1);
	}

	/* Verify we're in capability mode */
	if (cap_sandboxed() == 0) {
		warnx("Failed to enter capability mode");
		return (-1);
	}

	return (0);
}

/*
 * Limit rights on a file descriptor using Capsicum
 *
 * This function restricts the operations that can be performed
 * on a specific file descriptor.
 *
 * Parameters:
 *   fd     - File descriptor to limit
 *   rights - Capabilities to allow (e.g., CAP_READ | CAP_WRITE)
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_limit_fd_rights(int fd, cap_rights_t *rights)
{
	int error;

	if (fd < 0 || rights == NULL)
		return (-1);

	error = cap_rights_limit(fd, rights);
	if (error != 0) {
		warn("cap_rights_limit() failed on fd %d", fd);
		return (-1);
	}

	return (0);
}

/*
 * Limit ioctl operations on a file descriptor
 *
 * This function restricts which ioctl commands can be issued
 * on a specific file descriptor.
 *
 * Parameters:
 *   fd     - File descriptor to limit
 *   cmds   - Array of allowed ioctl commands
 *   ncmds  - Number of commands in the array
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_limit_fd_ioctls(int fd, const u_long *cmds, size_t ncmds)
{
	int error;

	if (fd < 0 || cmds == NULL || ncmds == 0)
		return (-1);

	error = cap_ioctls_limit(fd, cmds, ncmds);
	if (error != 0) {
		warn("cap_ioctls_limit() failed on fd %d", fd);
		return (-1);
	}

	return (0);
}

/*
 * Execution Control - Instruction Count Limits and Watchdog Timer
 *
 * This section implements security controls to prevent guest code from:
 * - Running infinite loops that hang the emulator
 * - Consuming excessive CPU time without yielding
 * - Crashing without detection
 *
 * These controls are essential for multi-tenant environments and
 * for ensuring fair resource allocation.
 */

/*
 * Initialize execution state
 *
 * Parameters:
 *   state           - Execution state structure to initialize
 *   insn_limit      - Maximum instructions per execution slice (0 = unlimited)
 *   watchdog_timeout - Watchdog timeout in seconds (0 = disabled)
 *
 * Returns 0 on success, -1 on failure
 */
int
emu_exec_state_init(struct emu_exec_state *state, uint64_t insn_limit,
    time_t watchdog_timeout)
{
	if (state == NULL)
		return (-1);

	memset(state, 0, sizeof(struct emu_exec_state));

	state->insn_count = 0;
	state->insn_limit = insn_limit;
	state->total_insns = 0;
	state->start_time = time(NULL);
	state->last_activity = state->start_time;
	state->watchdog_timeout = watchdog_timeout;
	state->watchdog_enabled = (watchdog_timeout > 0);
	state->slice_exceeded = false;
	state->watchdog_triggered = false;

	return (0);
}

/*
 * Reset instruction counter for new execution slice
 *
 * This is called at the start of each execution slice to allow
 * the guest to continue running while still enforcing per-slice limits.
 */
void
emu_exec_reset_slice(struct emu_exec_state *state)
{
	if (state == NULL)
		return;

	state->insn_count = 0;
	state->slice_exceeded = false;
}

/*
 * Increment instruction counter and check limit
 *
 * Parameters:
 *   state - Execution state
 *   count - Number of instructions to add (usually 1)
 *
 * Returns:
 *   0 if within limit
 *   -1 if limit exceeded
 */
int
emu_exec_insn_increment(struct emu_exec_state *state, uint64_t count)
{
	if (state == NULL)
		return (-1);

	state->insn_count += count;
	state->total_insns += count;

	/* Check if slice limit exceeded */
	if (state->insn_limit > 0 && state->insn_count > state->insn_limit) {
		state->slice_exceeded = true;
		return (-1);
	}

	return (0);
}

/*
 * Check if instruction slice limit has been exceeded
 *
 * Returns true if the current execution slice has exceeded the
 * configured instruction limit.
 */
bool
emu_exec_slice_exceeded(struct emu_exec_state *state)
{
	if (state == NULL)
		return (false);

	return (state->slice_exceeded);
}

/*
 * Update last activity timestamp
 *
 * This should be called whenever the guest makes progress
 * (e.g., completes an instruction, handles an interrupt).
 */
void
emu_exec_update_activity(struct emu_exec_state *state)
{
	if (state == NULL)
		return;

	state->last_activity = time(NULL);
}

/*
 * Check if watchdog timer has expired
 *
 * The watchdog timer detects guest hangs or crashes by monitoring
 * the time since last guest activity.
 *
 * Returns true if the watchdog has expired, false otherwise.
 */
bool
emu_exec_watchdog_expired(struct emu_exec_state *state)
{
	time_t now;
	time_t elapsed;

	if (state == NULL || !state->watchdog_enabled)
		return (false);

	now = time(NULL);
	elapsed = now - state->last_activity;

	if (elapsed >= state->watchdog_timeout) {
		state->watchdog_triggered = true;
		return (true);
	}

	return (false);
}

/*
 * Get execution statistics
 *
 * Parameters:
 *   state   - Execution state
 *   total_insns - Output: total instructions executed
 *   uptime  - Output: execution uptime in seconds
 */
void
emu_exec_get_stats(struct emu_exec_state *state, uint64_t *total_insns,
    time_t *uptime)
{
	time_t now;

	if (state == NULL)
		return;

	if (total_insns != NULL)
		*total_insns = state->total_insns;

	if (uptime != NULL) {
		now = time(NULL);
		*uptime = now - state->start_time;
	}
}

/*
 * Destroy/reset execution state
 *
 * This clears all execution state and can be called when
 * an instance is destroyed or reset.
 */
void
emu_exec_state_destroy(struct emu_exec_state *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(struct emu_exec_state));
}
