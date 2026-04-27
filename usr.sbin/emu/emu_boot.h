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

#ifndef _EMU_BOOT_H_
#define	_EMU_BOOT_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <elf.h>

#include "emu_engine.h"

/*
 * Emulation Framework - Secure ELF Boot Loader
 *
 * This module provides secure ELF binary loading with comprehensive
 * validation to prevent loading malformed or malicious binaries.
 *
 * Security considerations:
 * - All ELF headers are validated before use
 * - Program header bounds are checked against file size
 * - Segment addresses and sizes are validated
 * - No integer overflows in calculations
 * - Memory regions are checked for overlaps
 */

/* ELF validation result codes */
enum emu_elf_result {
	EMU_ELF_OK = 0,
	EMU_ELF_ERR_NULL,			/* NULL pointer */
	EMU_ELF_ERR_MAGIC,			/* Invalid ELF magic number */
	EMU_ELF_ERR_CLASS,			/* Invalid ELF class (32/64-bit) */
	EMU_ELF_ERR_ENDIAN,			/* Invalid endianness */
	EMU_ELF_ERR_VERSION,			/* Invalid ELF version */
	EMU_ELF_ERR_TYPE,			/* Invalid ELF type */
	EMU_ELF_ERR_MACHINE,			/* Invalid machine type */
	EMU_ELF_ERR_ENTRY,			/* Invalid entry point */
	EMU_ELF_ERR_PHOFF,			/* Invalid program header offset */
	EMU_ELF_ERR_PHENTSIZE,		/* Invalid program header entry size */
	EMU_ELF_ERR_PHNUM,			/* Invalid number of program headers */
	EMU_ELF_ERR_OVERFLOW,			/* Integer overflow detected */
	EMU_ELF_ERR_FILE_SIZE,		/* File size validation failed */
	EMU_ELF_ERR_SEGMENT_OVERLAP,	/* Segments overlap */
	EMU_ELF_ERR_SEGMENT_BOUNDS,	/* Segment outside valid range */
	EMU_ELF_ERR_SEGMENT_ALIGN,	/* Segment alignment violation */
	EMU_ELF_ERR_MEMORY,			/* Insufficient memory for load */
	EMU_ELF_ERR_IO,				/* I/O error reading file */
	EMU_ELF_ERR_UNSUPPORTED		/* Unsupported ELF feature */
};

/* ELF binary information */
struct emu_elf_info {
	uint8_t		class;			/* ELF class (32/64-bit) */
	uint8_t		endian;			/* Endianness */
	uint16_t	type;			/* ELF type (EXEC, DYN, etc.) */
	uint16_t	machine;		/* Machine type */
	uint64_t	entry;			/* Entry point address */
	uint64_t	phoff;			/* Program header offset */
	uint32_t	phentsize;		/* Program header entry size */
	uint32_t	phnum;			/* Number of program headers */
	size_t		file_size;		/* Size of ELF file */
};

/* Program segment descriptor */
struct emu_elf_segment {
	uint64_t	vaddr;			/* Virtual address in guest */
	uint64_t	offset;			/* Offset in ELF file */
	uint64_t	filesz;			/* Size in file */
	uint64_t	memsz;			/* Size in memory */
	uint64_t	flags;			/* Segment flags (R/W/X) */
	uint64_t	align;			/* Alignment requirement */
	bool		loadable;		/* PT_LOAD segment */
};

/*
 * ELF validation API
 */

/* Convert result code to string */
const char *emu_elf_result_str(enum emu_elf_result result);

/* Validate ELF file and extract metadata */
enum emu_elf_result emu_elf_validate(const char *path, struct emu_elf_info *info);

/* Validate ELF file from buffer */
enum emu_elf_result emu_elf_validate_buffer(const void *buf, size_t len,
    struct emu_elf_info *info);

/*
 * Program header parsing
 */

/* Parse program headers from ELF file */
enum emu_elf_result emu_elf_parse_phdrs(const char *path,
    const struct emu_elf_info *info, struct emu_elf_segment **segments,
    int *num_segments);

/* Parse program headers from buffer */
enum emu_elf_result emu_elf_parse_phdrs_buffer(const void *buf, size_t len,
    const struct emu_elf_info *info, struct emu_elf_segment **segments,
    int *num_segments);

/*
 * Segment validation
 */

/* Check if segments overlap */
bool emu_elf_segments_overlap(const struct emu_elf_segment *seg1,
    const struct emu_elf_segment *seg2);

/* Validate all segments for overlaps and bounds */
enum emu_elf_result emu_elf_validate_segments(const struct emu_elf_segment *segments,
    int num_segments, uint64_t mem_size);

/*
 * ELF loading
 */

/* Load ELF binary into guest memory */
enum emu_elf_result emu_elf_load(const char *path, struct emu_guest_mem *mem,
    uint64_t *entry_point);

/* Load ELF binary from buffer into guest memory */
enum emu_elf_result emu_elf_load_buffer(const void *buf, size_t len,
    struct emu_guest_mem *mem, uint64_t *entry_point);

/*
 * Utility functions
 */

/* Get ELF type string */
const char *emu_elf_type_str(uint16_t type);

/* Get ELF machine string */
const char *emu_elf_machine_str(uint16_t machine);

/* Check if machine type is supported */
bool emu_elf_machine_supported(uint16_t machine);

#endif /* !_EMU_BOOT_H_ */
