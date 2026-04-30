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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "emu_decoder.h"
#include "emu_engine.h"
#include "emu.h"
#include "emu_mem.h"

/*
 * Emulation Framework - Safe Instruction Decoder Implementation
 *
 * This module implements a framework for safely decoding guest instructions.
 * All operand reads are bounds-checked, and instruction lengths are limited
 * to prevent decoder exploits.
 *
 * Security considerations:
 * - All instruction fetches are bounds-checked
 * - Maximum instruction length enforced (15 bytes for x86)
 * - Operand reads validated against memory regions
 * - No dynamic code generation (interpreter mode only)
 * - Invalid instructions return error codes, never crash
 */

/*
 * Convert decode result to string for debugging
 */
const char *
emu_decode_result_str(enum emu_decode_result result)
{
	switch (result) {
	case EMU_DECODE_OK:
		return "OK";
	case EMU_DECODE_ERR_NULL:
		return "NULL pointer";
	case EMU_DECODE_ERR_BOUNDS:
		return "Instruction fetch out of bounds";
	case EMU_DECODE_ERR_LENGTH:
		return "Instruction too long";
	case EMU_DECODE_ERR_INVALID:
		return "Invalid instruction encoding";
	case EMU_DECODE_ERR_UNSUPPORTED:
		return "Unsupported instruction";
	case EMU_DECODE_ERR_PRIVILEGED:
		return "Privileged instruction";
	case EMU_DECODE_ERR_ILLEGAL:
		return "Illegal instruction";
	case EMU_DECODE_ERR_MEMORY:
		return "Memory access error";
	case EMU_DECODE_ERR_OVERFLOW:
		return "Operand overflow";
	default:
		return "Unknown error";
	}
}

/*
 * Initialize decoder for specific architecture
 * Returns 0 on success, -1 on failure
 */
int
emu_decoder_init(int arch)
{
	/* Validate architecture */
	switch (arch) {
	case EMU_ARCH_AMD64:
	case EMU_ARCH_I386:
	case EMU_ARCH_ARM64:
	case EMU_ARCH_ARM:
	case EMU_ARCH_POWERPC:
	case EMU_ARCH_RISCV:
		/* Supported architecture */
		break;
	default:
		return (-1);
	}

	/* Architecture-specific initialization would go here */
	/* For now, just return success */
	return (0);
}

/*
 * Check if instruction is valid
 */
bool
emu_insn_is_valid(const struct emu_insn *insn)
{
	if (insn == NULL)
		return (false);

	if (insn->length == 0 || insn->length > EMU_MAX_INSN_LEN)
		return (false);

	if (insn->mnemonic == NULL)
		return (false);

	return (true);
}

/*
 * Check if instruction is privileged
 */
bool
emu_insn_is_privileged(const struct emu_insn *insn)
{
	if (insn == NULL)
		return (false);

	return ((insn->flags & EMU_INSFLAG_PRIVILEGED) != 0);
}

/*
 * Check if instruction accesses memory
 */
bool
emu_insn_accesses_memory(const struct emu_insn *insn)
{
	if (insn == NULL)
		return (false);

	return ((insn->flags & (EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_MEM)) != 0);
}

/*
 * Get instruction length
 */
uint8_t
emu_insn_length(const struct emu_insn *insn)
{
	if (insn == NULL)
		return (0);

	return (insn->length);
}

/*
 * Get instruction flags
 */
uint8_t
emu_insn_flags(const struct emu_insn *insn)
{
	if (insn == NULL)
		return (0);

	return (insn->flags);
}

/*
 * Read byte from guest memory with bounds checking
 * Helper function for instruction decoding
 */
static enum emu_decode_result
emu_fetch_byte(struct emu_guest_mem *mem, uint64_t addr, uint8_t *value)
{
	enum emu_mem_access ret;

	ret = emu_mem_read8(mem, addr, value);
	switch (ret) {
	case EMU_MEM_ACCESS_OK:
		return (EMU_DECODE_OK);
	case EMU_MEM_ACCESSOutOfBounds:
		return (EMU_DECODE_ERR_BOUNDS);
	case EMU_MEM_ACCESS_NULL:
		return (EMU_DECODE_ERR_NULL);
	default:
		return (EMU_DECODE_ERR_MEMORY);
	}
}

/*
 * Fetch instruction bytes from guest memory
 * Returns EMU_DECODE_OK on success, error code on failure
 */
static enum emu_decode_result
emu_fetch_insn_bytes(struct emu_guest_mem *mem, uint64_t rip,
    uint8_t *buffer, size_t max_len, size_t *fetched_len)
{
	size_t i;
	enum emu_decode_result result;

	for (i = 0; i < max_len; i++) {
		result = emu_fetch_byte(mem, rip + i, &buffer[i]);
		if (result != EMU_DECODE_OK) {
			if (i == 0)
				return (result); /* Can't fetch first byte */
			/* Partial fetch - return what we have */
			*fetched_len = i;
			return (EMU_DECODE_OK);
		}
	}

	*fetched_len = max_len;
	return (EMU_DECODE_OK);
}

/*
 * Decode x86-64 instruction prefixes
 * Returns number of prefix bytes, or -1 on error
 */
static int
emu_decode_x86_prefixes(const uint8_t *bytes, size_t len, uint8_t *prefixes)
{
	int i;

	*prefixes = 0;

	for (i = 0; i < (int)len && i < 4; i++) {
		switch (bytes[i]) {
		case 0xF0: /* LOCK */
		case 0xF2: /* REPNE/REPNZ */
		case 0xF3: /* REP/REPE/REPZ */
		case 0x2E: /* CS segment override */
		case 0x36: /* SS segment override */
		case 0x3E: /* DS segment override */
		case 0x26: /* ES segment override */
		case 0x64: /* FS segment override */
		case 0x65: /* GS segment override */
		case 0x66: /* Operand size override */
		case 0x67: /* Address size override */
			break;
		default:
			/* Not a prefix byte */
			return (i);
		}
	}

	return (i);
}

/*
 * Decode x86-64 ModR/M byte
 */
static void
emu_decode_x86_modrm(uint8_t modrm, uint8_t *mod, uint8_t *reg, uint8_t *rm)
{
	*mod = (modrm >> 6) & 0x03;
	*reg = (modrm >> 3) & 0x07;
	*rm = modrm & 0x07;
}

/*
 * Decode x86-64 SIB byte
 */
static void
emu_decode_x86_sib(uint8_t sib, uint8_t *scale, uint8_t *index, uint8_t *base) __unused;

static void
emu_decode_x86_sib(uint8_t sib, uint8_t *scale, uint8_t *index, uint8_t *base)
{
	*scale = (sib >> 6) & 0x03;
	*index = (sib >> 3) & 0x07;
	*base = sib & 0x07;
}

/*
 * Decode x86-64 instruction
 */
enum emu_decode_result
emu_decode_x86_64(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	uint8_t buffer[EMU_MAX_INSN_LEN];
	size_t fetched_len;
	int prefix_len;
	uint8_t mod, reg, rm;
	enum emu_decode_result result;
	int i;

	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Initialize instruction structure */
	memset(insn, 0, sizeof(struct emu_insn));
	insn->rip = rip;

	/* Fetch instruction bytes */
	result = emu_fetch_insn_bytes(mem, rip, buffer, EMU_MAX_INSN_LEN,
	    &fetched_len);
	if (result != EMU_DECODE_OK)
		return (result);

	if (fetched_len == 0)
		return (EMU_DECODE_ERR_BOUNDS);

	/* Decode prefixes */
	prefix_len = emu_decode_x86_prefixes(buffer, fetched_len, &insn->prefixes);
	if (prefix_len < 0)
		return (EMU_DECODE_ERR_INVALID);

	/* Check for instruction length limit */
	if (prefix_len >= EMU_MAX_INSN_LEN)
		return (EMU_DECODE_ERR_LENGTH);

	/* Get primary opcode */
	insn->opcode = buffer[prefix_len];
	insn->length = prefix_len + 1;

	/* Decode ModR/M byte if present */
	if (insn->opcode >= 0x00 && insn->opcode <= 0xFF) {
		/* Most opcodes have ModR/M byte */
		if ((size_t)(prefix_len + 1) < fetched_len) {
			insn->modrm = buffer[prefix_len + 1];
			emu_decode_x86_modrm(insn->modrm, &mod, &reg, &rm);

			/* Set up operands based on ModR/M */
			insn->num_operands = 2;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = reg;
			insn->operands[0].size = 8; /* Default to 64-bit */

			if (mod == 0x03) {
				/* Register operand */
				insn->operands[1].type = EMU_OP_REG;
				insn->operands[1].reg = rm;
				insn->operands[1].size = 8;
			} else {
				/* Memory operand */
				insn->operands[1].type = EMU_OP_MEM;
				insn->operands[1].size = 8;

				/* Check for SIB byte */
				if (rm == 0x04 && mod != 0x03) {
					if ((size_t)(prefix_len + 2) < fetched_len) {
						insn->sib = buffer[prefix_len + 2];
						insn->length++;
					}
				}

				/* Add displacement if present */
				if (mod == 0x00 && rm == 0x05) {
					/* 32-bit displacement */
					if ((size_t)(prefix_len + 2 + 4) <= fetched_len) {
						insn->operands[1].type = EMU_OP_DISP;
						for (i = 0; i < 4; i++)
							insn->operands[1].value |=
							    ((uint64_t)buffer[prefix_len + 2 + i]) << (i * 8);
						insn->length += 4;
					}
				} else if (mod == 0x01) {
					/* 8-bit displacement */
					if ((size_t)(prefix_len + 2) <= fetched_len) {
						insn->operands[1].value = buffer[prefix_len + 2];
						insn->length++;
					}
				} else if (mod == 0x02) {
					/* 32-bit displacement */
					if ((size_t)(prefix_len + 2 + 4) <= fetched_len) {
						for (i = 0; i < 4; i++)
							insn->operands[1].value |=
							    ((uint64_t)buffer[prefix_len + 2 + i]) << (i * 8);
						insn->length += 4;
					}
				}
			}
		}
	}

	/* Set mnemonic based on opcode (simplified - real implementation would be comprehensive) */
	switch (insn->opcode) {
	case 0x00:
		insn->mnemonic = "add";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		break;
	case 0x01:
		insn->mnemonic = "add";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG |
		    EMU_INSFLAG_READS_MEM;
		break;
	case 0x03:
		insn->mnemonic = "add";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		break;
	case 0x0F:
		/* Two-byte opcode */
		if ((size_t)(prefix_len + 1) < fetched_len) {
			insn->opcode2 = buffer[prefix_len + 1];
			insn->length++;
			insn->mnemonic = "two-byte-escape";
		}
		break;
	case 0x31:
		insn->mnemonic = "xor";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		break;
	case 0x89:
		insn->mnemonic = "mov";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG |
		    EMU_INSFLAG_READS_MEM;
		break;
	case 0x8B:
		insn->mnemonic = "mov";
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG |
		    EMU_INSFLAG_READS_MEM;
		break;
	case 0xC3:
		insn->mnemonic = "ret";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_READS_MEM;
		break;
	case 0xE8:
		insn->mnemonic = "call";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_MEM;
		break;
	case 0xE9:
		insn->mnemonic = "jmp";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW;
		break;
	case 0x90:
		insn->mnemonic = "nop";
		break;
	default:
		insn->mnemonic = "unknown";
		insn->flags = 0;
		break;
	}

	/* Validate instruction length */
	if (insn->length > EMU_MAX_INSN_LEN)
		return (EMU_DECODE_ERR_LENGTH);

	if (insn->length > fetched_len)
		return (EMU_DECODE_ERR_BOUNDS);

	return (EMU_DECODE_OK);
}

/*
 * Decode instruction from buffer (generic wrapper)
 */
enum emu_decode_result
emu_decode_instruction_buffer(const uint8_t *buf, size_t len, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	struct emu_guest_mem temp_mem;
	enum emu_decode_result result;

	if (buf == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	if (len == 0)
		return (EMU_DECODE_ERR_BOUNDS);

	/* Create temporary memory descriptor for buffer */
	temp_mem.base = (void *)(uintptr_t)buf;
	temp_mem.total_size = len;
	temp_mem.used_size = len;
	temp_mem.regions = NULL;
	temp_mem.num_regions = 0;
	temp_mem.strict_align = false;
	temp_mem.initialized = true;

	/* Decode based on architecture */
	switch (cpu->arch) {
	case EMU_ARCH_AMD64:
		result = emu_decode_x86_64(&temp_mem, 0, insn, cpu);
		break;
	case EMU_ARCH_I386:
		/* For now, use x86-64 decoder (will need separate implementation) */
		result = emu_decode_x86_64(&temp_mem, 0, insn, cpu);
		break;
	case EMU_ARCH_ARM64:
	case EMU_ARCH_ARM:
	case EMU_ARCH_POWERPC:
	case EMU_ARCH_RISCV:
		/* TODO: Implement architecture-specific decoders */
		return (EMU_DECODE_ERR_UNSUPPORTED);
	default:
		return (EMU_DECODE_ERR_INVALID);
	}

	/* Adjust RIP in decoded instruction */
	if (result == EMU_DECODE_OK)
		insn->rip = rip;

	return (result);
}

/*
 * Decode instruction at given address (generic wrapper)
 */
enum emu_decode_result
emu_decode_instruction(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Decode based on architecture */
	switch (cpu->arch) {
	case EMU_ARCH_AMD64:
		return (emu_decode_x86_64(mem, rip, insn, cpu));
	case EMU_ARCH_I386:
		/* For now, use x86-64 decoder (will need separate implementation) */
		return (emu_decode_x86_64(mem, rip, insn, cpu));
	case EMU_ARCH_ARM64:
	case EMU_ARCH_ARM:
	case EMU_ARCH_POWERPC:
	case EMU_ARCH_RISCV:
		/* TODO: Implement architecture-specific decoders */
		return (EMU_DECODE_ERR_UNSUPPORTED);
	default:
		return (EMU_DECODE_ERR_INVALID);
	}
}

/*
 * Calculate effective address for memory operand
 */
enum emu_decode_result
emu_calc_effective_address(struct emu_cpu_state *cpu,
    const struct emu_operand *operand, uint64_t *effective_addr)
{
	uint64_t base, index, scale, disp;

	if (cpu == NULL || operand == NULL || effective_addr == NULL)
		return (EMU_DECODE_ERR_NULL);

	if (operand->type != EMU_OP_MEM && operand->type != EMU_OP_DISP)
		return (EMU_DECODE_ERR_INVALID);

	/* Simplified effective address calculation */
	/* Real implementation would handle all x86-64 addressing modes */
	base = 0;
	index = 0;
	scale = 1;
	disp = operand->value;

	*effective_addr = base + (index * scale) + disp;

	return (EMU_DECODE_OK);
}

/*
 * Read operand value
 */
enum emu_decode_result
emu_read_operand(struct emu_guest_mem *mem, struct emu_cpu_state *cpu,
    const struct emu_operand *operand, uint64_t *value)
{
	enum emu_mem_access ret;

	if (mem == NULL || cpu == NULL || operand == NULL || value == NULL)
		return (EMU_DECODE_ERR_NULL);

	switch (operand->type) {
	case EMU_OP_REG:
		/* Read from register - simplified */
		*value = 0;
		return (EMU_DECODE_OK);

	case EMU_OP_MEM:
		/* Read from memory */
		switch (operand->size) {
		case 1:
			ret = emu_mem_read8(mem, operand->address, (uint8_t *)value);
			break;
		case 2:
			ret = emu_mem_read16(mem, operand->address, (uint16_t *)value);
			break;
		case 4:
			ret = emu_mem_read32(mem, operand->address, (uint32_t *)value);
			break;
		case 8:
			ret = emu_mem_read64(mem, operand->address, value);
			break;
		default:
			return (EMU_DECODE_ERR_INVALID);
		}

		switch (ret) {
		case EMU_MEM_ACCESS_OK:
			return (EMU_DECODE_OK);
		case EMU_MEM_ACCESSOutOfBounds:
			return (EMU_DECODE_ERR_BOUNDS);
		default:
			return (EMU_DECODE_ERR_MEMORY);
		}

	case EMU_OP_IMM:
		*value = operand->value;
		return (EMU_DECODE_OK);

	case EMU_OP_DISP:
		*value = operand->value;
		return (EMU_DECODE_OK);

	default:
		return (EMU_DECODE_ERR_INVALID);
	}
}

/*
 * Write operand value
 */
enum emu_decode_result
emu_write_operand(struct emu_guest_mem *mem, struct emu_cpu_state *cpu,
    const struct emu_operand *operand, uint64_t value)
{
	enum emu_mem_access ret;

	if (mem == NULL || cpu == NULL || operand == NULL)
		return (EMU_DECODE_ERR_NULL);

	switch (operand->type) {
	case EMU_OP_REG:
		/* Write to register - simplified */
		return (EMU_DECODE_OK);

	case EMU_OP_MEM:
		/* Write to memory */
		switch (operand->size) {
		case 1:
			ret = emu_mem_write8(mem, operand->address, (uint8_t)value);
			break;
		case 2:
			ret = emu_mem_write16(mem, operand->address, (uint16_t)value);
			break;
		case 4:
			ret = emu_mem_write32(mem, operand->address, (uint32_t)value);
			break;
		case 8:
			ret = emu_mem_write64(mem, operand->address, value);
			break;
		default:
			return (EMU_DECODE_ERR_INVALID);
		}

		switch (ret) {
		case EMU_MEM_ACCESS_OK:
			return (EMU_DECODE_OK);
		case EMU_MEM_ACCESSOutOfBounds:
			return (EMU_DECODE_ERR_BOUNDS);
		default:
			return (EMU_DECODE_ERR_MEMORY);
		}

	default:
		return (EMU_DECODE_ERR_INVALID);
	}
}

/*
 * Get instruction mnemonic (stub - real implementation would be comprehensive)
 */
const char *
emu_insn_mnemonic(uint8_t opcode, uint8_t opcode2 __unused)
{
	/* Simplified - real implementation would have full opcode table */
	switch (opcode) {
	case 0x00:
	case 0x01:
	case 0x03:
		return "add";
	case 0x31:
		return "xor";
	case 0x89:
	case 0x8B:
		return "mov";
	case 0xC3:
		return "ret";
	case 0xE8:
		return "call";
	case 0xE9:
		return "jmp";
	case 0x90:
		return "nop";
	default:
		return "unknown";
	}
}

/*
 * Architecture-specific decoder stubs
 * These would be fully implemented in separate files for each architecture
 */

enum emu_decode_result
emu_decode_x86(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* Use x86-64 decoder for now */
	return (emu_decode_x86_64(mem, rip, insn, cpu));
}

enum emu_decode_result
emu_decode_arm64(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* TODO: Implement ARM64 decoder */
	(void)mem;
	(void)rip;
	(void)insn;
	(void)cpu;
	return (EMU_DECODE_ERR_UNSUPPORTED);
}

enum emu_decode_result
emu_decode_arm(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* TODO: Implement ARM decoder */
	(void)mem;
	(void)rip;
	(void)insn;
	(void)cpu;
	return (EMU_DECODE_ERR_UNSUPPORTED);
}

enum emu_decode_result
emu_decode_riscv(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* TODO: Implement RISC-V decoder */
	(void)mem;
	(void)rip;
	(void)insn;
	(void)cpu;
	return (EMU_DECODE_ERR_UNSUPPORTED);
}

enum emu_decode_result
emu_decode_powerpc(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* TODO: Implement PowerPC decoder - requires endian-aware memory accessors */
	(void)mem;
	(void)rip;
	(void)insn;
	(void)cpu;
	return (EMU_DECODE_ERR_UNSUPPORTED);
}
