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

/*
 * Emulation Framework - Endianness-Aware Memory Access
 *
 * This module provides endianness-aware memory access primitives for
 * cross-architecture emulation. It supports both little-endian and
 * big-endian guest architectures on any host endianness.
 */

#ifndef _EMU_MEM_H_
#define	_EMU_MEM_H_

#include <sys/types.h>
#include <sys/endian.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*
 * Guest endianness enumeration
 */
enum emu_endian {
	EMU_ENDIAN_UNKNOWN = 0,
	EMU_ENDIAN_LITTLE,	/* Little-endian (x86, ARM64, RISC-V LE) */
	EMU_ENDIAN_BIG		/* Big-endian (PowerPC, ARM BE, RISC-V BE) */
};

/*
 * Raw memory access helpers for bounds-checked operations
 * Used by emu_engine.c for safe guest memory access
 */
static inline uint8_t
emu_mem_raw_read8(void *base, uint64_t offset)
{
	return (*(uint8_t *)((uint8_t *)base + offset));
}

static inline uint16_t
emu_mem_raw_read16(void *base, uint64_t offset)
{
	uint16_t val;
	memcpy(&val, (uint8_t *)base + offset, sizeof(val));
	return (val);
}

static inline uint32_t
emu_mem_raw_read32(void *base, uint64_t offset)
{
	uint32_t val;
	memcpy(&val, (uint8_t *)base + offset, sizeof(val));
	return (val);
}

static inline uint64_t
emu_mem_raw_read64(void *base, uint64_t offset)
{
	uint64_t val;
	memcpy(&val, (uint8_t *)base + offset, sizeof(val));
	return (val);
}

static inline void
emu_mem_raw_write8(void *base, uint64_t offset, uint8_t value)
{
	(*(uint8_t *)((uint8_t *)base + offset)) = value;
}

static inline void
emu_mem_raw_write16(void *base, uint64_t offset, uint16_t value)
{
	memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static inline void
emu_mem_raw_write32(void *base, uint64_t offset, uint32_t value)
{
	memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

static inline void
emu_mem_raw_write64(void *base, uint64_t offset, uint64_t value)
{
	memcpy((uint8_t *)base + offset, &value, sizeof(value));
}

/*
 * Endianness-aware memory read operations
 *
 * These functions read values from guest memory and convert them to
 * host byte order based on the guest's endianness.
 */

/* Little-endian read operations */
static inline uint16_t
emu_mem_read_le16(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return (p[0] | (p[1] << 8));
}

static inline uint32_t
emu_mem_read_le32(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static inline uint64_t
emu_mem_read_le64(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return ((uint64_t)p[0] | ((uint64_t)p[1] << 8) |
	    ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
	    ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
	    ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56));
}

/* Big-endian read operations */
static inline uint16_t
emu_mem_read_be16(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return ((p[0] << 8) | p[1]);
}

static inline uint32_t
emu_mem_read_be32(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static inline uint64_t
emu_mem_read_be64(const void *src)
{
	const uint8_t *p = (const uint8_t *)src;
	return (((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
	    ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
	    ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
	    ((uint64_t)p[6] << 8) | (uint64_t)p[7]);
}

/*
 * Endianness-aware memory write operations
 *
 * These functions write values to guest memory, converting from host
 * byte order to the guest's endianness.
 */

/* Little-endian write operations */
static inline void
emu_mem_write_le16(void *dst, uint16_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = val & 0xff;
	p[1] = (val >> 8) & 0xff;
}

static inline void
emu_mem_write_le32(void *dst, uint32_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = val & 0xff;
	p[1] = (val >> 8) & 0xff;
	p[2] = (val >> 16) & 0xff;
	p[3] = (val >> 24) & 0xff;
}

static inline void
emu_mem_write_le64(void *dst, uint64_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = val & 0xff;
	p[1] = (val >> 8) & 0xff;
	p[2] = (val >> 16) & 0xff;
	p[3] = (val >> 24) & 0xff;
	p[4] = (val >> 32) & 0xff;
	p[5] = (val >> 40) & 0xff;
	p[6] = (val >> 48) & 0xff;
	p[7] = (val >> 56) & 0xff;
}

/* Big-endian write operations */
static inline void
emu_mem_write_be16(void *dst, uint16_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = (val >> 8) & 0xff;
	p[1] = val & 0xff;
}

static inline void
emu_mem_write_be32(void *dst, uint32_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = (val >> 24) & 0xff;
	p[1] = (val >> 16) & 0xff;
	p[2] = (val >> 8) & 0xff;
	p[3] = val & 0xff;
}

static inline void
emu_mem_write_be64(void *dst, uint64_t val)
{
	uint8_t *p = (uint8_t *)dst;
	p[0] = (val >> 56) & 0xff;
	p[1] = (val >> 48) & 0xff;
	p[2] = (val >> 40) & 0xff;
	p[3] = (val >> 32) & 0xff;
	p[4] = (val >> 24) & 0xff;
	p[5] = (val >> 16) & 0xff;
	p[6] = (val >> 8) & 0xff;
	p[7] = val & 0xff;
}

/*
 * Generic endianness-aware accessors
 *
 * These functions select the appropriate read/write operation based
 * on the guest's endianness.
 */

static inline uint16_t
emu_mem_read16_byEndian(const void *src, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		return (emu_mem_read_be16(src));
	else
		return (emu_mem_read_le16(src));
}

static inline uint32_t
emu_mem_read32_byEndian(const void *src, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		return (emu_mem_read_be32(src));
	else
		return (emu_mem_read_le32(src));
}

static inline uint64_t
emu_mem_read64_byEndian(const void *src, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		return (emu_mem_read_be64(src));
	else
		return (emu_mem_read_le64(src));
}

static inline void
emu_mem_write16_byEndian(void *dst, uint16_t val, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		emu_mem_write_be16(dst, val);
	else
		emu_mem_write_le16(dst, val);
}

static inline void
emu_mem_write32_byEndian(void *dst, uint32_t val, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		emu_mem_write_be32(dst, val);
	else
		emu_mem_write_le32(dst, val);
}

static inline void
emu_mem_write64_byEndian(void *dst, uint64_t val, enum emu_endian endian)
{
	if (endian == EMU_ENDIAN_BIG)
		emu_mem_write_be64(dst, val);
	else
		emu_mem_write_le64(dst, val);
}

/*
 * Host endianness detection
 */
static inline enum emu_endian
emu_host_endian(void)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	return (EMU_ENDIAN_LITTLE);
#elif BYTE_ORDER == BIG_ENDIAN
	return (EMU_ENDIAN_BIG);
#else
#error "Unknown host endianness"
#endif
}

/*
 * Detect endianness from architecture name
 */
static inline enum emu_endian
emu_arch_endian(const char *arch)
{
	/* Most architectures have fixed endianness */
	if (strcmp(arch, "amd64") == 0 || strcmp(arch, "x86_64") == 0)
		return (EMU_ENDIAN_LITTLE);
	if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86") == 0)
		return (EMU_ENDIAN_LITTLE);
	if (strcmp(arch, "arm64") == 0 || strcmp(arch, "aarch64") == 0)
		return (EMU_ENDIAN_LITTLE);	/* ARM64 is LE by default */
	if (strcmp(arch, "arm") == 0 || strcmp(arch, "armv7") == 0)
		return (EMU_ENDIAN_LITTLE);	/* ARM is typically LE */
	if (strcmp(arch, "riscv") == 0 || strcmp(arch, "riscv64") == 0)
		return (EMU_ENDIAN_LITTLE);	/* RISC-V is typically LE */
	if (strcmp(arch, "powerpc") == 0 || strcmp(arch, "ppc") == 0)
		return (EMU_ENDIAN_BIG);	/* PowerPC is typically BE */
	if (strcmp(arch, "powerpc64") == 0 || strcmp(arch, "ppc64") == 0)
		return (EMU_ENDIAN_BIG);
	
	/* Unknown architecture - assume little-endian (most common) */
	return (EMU_ENDIAN_LITTLE);
}

#endif /* !_EMU_MEM_H_ */
