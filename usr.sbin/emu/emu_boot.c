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
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>

#include "emu_boot.h"
#include "emu_engine.h"
#include "emu.h"

/*
 * Emulation Framework - Secure ELF Boot Loader Implementation
 *
 * This module implements secure ELF binary loading with comprehensive
 * validation to prevent loading malformed or malicious binaries.
 *
 * Security considerations:
 * - All ELF headers are validated before use
 * - Program header bounds are checked against file size
 * - Segment addresses and sizes are validated
 * - No integer overflows in calculations
 * - Memory regions are checked for overlaps
 */

/* ELF magic number */
static const uint8_t elf_magic[4] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };

/*
 * Convert ELF result code to string for debugging
 */
const char *
emu_elf_result_str(enum emu_elf_result result)
{
	switch (result) {
	case EMU_ELF_OK:
		return "OK";
	case EMU_ELF_ERR_NULL:
		return "NULL pointer";
	case EMU_ELF_ERR_MAGIC:
		return "Invalid ELF magic number";
	case EMU_ELF_ERR_CLASS:
		return "Invalid ELF class";
	case EMU_ELF_ERR_ENDIAN:
		return "Invalid endianness";
	case EMU_ELF_ERR_VERSION:
		return "Invalid ELF version";
	case EMU_ELF_ERR_TYPE:
		return "Invalid ELF type";
	case EMU_ELF_ERR_MACHINE:
		return "Invalid machine type";
	case EMU_ELF_ERR_ENTRY:
		return "Invalid entry point";
	case EMU_ELF_ERR_PHOFF:
		return "Invalid program header offset";
	case EMU_ELF_ERR_PHENTSIZE:
		return "Invalid program header entry size";
	case EMU_ELF_ERR_PHNUM:
		return "Invalid number of program headers";
	case EMU_ELF_ERR_OVERFLOW:
		return "Integer overflow detected";
	case EMU_ELF_ERR_FILE_SIZE:
		return "File size validation failed";
	case EMU_ELF_ERR_SEGMENT_OVERLAP:
		return "Segments overlap";
	case EMU_ELF_ERR_SEGMENT_BOUNDS:
		return "Segment outside valid range";
	case EMU_ELF_ERR_SEGMENT_ALIGN:
		return "Segment alignment violation";
	case EMU_ELF_ERR_MEMORY:
		return "Insufficient memory for load";
	case EMU_ELF_ERR_IO:
		return "I/O error reading file";
	case EMU_ELF_ERR_UNSUPPORTED:
		return "Unsupported ELF feature";
	default:
		return "Unknown error";
	}
}

/*
 * Get ELF type string
 */
const char *
emu_elf_type_str(uint16_t type)
{
	switch (type) {
	case ET_NONE:
		return "NONE";
	case ET_REL:
		return "REL (Relocatable)";
	case ET_EXEC:
		return "EXEC (Executable)";
	case ET_DYN:
		return "DYN (Shared object)";
	case ET_CORE:
		return "CORE (Core file)";
	default:
		return "Unknown";
	}
}

/*
 * Get ELF machine type string
 */
const char *
emu_elf_machine_str(uint16_t machine)
{
	switch (machine) {
	case EM_NONE:
		return "None";
	case EM_386:
		return "Intel 80386";
	case EM_X86_64:
		return "AMD x86-64";
	case EM_ARM:
		return "ARM";
	case EM_AARCH64:
		return "AArch64";
	case EM_PPC:
		return "PowerPC";
	case EM_PPC64:
		return "PowerPC64";
	case EM_RISCV:
		return "RISC-V";
	default:
		return "Unknown";
	}
}

/*
 * Check if machine type is supported by the emulator
 */
bool
emu_elf_machine_supported(uint16_t machine)
{
	switch (machine) {
	case EM_386:
	case EM_X86_64:
	case EM_ARM:
	case EM_AARCH64:
	case EM_PPC:
	case EM_PPC64:
	case EM_RISCV:
		return (true);
	default:
		return (false);
	}
}

/*
 * Check for addition overflow
 * Returns true if overflow would occur
 */
static bool
check_add_overflow(uint64_t a, uint64_t b, uint64_t *result)
{
	*result = a + b;
	return (*result < a);
}

/*
 * Validate ELF header from buffer
 * Returns EMU_ELF_OK on success, error code on failure
 */
static enum emu_elf_result
emu_elf_validate_header(const void *buf, size_t len, struct emu_elf_info *info)
{
	const Elf64_Ehdr *ehdr;

	if (buf == NULL || info == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Minimum size check */
	if (len < sizeof(Elf64_Ehdr))
		return (EMU_ELF_ERR_FILE_SIZE);

	ehdr = (const Elf64_Ehdr *)buf;

	/* Magic number check */
	if (memcmp(ehdr->e_ident, elf_magic, 4) != 0)
		return (EMU_ELF_ERR_MAGIC);

	/* ELF class check (must be 32 or 64-bit) */
	if (ehdr->e_ident[EI_CLASS] != ELFCLASS32 &&
	    ehdr->e_ident[EI_CLASS] != ELFCLASS64)
		return (EMU_ELF_ERR_CLASS);

	/* Endianness check */
	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB &&
	    ehdr->e_ident[EI_DATA] != ELFDATA2MSB)
		return (EMU_ELF_ERR_ENDIAN);

	/* ELF version check */
	if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
		return (EMU_ELF_ERR_VERSION);

	/* ELF type check (must be EXEC, DYN, or REL) */
	if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN &&
	    ehdr->e_type != ET_REL)
		return (EMU_ELF_ERR_TYPE);

	/* Machine type check */
	if (!emu_elf_machine_supported(ehdr->e_machine))
		return (EMU_ELF_ERR_MACHINE);

	/* Entry point validation */
	if (ehdr->e_entry == 0 && ehdr->e_type != ET_REL)
		return (EMU_ELF_ERR_ENTRY);

	/* Program header offset validation */
	if (ehdr->e_phoff == 0 || ehdr->e_phoff >= len)
		return (EMU_ELF_ERR_PHOFF);

	/* Program header entry size validation */
	if (ehdr->e_phentsize != sizeof(Elf64_Phdr) &&
	    ehdr->e_phentsize != sizeof(Elf32_Phdr))
		return (EMU_ELF_ERR_PHENTSIZE);

	/* Program header count validation */
	if (ehdr->e_phnum == 0 || ehdr->e_phnum > 65535)
		return (EMU_ELF_ERR_PHNUM);

	/* Check for overflow in program headers size calculation */
	uint64_t phdrs_size;
	if (check_add_overflow(ehdr->e_phoff,
	    (uint64_t)ehdr->e_phentsize * ehdr->e_phnum, &phdrs_size))
		return (EMU_ELF_ERR_OVERFLOW);

	if (phdrs_size > len)
		return (EMU_ELF_ERR_FILE_SIZE);

	/* Fill in info structure */
	info->class = ehdr->e_ident[EI_CLASS];
	info->endian = ehdr->e_ident[EI_DATA];
	info->type = ehdr->e_type;
	info->machine = ehdr->e_machine;
	info->entry = ehdr->e_entry;
	info->phoff = ehdr->e_phoff;
	info->phentsize = ehdr->e_phentsize;
	info->phnum = ehdr->e_phnum;
	info->file_size = len;

	return (EMU_ELF_OK);
}

/*
 * Validate ELF file from buffer
 */
enum emu_elf_result
emu_elf_validate_buffer(const void *buf, size_t len, struct emu_elf_info *info)
{
	return (emu_elf_validate_header(buf, len, info));
}

/*
 * Validate ELF file from disk
 */
enum emu_elf_result
emu_elf_validate(const char *path, struct emu_elf_info *info)
{
	struct stat st;
	void *buf;
	int fd;
	enum emu_elf_result result;

	if (path == NULL || info == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Open file */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (EMU_ELF_ERR_IO);

	/* Get file size */
	if (fstat(fd, &st) < 0) {
		close(fd);
		return (EMU_ELF_ERR_IO);
	}

	if ((size_t)st.st_size < sizeof(Elf64_Ehdr)) {
		close(fd);
		return (EMU_ELF_ERR_FILE_SIZE);
	}

	/* Map file into memory */
	buf = malloc(st.st_size);
	if (buf == NULL) {
		close(fd);
		return (EMU_ELF_ERR_MEMORY);
	}

	if (read(fd, buf, st.st_size) != (ssize_t)st.st_size) {
		free(buf);
		close(fd);
		return (EMU_ELF_ERR_IO);
	}

	close(fd);

	/* Validate header */
	result = emu_elf_validate_header(buf, st.st_size, info);

	free(buf);
	return (result);
}

/*
 * Parse program headers from buffer
 */
enum emu_elf_result
emu_elf_parse_phdrs_buffer(const void *buf, size_t len,
    const struct emu_elf_info *info, struct emu_elf_segment **segments,
    int *num_segments)
{
	const Elf64_Phdr *phdr;
	struct emu_elf_segment *segs;
	int i;

	if (buf == NULL || info == NULL || segments == NULL || num_segments == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Allocate segment array */
	segs = calloc(info->phnum, sizeof(struct emu_elf_segment));
	if (segs == NULL)
		return (EMU_ELF_ERR_MEMORY);

	/* Parse each program header */
	for (i = 0; i < (int)info->phnum; i++) {
		const uint8_t *phdr_ptr = (const uint8_t *)buf + info->phoff +
		    i * info->phentsize;
		Elf64_Phdr phdr_copy;
		memcpy(&phdr_copy, phdr_ptr, sizeof(phdr_copy));
		phdr = &phdr_copy;

		segs[i].vaddr = phdr->p_vaddr;
		segs[i].offset = phdr->p_offset;
		segs[i].filesz = phdr->p_filesz;
		segs[i].memsz = phdr->p_memsz;
		segs[i].flags = phdr->p_flags;
		segs[i].align = phdr->p_align;
		segs[i].loadable = (phdr->p_type == PT_LOAD);

		/* Validate segment bounds */
		uint64_t end_offset;
		if (check_add_overflow(segs[i].offset, segs[i].filesz, &end_offset)) {
			free(segs);
			return (EMU_ELF_ERR_OVERFLOW);
		}

		if (end_offset > len) {
			free(segs);
			return (EMU_ELF_ERR_FILE_SIZE);
		}

		uint64_t end_vaddr;
		if (check_add_overflow(segs[i].vaddr, segs[i].memsz, &end_vaddr)) {
			free(segs);
			return (EMU_ELF_ERR_OVERFLOW);
		}
	}

	*segments = segs;
	*num_segments = info->phnum;

	return (EMU_ELF_OK);
}

/*
 * Parse program headers from file
 */
enum emu_elf_result
emu_elf_parse_phdrs(const char *path, const struct emu_elf_info *info,
    struct emu_elf_segment **segments, int *num_segments)
{
	void *buf;
	int fd;
	enum emu_elf_result result;

	if (path == NULL || info == NULL || segments == NULL || num_segments == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Open and read file */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (EMU_ELF_ERR_IO);

	buf = malloc(info->file_size);
	if (buf == NULL) {
		close(fd);
		return (EMU_ELF_ERR_MEMORY);
	}

	if (read(fd, buf, info->file_size) != (ssize_t)info->file_size) {
		free(buf);
		close(fd);
		return (EMU_ELF_ERR_IO);
	}

	close(fd);

	/* Parse program headers */
	result = emu_elf_parse_phdrs_buffer(buf, info->file_size, info,
	    segments, num_segments);

	free(buf);
	return (result);
}

/*
 * Check if two segments overlap
 */
bool
emu_elf_segments_overlap(const struct emu_elf_segment *seg1,
    const struct emu_elf_segment *seg2)
{
	uint64_t end1, end2;

	if (!seg1->loadable || !seg2->loadable)
		return (false);

	/* Calculate end addresses */
	if (check_add_overflow(seg1->vaddr, seg1->memsz, &end1) ||
	    check_add_overflow(seg2->vaddr, seg2->memsz, &end2))
		return (true); /* Treat overflow as overlap for safety */

	/* Check for overlap */
	return (seg1->vaddr < end2 && seg2->vaddr < end1);
}

/*
 * Validate all segments for overlaps and bounds
 */
enum emu_elf_result
emu_elf_validate_segments(const struct emu_elf_segment *segments,
    int num_segments, uint64_t mem_size)
{
	int i, j;

	if (segments == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Check each pair of segments for overlaps */
	for (i = 0; i < num_segments; i++) {
		if (!segments[i].loadable)
			continue;

		/* Check segment bounds */
		uint64_t end_vaddr;
		if (check_add_overflow(segments[i].vaddr, segments[i].memsz, &end_vaddr))
			return (EMU_ELF_ERR_OVERFLOW);

		if (end_vaddr > mem_size)
			return (EMU_ELF_ERR_SEGMENT_BOUNDS);

		/* Check alignment */
		if (segments[i].align > 0 &&
		    (segments[i].vaddr & (segments[i].align - 1)) != 0)
			return (EMU_ELF_ERR_SEGMENT_ALIGN);

		/* Check against all other segments */
		for (j = i + 1; j < num_segments; j++) {
			if (!segments[j].loadable)
				continue;

			if (emu_elf_segments_overlap(&segments[i], &segments[j]))
				return (EMU_ELF_ERR_SEGMENT_OVERLAP);
		}
	}

	return (EMU_ELF_OK);
}

/*
 * Load ELF binary from buffer into guest memory
 */
enum emu_elf_result
emu_elf_load_buffer(const void *buf, size_t len, struct emu_guest_mem *mem,
    uint64_t *entry_point)
{
	struct emu_elf_info info;
	struct emu_elf_segment *segments = NULL;
	int num_segments;
	enum emu_elf_result result;
	int i;

	if (buf == NULL || mem == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Validate ELF header */
	result = emu_elf_validate_header(buf, len, &info);
	if (result != EMU_ELF_OK)
		return (result);

	/* Parse program headers */
	result = emu_elf_parse_phdrs_buffer(buf, len, &info, &segments,
	    &num_segments);
	if (result != EMU_ELF_OK)
		return (result);

	/* Validate segments */
	result = emu_elf_validate_segments(segments, num_segments, mem->total_size);
	if (result != EMU_ELF_OK) {
		free(segments);
		return (result);
	}

	/* Load each PT_LOAD segment */
	for (i = 0; i < num_segments; i++) {
		if (!segments[i].loadable)
			continue;

		/* Copy segment data to guest memory */
		if (segments[i].filesz > 0) {
			if (emu_mem_write_bytes(mem, segments[i].vaddr,
			    (const uint8_t *)buf + segments[i].offset,
			    segments[i].filesz) != EMU_MEM_ACCESS_OK) {
				free(segments);
				return (EMU_ELF_ERR_MEMORY);
			}
		}

		/* Zero out BSS (memsz > filesz) */
		if (segments[i].memsz > segments[i].filesz) {
			size_t bss_size = segments[i].memsz - segments[i].filesz;
			uint8_t *zero_buf = calloc(1, bss_size);
			if (zero_buf == NULL) {
				free(segments);
				return (EMU_ELF_ERR_MEMORY);
			}

			if (emu_mem_write_bytes(mem,
			    segments[i].vaddr + segments[i].filesz,
			    zero_buf, bss_size) != EMU_MEM_ACCESS_OK) {
				free(zero_buf);
				free(segments);
				return (EMU_ELF_ERR_MEMORY);
			}
			free(zero_buf);
		}
	}

	/* Set entry point */
	if (entry_point != NULL)
		*entry_point = info.entry;

	free(segments);
	return (EMU_ELF_OK);
}

/*
 * Load ELF binary from file into guest memory
 */
enum emu_elf_result
emu_elf_load(const char *path, struct emu_guest_mem *mem, uint64_t *entry_point)
{
	void *buf;
	int fd;
	enum emu_elf_result result;
	struct stat st;

	if (path == NULL || mem == NULL)
		return (EMU_ELF_ERR_NULL);

	/* Open file */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (EMU_ELF_ERR_IO);

	/* Get file size */
	if (fstat(fd, &st) < 0) {
		close(fd);
		return (EMU_ELF_ERR_IO);
	}

	/* Read file into memory */
	buf = malloc(st.st_size);
	if (buf == NULL) {
		close(fd);
		return (EMU_ELF_ERR_MEMORY);
	}

	if (read(fd, buf, st.st_size) != (ssize_t)st.st_size) {
		free(buf);
		close(fd);
		return (EMU_ELF_ERR_IO);
	}

	close(fd);

	/* Load ELF from buffer */
	result = emu_elf_load_buffer(buf, st.st_size, mem, entry_point);

	free(buf);
	return (result);
}
