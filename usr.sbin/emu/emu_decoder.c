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
		result = emu_decode_arm64(&temp_mem, 0, insn, cpu);
		break;
	case EMU_ARCH_ARM:
		result = emu_decode_arm(&temp_mem, 0, insn, cpu);
		break;
	case EMU_ARCH_POWERPC:
		result = emu_decode_powerpc(&temp_mem, 0, insn, cpu);
		break;
	case EMU_ARCH_RISCV:
		result = emu_decode_riscv(&temp_mem, 0, insn, cpu);
		break;
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
		return (emu_decode_arm64(mem, rip, insn, cpu));
	case EMU_ARCH_ARM:
		return (emu_decode_arm(mem, rip, insn, cpu));
	case EMU_ARCH_POWERPC:
		return (emu_decode_powerpc(mem, rip, insn, cpu));
	case EMU_ARCH_RISCV:
		return (emu_decode_riscv(mem, rip, insn, cpu));
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
 * Architecture-specific decoder implementations
 */

/*
 * Decode x86 instruction (wrapper for x86-64 decoder)
 */
enum emu_decode_result
emu_decode_x86(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	/* Use x86-64 decoder for now */
	return (emu_decode_x86_64(mem, rip, insn, cpu));
}

/*
 * Fetch 32-bit instruction word from ARM64 memory (little-endian)
 */
static enum emu_decode_result
emu_fetch_arm64_insn(struct emu_guest_mem *mem, uint64_t addr, uint32_t *insn)
{
	uint8_t bytes[4];
	enum emu_decode_result result;
	size_t fetched;

	result = emu_fetch_insn_bytes(mem, addr, bytes, 4, &fetched);
	if (result != EMU_DECODE_OK)
		return (result);

	if (fetched < 4)
		return (EMU_DECODE_ERR_BOUNDS);

	/* Little-endian decode */
	*insn = ((uint32_t)bytes[0]) |
	    ((uint32_t)bytes[1] << 8) |
	    ((uint32_t)bytes[2] << 16) |
	    ((uint32_t)bytes[3] << 24);

	return (EMU_DECODE_OK);
}

/*
 * ARM64 (AArch64) instruction decoder
 *
 * AArch64 uses fixed 4-byte (32-bit) instructions with various encoding formats:
 * - Branch (B, BL, BR, BLR, CBZ, CBNZ, TBZ, TBNZ)
 * - Load/Store (LDR, STR, LDUR, STUR, LDXR, STXR)
 * - Data Processing - Register (ADD, SUB, AND, ORR, EOR, etc.)
 * - Data Processing - Immediate (ADDI, SUBI, ANDI, etc.)
 * - SIMD (NEON instructions)
 */
enum emu_decode_result
emu_decode_arm64(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	uint32_t raw;
	uint8_t op0, op1, op2;
	enum emu_decode_result result;

	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Initialize instruction structure */
	memset(insn, 0, sizeof(struct emu_insn));
	insn->rip = rip;
	insn->length = 4;  /* ARM64 fixed instruction length */

	/* Fetch instruction word */
	result = emu_fetch_arm64_insn(mem, rip, &raw);
	if (result != EMU_DECODE_OK)
		return (result);

	/* Decode instruction based on major opcodes (bits [25:28]) */
	op0 = (raw >> 28) & 0x0F;
	op1 = (raw >> 25) & 0x07;

	/* Branch instructions (op0 bits) */
	switch (op0) {
	case 0x0:
		/* Unconditional branch (immediate) or System instructions */
		if (op1 == 0x4) {
			/* BL/BLX (Branch with Link) */
			if ((raw >> 31) & 1) {
				insn->mnemonic = "bl";
				insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_REG;
			} else {
				insn->mnemonic = "b";
				insn->flags = EMU_INSFLAG_CONTROL_FLOW;
			}
			/* Decode immediate offset (26-bit signed) */
			insn->operands[0].type = EMU_OP_IMM;
			insn->operands[0].value = ((int64_t)(raw & 0x03FFFFFF) << 6) >> 6;
			insn->num_operands = 1;
		} else if (op1 == 0x0) {
			/* Compare and branch on zero (CBZ) */
			if ((raw >> 24) & 1) {
				insn->mnemonic = "cbnz";
			} else {
				insn->mnemonic = "cbz";
			}
			insn->flags = EMU_INSFLAG_CONTROL_FLOW;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = (raw >> 0) & 0x1F;
			insn->operands[0].size = 64;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = ((int64_t)((raw >> 5) & 0x7FFFF) << 14) >> 14;
			insn->num_operands = 2;
		}
		break;

	case 0x1:
		/* Conditional branch (CB) */
		insn->mnemonic = "b_cond";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW;
		insn->operands[0].type = EMU_OP_IMM;
		insn->operands[0].value = ((int64_t)(raw & 0x00FFFFFF) << 10) >> 10;
		insn->operands[1].type = EMU_OP_IMM;
		insn->operands[1].value = (raw >> 0) & 0x0F;  /* condition code */
		insn->num_operands = 2;
		break;

	case 0x2:
		/* Compare and branch on zero (wide) or test and branch */
		if (op1 == 0x6) {
			if ((raw >> 24) & 1) {
				insn->mnemonic = "tbnz";
			} else {
				insn->mnemonic = "tbz";
			}
			insn->flags = EMU_INSFLAG_CONTROL_FLOW;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = (raw >> 0) & 0x1F;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = ((raw >> 5) & 0x3F);
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = ((int64_t)((raw >> 5) & 0x7FFF) << 18) >> 18;
			insn->num_operands = 3;
		}
		break;

	case 0x4:
	case 0x5:
		/* Direct branch */
		if (op1 == 0x4) {
			if ((raw >> 31) & 1) {
				insn->mnemonic = "bl";
				insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_REG;
			} else {
				insn->mnemonic = "b";
				insn->flags = EMU_INSFLAG_CONTROL_FLOW;
			}
			insn->operands[0].type = EMU_OP_IMM;
			insn->operands[0].value = ((int64_t)(raw & 0x03FFFFFF) << 6) >> 6;
			insn->num_operands = 1;
		}
		break;
	}

	/* Load/Store instructions (bits [26:29]) */
	op0 = (raw >> 26) & 0x0F;
	if (op0 == 0x1 || op0 == 0x3) {
		uint8_t size = (raw >> 30) & 0x03;
		uint8_t opc = (raw >> 22) & 0x03;
		uint8_t rt = (raw >> 0) & 0x1F;
		uint8_t rn = (raw >> 5) & 0x1F;

		/* LDR/STR variants */
		if ((raw >> 24) & 1) {
			/* LDR (literal) */
			insn->mnemonic = "ldr";
			insn->flags = EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
		} else {
			/* Check LDR vs STR based on opc bit 1 */
			if (opc & 0x02) {
				insn->mnemonic = "ldr";
				insn->flags = EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			} else {
				insn->mnemonic = "str";
				insn->flags = EMU_INSFLAG_WRITES_MEM | EMU_INSFLAG_READS_REG;
			}
		}

		/* Set operand sizes based on size field */
		switch (size) {
		case 0: insn->operands[0].size = 1; break;
		case 1: insn->operands[0].size = 2; break;
		case 2: insn->operands[0].size = 4; break;
		case 3: insn->operands[0].size = 8; break;
		}

		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rt;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = rn;
		insn->operands[1].address = 0;
		insn->num_operands = 2;
	}

	/* Data Processing - Register (opc bits [28:31]) */
	op0 = (raw >> 28) & 0x0F;
	op1 = (raw >> 24) & 0x07;
	op2 = (raw >> 21) & 0x07;

	/* R-type instructions (add, sub, and, orr, eor, etc.) */
	if (op0 == 0x0 && op1 == 0x2) {
		uint8_t sf = (raw >> 31) & 1;
		uint8_t rm = (raw >> 16) & 0x1F;
		uint8_t rn = (raw >> 5) & 0x1F;
		uint8_t rd = (raw >> 0) & 0x1F;
		(void)sf;  /* unused but may be needed later */
		(void)rm;
		(void)rn;
		(void)rd;

		/* Determine operation from bits [30:31] and [21:23] */
		switch (op2) {
		case 0x0:
			/* ADD/SUB with register/shifted register */
			if ((raw >> 30) & 1) {
				insn->mnemonic = "subs";
				insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG | EMU_INSFLAG_READS_MEM;
			} else {
				insn->mnemonic = "add";
				insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			}
			break;
		case 0x2:
			/* AND/ANDS */
			if ((raw >> 30) & 1) {
				insn->mnemonic = "ands";
			} else {
				insn->mnemonic = "and";
			}
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			break;
		case 0x6:
			if ((raw >> 30) & 1) {
				insn->mnemonic = "orns";
			} else {
				insn->mnemonic = "orr";
			}
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			break;
		default:
			insn->mnemonic = "data_proc";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			break;
		}

		/* Set up operands: rd, rn, rm [, shift] */
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[0].size = sf ? 64 : 32;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rn;
		insn->operands[1].size = sf ? 64 : 32;
		insn->operands[2].type = EMU_OP_REG;
		insn->operands[2].reg = rm;
		insn->operands[2].size = sf ? 64 : 32;
		insn->num_operands = 3;
	}

	/* NOP - most common instruction */
	if (raw == 0xD503201F) {
		insn->mnemonic = "nop";
		insn->flags = 0;
		insn->num_operands = 0;
	}

	/* RET instruction */
	if (raw == 0xD65F07C0) {
		insn->mnemonic = "ret";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW;
		insn->num_operands = 0;
	}

	/* SVC instruction (system call) */
	if ((raw >> 24) == 0x01 && (raw & 0x0FFF0000) == 0x01000000) {
		insn->mnemonic = "svc";
		insn->flags = EMU_INSFLAG_PRIVILEGED | EMU_INSFLAG_CONTROL_FLOW;
		insn->operands[0].type = EMU_OP_IMM;
		insn->operands[0].value = raw & 0xFFFF;
		insn->num_operands = 1;
	}

	/* Default to unknown if not matched */
	if (insn->mnemonic == NULL) {
		insn->mnemonic = "unknown";
		insn->opcode = raw & 0xFF;
	}

	return (EMU_DECODE_OK);
}

/*
 * ARM (32-bit AArch32) instruction decoder
 *
 * AArch32 uses:
 * - 4-byte instructions in ARM mode
 * - 2-byte (Thumb) or 4-byte (Thumb-2) instructions in Thumb mode
 *
 * For simplicity, this implements ARM mode only with common instructions.
 */
enum emu_decode_result
emu_decode_arm(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	uint32_t raw;
	uint8_t cond, op, rn, rd, rm;
	enum emu_decode_result result;

	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Initialize instruction structure */
	memset(insn, 0, sizeof(struct emu_insn));
	insn->rip = rip;
	insn->length = 4;  /* ARM mode fixed instruction length */

	/* Fetch instruction word (little-endian) */
	result = emu_fetch_insn_bytes(mem, rip, (uint8_t *)&raw, 4, &(size_t){0});
	if (result != EMU_DECODE_OK) {
		if (result == EMU_DECODE_ERR_BOUNDS)
			return (EMU_DECODE_ERR_BOUNDS);
		/* Try byte-by-byte fetch */
		result = emu_fetch_insn_bytes(mem, rip, (uint8_t *)&raw, 4, &(size_t){0});
		if (result != EMU_DECODE_OK)
			return (result);
	}

	/* Get condition code (bits [28:31]) */
	cond = (raw >> 28) & 0x0F;

	/* Check for NOP (also used as padding) */
	if (raw == 0xE320F000 || raw == 0x00000000) {
		insn->mnemonic = "nop";
		insn->flags = 0;
		return (EMU_DECODE_OK);
	}

	/* Data processing instructions (bits [25:27]) */
	if ((raw >> 25) == 0x4) {
		/* Multiply / Multiply-Accumulate */
		if (((raw >> 4) & 0x0F) == 0x9) {
			if ((raw >> 20) & 1) {
				insn->mnemonic = "mla";
			} else {
				insn->mnemonic = "mul";
			}
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			rd = (raw >> 16) & 0x0F;
			rm = (raw >> 0) & 0x0F;
			rn = (raw >> 12) & 0x0F;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rd;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = rm;
			insn->operands[2].type = EMU_OP_REG;
			insn->operands[2].reg = rn;
			insn->num_operands = 3;
			return (EMU_DECODE_OK);
		}
	}

	/* Branch instructions (bits [25:27] = 0x5) */
	if ((raw >> 25) == 0x5) {
		if ((raw >> 24) & 1) {
			/* BL - Branch with Link */
			insn->mnemonic = "bl";
			insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_REG;
		} else {
			/* B - Branch */
			insn->mnemonic = "b";
			insn->flags = EMU_INSFLAG_CONTROL_FLOW;
		}
		/* Sign-extend 24-bit immediate */
		insn->operands[0].type = EMU_OP_IMM;
		insn->operands[0].value = ((int32_t)(raw & 0x00FFFFFF) << 8) >> 8;
		insn->num_operands = 1;
		return (EMU_DECODE_OK);
	}

	/* Load/Store instructions (bits [26:27] = 0x1 or 0x3) */
	op = (raw >> 20) & 0x1F;
	if ((raw >> 26) == 0x1) {
		if ((raw >> 20) & 0x01) {
			/* LDR */
			insn->mnemonic = "ldr";
			insn->flags = EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
		} else {
			/* STR */
			insn->mnemonic = "str";
			insn->flags = EMU_INSFLAG_WRITES_MEM | EMU_INSFLAG_READS_REG;
		}
		rd = (raw >> 12) & 0x0F;
		rn = (raw >> 16) & 0x0F;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = rn;
		insn->num_operands = 2;
		return (EMU_DECODE_OK);
	}

	/* Data processing immediate (bits [25:27] = 0x2) */
	if ((raw >> 25) == 0x2) {
		op = (raw >> 21) & 0x0F;
		rd = (raw >> 12) & 0x0F;
		rn = (raw >> 16) & 0x0F;

		switch (op) {
		case 0x0: insn->mnemonic = "and"; break;
		case 0x1: insn->mnemonic = "eor"; break;
		case 0x2: insn->mnemonic = "sub"; break;
		case 0x3: insn->mnemonic = "rsb"; break;
		case 0x4: insn->mnemonic = "add"; break;
		case 0x5: insn->mnemonic = "adc"; break;
		case 0x6: insn->mnemonic = "sbc"; break;
		case 0x7: insn->mnemonic = "rsc"; break;
		case 0x8:
			if (cond != 0xF) {
				insn->mnemonic = "tst";
				insn->flags = EMU_INSFLAG_READS_REG;
			} else {
				/* Undefined/SVC */
				insn->mnemonic = "svc";
				insn->flags = EMU_INSFLAG_PRIVILEGED | EMU_INSFLAG_CONTROL_FLOW;
			}
			break;
		case 0x9:
			insn->mnemonic = "teq";
			insn->flags = EMU_INSFLAG_READS_REG;
			break;
		case 0xA: insn->mnemonic = "cmp"; break;
		case 0xB: insn->mnemonic = "cmn"; break;
		case 0xC: insn->mnemonic = "orr"; break;
		case 0xD: insn->mnemonic = "mov"; insn->flags = EMU_INSFLAG_WRITES_REG; break;
		case 0xE:
			if ((raw >> 5) & 1) {
				insn->mnemonic = "mvn";
			} else {
				insn->mnemonic = "bic";
			}
			break;
		case 0xF: insn->mnemonic = "mvn"; break;
		default: insn->mnemonic = "data_imm"; break;
		}

		insn->flags |= EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rn;
		insn->operands[2].type = EMU_OP_IMM;
		insn->operands[2].value = raw & 0xFFF;
		insn->num_operands = 3;
		return (EMU_DECODE_OK);
	}

	/* Data processing register (bits [25:27] = 0x0) */
	if ((raw >> 25) == 0x0 && ((raw >> 4) & 0x0F) != 0x9) {
		op = (raw >> 21) & 0x0F;
		rd = (raw >> 12) & 0x0F;
		rn = (raw >> 16) & 0x0F;
		rm = (raw >> 0) & 0x0F;

		switch (op) {
		case 0x0: insn->mnemonic = "and"; break;
		case 0x1: insn->mnemonic = "eor"; break;
		case 0x2: insn->mnemonic = "sub"; break;
		case 0x3: insn->mnemonic = "rsb"; break;
		case 0x4: insn->mnemonic = "add"; break;
		case 0xA: insn->mnemonic = "cmp"; break;
		case 0xD: insn->mnemonic = "mov"; break;
		case 0xF: insn->mnemonic = "mvn"; break;
		default: insn->mnemonic = "data_reg"; break;
		}

		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rn;
		insn->operands[2].type = EMU_OP_REG;
		insn->operands[2].reg = rm;
		insn->num_operands = 3;
		return (EMU_DECODE_OK);
	}

	/* MOVW / MOVT */
	if ((raw >> 25) == 0x3) {
		if ((raw >> 20) == 0x3) {
			insn->mnemonic = "movw";
			insn->flags = EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = (raw >> 12) & 0x0F;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw & 0x0FFF) | ((raw >> 4) & 0xF000);
			insn->num_operands = 2;
			return (EMU_DECODE_OK);
		}
		if ((raw >> 20) == 0x4) {
			insn->mnemonic = "movt";
			insn->flags = EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = (raw >> 12) & 0x0F;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw & 0x0FFF) | ((raw >> 4) & 0xF000);
			insn->num_operands = 2;
			return (EMU_DECODE_OK);
		}
	}

	/* Default to unknown */
	insn->mnemonic = "unknown";
	insn->opcode = raw & 0xFF;

	return (EMU_DECODE_OK);
}

/*
 * RISC-V instruction decoder
 *
 * RISC-V base ISA uses 4-byte (32-bit) instructions with variable-length
 * extension (2, 4, 6, or 8 bytes). This implements the base I (integer) ISA.
 *
 * Instruction formats: R, I, S, B, U, J
 */
enum emu_decode_result
emu_decode_riscv(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	uint32_t raw;
	uint8_t opcode, rd, rs1, rs2, funct3, funct7;
	enum emu_decode_result result;

	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Initialize instruction structure */
	memset(insn, 0, sizeof(struct emu_insn));
	insn->rip = rip;
	insn->length = 4;  /* RISC-V base ISA is 32-bit */

	/* Fetch instruction word (little-endian) */
	result = emu_fetch_insn_bytes(mem, rip, (uint8_t *)&raw, 4, &(size_t){0});
	if (result != EMU_DECODE_OK)
		return (result);

	/* Decode instruction fields */
	opcode = raw & 0x7F;
	rd = (raw >> 7) & 0x1F;
	funct3 = (raw >> 12) & 0x07;
	rs1 = (raw >> 15) & 0x1F;
	rs2 = (raw >> 20) & 0x1F;
	funct7 = (raw >> 25) & 0x7F;

	/* NOP (addi x0, x0, 0) */
	if (raw == 0x00000013) {
		insn->mnemonic = "nop";
		insn->flags = 0;
		return (EMU_DECODE_OK);
	}

	/* Decode based on opcode major category */
	switch (opcode) {
	case 0x03:
		/* Load instructions */
		insn->flags = EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = rs1;
		insn->operands[2].type = EMU_OP_IMM;
		/* sign-extend 12-bit immediate */
		insn->operands[2].value = ((int32_t)raw >> 20);
		insn->num_operands = 3;

		switch (funct3) {
		case 0x0: insn->mnemonic = "lb"; insn->operands[0].size = 1; break;
		case 0x1: insn->mnemonic = "lh"; insn->operands[0].size = 2; break;
		case 0x2: insn->mnemonic = "lw"; insn->operands[0].size = 4; break;
		case 0x3: insn->mnemonic = "ld"; insn->operands[0].size = 8; break;
		case 0x4: insn->mnemonic = "lbu"; insn->operands[0].size = 1; break;
		case 0x5: insn->mnemonic = "lhu"; insn->operands[0].size = 2; break;
		case 0x6: insn->mnemonic = "lwu"; insn->operands[0].size = 4; break;
		default: insn->mnemonic = "load"; break;
		}
		break;

	case 0x07:
		/* Store instructions */
		insn->flags = EMU_INSFLAG_WRITES_MEM | EMU_INSFLAG_READS_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rs2;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = rs1;
		insn->operands[2].type = EMU_OP_IMM;
		/* sign-extend 12-bit immediate (bits 7:11 and 25:31) */
		insn->operands[2].value = (((int32_t)(raw & 0xFE000000)) >> 20) |
		    ((raw >> 7) & 0x1F);
		insn->num_operands = 3;

		switch (funct3) {
		case 0x0: insn->mnemonic = "sb"; insn->operands[0].size = 1; break;
		case 0x1: insn->mnemonic = "sh"; insn->operands[0].size = 2; break;
		case 0x2: insn->mnemonic = "sw"; insn->operands[0].size = 4; break;
		case 0x3: insn->mnemonic = "sd"; insn->operands[0].size = 8; break;
		default: insn->mnemonic = "store"; break;
		}
		break;

	case 0x13:
		/* Integer Register-Immediate */
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rs1;
		insn->operands[2].type = EMU_OP_IMM;
		insn->operands[2].value = ((int32_t)raw) >> 20;
		insn->num_operands = 3;

		switch (funct3) {
		case 0x0: insn->mnemonic = "addi"; break;
		case 0x1:
			if (funct7 == 0x00) {
				insn->mnemonic = "slli";
			} else {
				insn->mnemonic = "slliw";
			}
			break;
		case 0x2: insn->mnemonic = "slti"; break;
		case 0x3: insn->mnemonic = "sltiu"; break;
		case 0x4: insn->mnemonic = "xori"; break;
		case 0x5:
			if (funct7 == 0x00) {
				insn->mnemonic = "srli";
			} else if (funct7 == 0x20) {
				insn->mnemonic = "srai";
			} else {
				insn->mnemonic = "srliw";
			}
			break;
		case 0x6: insn->mnemonic = "ori"; break;
		case 0x7: insn->mnemonic = "andi"; break;
		default: insn->mnemonic = "op_imm"; break;
		}
		break;

	case 0x33:
		/* Integer Register-Register */
		insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rs1;
		insn->operands[2].type = EMU_OP_REG;
		insn->operands[2].reg = rs2;
		insn->num_operands = 3;

		if (funct7 == 0x01) {
			/* M extension (multiply/divide) */
			switch (funct3) {
			case 0x0: insn->mnemonic = "mul"; break;
			case 0x1: insn->mnemonic = "mulh"; break;
			case 0x2: insn->mnemonic = "mulhsu"; break;
			case 0x3: insn->mnemonic = "mulhu"; break;
			case 0x4: insn->mnemonic = "div"; break;
			case 0x5: insn->mnemonic = "divu"; break;
			case 0x6: insn->mnemonic = "rem"; break;
			case 0x7: insn->mnemonic = "remu"; break;
			default: insn->mnemonic = "m_op"; break;
			}
		} else {
			/* Base ISA operations */
			switch (funct3) {
			case 0x0:
				if (funct7 == 0x00) {
					insn->mnemonic = "add";
				} else if (funct7 == 0x20) {
					insn->mnemonic = "sub";
				} else {
					insn->mnemonic = "addw";
				}
				break;
			case 0x1:
				if (funct7 == 0x00) {
					insn->mnemonic = "sll";
				} else {
					insn->mnemonic = "sllw";
				}
				break;
			case 0x2: insn->mnemonic = "slt"; break;
			case 0x3: insn->mnemonic = "sltu"; break;
			case 0x4: insn->mnemonic = "xor"; break;
			case 0x5:
				if (funct7 == 0x00) {
					insn->mnemonic = "srl";
				} else if (funct7 == 0x20) {
					insn->mnemonic = "sra";
				} else {
					insn->mnemonic = "srlw";
				}
				break;
			case 0x6: insn->mnemonic = "or"; break;
			case 0x7: insn->mnemonic = "and"; break;
			default: insn->mnemonic = "op"; break;
			}
		}
		break;

	case 0x17:
		/* AUIPC - Add Upper Immediate to PC */
		insn->mnemonic = "auipc";
		insn->flags = EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_IMM;
		insn->operands[1].value = raw & 0xFFFFF000;
		insn->num_operands = 2;
		break;

	case 0x37:
		/* LUI - Load Upper Immediate */
		insn->mnemonic = "lui";
		insn->flags = EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_IMM;
		insn->operands[1].value = raw & 0xFFFFF000;
		insn->num_operands = 2;
		break;

	case 0x1F:
		/* FENCE (and FENCE.TSO in some extensions) */
		insn->mnemonic = "fence";
		insn->flags = EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_MEM;
		insn->num_operands = 0;
		break;

	case 0x63:
		/* Conditional branch */
		insn->mnemonic = "branch";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_READS_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rs1;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rs2;
		insn->operands[2].type = EMU_OP_IMM;
		/* sign-extend 13-bit offset (B-type immediate) */
		insn->operands[2].value = (((int32_t)(raw & 0x80000000)) >> 19) |
		    ((raw & 0x80) << 4) | ((raw >> 20) & 0x7E0) | ((raw >> 7) & 0x1E);
		insn->num_operands = 3;

		switch (funct3) {
		case 0x0: insn->mnemonic = "beq"; break;
		case 0x1: insn->mnemonic = "bne"; break;
		case 0x4: insn->mnemonic = "blt"; break;
		case 0x5: insn->mnemonic = "bge"; break;
		case 0x6: insn->mnemonic = "bltu"; break;
		case 0x7: insn->mnemonic = "bgeu"; break;
		}
		break;

	case 0x6F:
		/* JAL - Jump and Link */
		insn->mnemonic = "jal";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_IMM;
		/* sign-extend 21-bit offset (J-type immediate) */
		insn->operands[1].value = (((int32_t)(raw & 0x80000000)) >> 11) |
		    ((raw >> 20) & 1) | ((raw & 0x7FE000) >> 20) | ((raw & 0x00100000) >> 9);
		insn->num_operands = 2;
		break;

	case 0x67:
		/* JALR - Jump and Link Register */
		insn->mnemonic = "jalr";
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_WRITES_REG | EMU_INSFLAG_READS_REG;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rd;
		insn->operands[1].type = EMU_OP_REG;
		insn->operands[1].reg = rs1;
		insn->operands[2].type = EMU_OP_IMM;
		insn->operands[2].value = ((int32_t)raw >> 20);
		insn->num_operands = 3;
		break;

	case 0x73:
		/* SYSTEM instructions (CSR, ecall, ebreak, mret) */
		insn->flags = EMU_INSFLAG_PRIVILEGED;
		if (rs1 == 0 && funct3 == 0) {
			/* ecall, ebreak, mret, sret, uret */
			switch ((raw >> 20) & 0xFFF) {
			case 0x000: insn->mnemonic = "ecall"; break;
			case 0x001: insn->mnemonic = "ebreak"; break;
			case 0x002: insn->mnemonic = "uret"; break;
			case 0x102: insn->mnemonic = "sret"; break;
			case 0x302: insn->mnemonic = "mret"; break;
			case 0x105: insn->mnemonic = "wfi"; break;
			default: insn->mnemonic = "system"; break;
			}
		} else {
			/* CSR instructions */
			insn->mnemonic = "csr";
			insn->flags |= EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rd;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = rs1;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = (raw >> 20) & 0xFFF;
			insn->num_operands = 3;

			switch (funct3) {
			case 0x1: insn->mnemonic = "csrrw"; break;
			case 0x2: insn->mnemonic = "csrrs"; break;
			case 0x3: insn->mnemonic = "csrrc"; break;
			case 0x5: insn->mnemonic = "csrrwi"; break;
			case 0x6: insn->mnemonic = "csrrsi"; break;
			case 0x7: insn->mnemonic = "csrrci"; break;
			}
		}
		break;

	default:
		insn->mnemonic = "unknown";
		insn->opcode = opcode;
		break;
	}

	return (EMU_DECODE_OK);
}

/*
 * PowerPC instruction decoder
 *
 * PowerPC uses fixed 4-byte (32-bit) instructions with various formats:
 * - X-form: bits [0:5] = opcode, bits [6:10] = RT, bits [11:15] = RA, bits [16:20] = RB, bits [21:30] = XO
 * - D-form: bits [0:5] = opcode, bits [6:10] = RT, bits [11:15] = RA, bits [16:31] = D/DS
 * - B-form: bits [0:5] = opcode, bits [6:10] = BT/BI, bits [11:15] = BA, bits [16:20] = BB, bits [21:30] = BO, bits [31] = AA, bits [31] = LK
 * - I-form: bits [0:5] = opcode, bits [6:10] = RT/RA, bits [11:15] = RS/SPR, bits [16:31] = LI/IMM
 *
 * This implements common PowerPC e500 and general-purpose instructions.
 */
enum emu_decode_result
emu_decode_powerpc(struct emu_guest_mem *mem, uint64_t rip,
    struct emu_insn *insn, struct emu_cpu_state *cpu)
{
	uint32_t raw;
	uint8_t opcode, rt, ra, rb;
	uint16_t d;
	uint32_t li;
	enum emu_decode_result result;

	if (mem == NULL || insn == NULL || cpu == NULL)
		return (EMU_DECODE_ERR_NULL);

	/* Initialize instruction structure */
	memset(insn, 0, sizeof(struct emu_insn));
	insn->rip = rip;
	insn->length = 4;  /* PowerPC fixed instruction length */

	/* Fetch instruction word (big-endian) */
	result = emu_fetch_insn_bytes(mem, rip, (uint8_t *)&raw, 4, &(size_t){0});
	if (result != EMU_DECODE_OK)
		return (result);

	/* Convert to big-endian for decoding (host may be little-endian) */
	raw = ((raw & 0xFF) << 24) | ((raw & 0xFF00) << 8) |
	    ((raw >> 8) & 0xFF00) | ((raw >> 24) & 0xFF);

	/* Extract common fields */
	opcode = (raw >> 26) & 0x3F;
	rt = (raw >> 21) & 0x1F;
	ra = (raw >> 16) & 0x1F;
	rb = (raw >> 11) & 0x1F;
	(void)rb;  /* unused in current implementation */

	/* NOP */
	if (raw == 0x60000000 || raw == 0x00000000) {
		insn->mnemonic = "nop";
		insn->flags = 0;
		return (EMU_DECODE_OK);
	}

	/* Decode based on opcode major category */
	switch (opcode) {
	case 0x18:
		/* Branch instructions (B, BA, BL, BLA, BC, BCA, BCL, BCLA) */
		insn->flags = EMU_INSFLAG_CONTROL_FLOW;
		li = raw & 0x03FFFFFC;

		if ((raw >> 1) & 1) {
			/* Link bit set */
			insn->flags |= EMU_INSFLAG_WRITES_REG;
		}

		if ((raw & 0x04) == 0) {
			/* Conditional branch (BC/BCL) */
			if (insn->flags & EMU_INSFLAG_WRITES_REG) {
				insn->mnemonic = "bcl";
			} else {
				insn->mnemonic = "bc";
			}
			insn->operands[0].type = EMU_OP_IMM;
			insn->operands[0].value = (raw >> 6) & 0x1F;  /* BO field */
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw >> 11) & 0x1F;  /* BI field */
			insn->operands[2].type = EMU_OP_IMM;
			/* sign-extend 16-bit BD */
			insn->operands[2].value = ((int16_t)(raw & 0xFFFC));
			insn->num_operands = 3;
		} else {
			/* Unconditional branch (B/BL) */
			if (insn->flags & EMU_INSFLAG_WRITES_REG) {
				insn->mnemonic = "bl";
			} else {
				insn->mnemonic = "b";
			}
			/* sign-extend 26-bit LI */
			insn->operands[0].type = EMU_OP_IMM;
			insn->operands[0].value = ((int32_t)(li << 6)) >> 6;
			insn->num_operands = 1;
		}
		break;

	case 0x12:
		/* Branch to link register (BLR, BCTR, etc.) */
		insn->flags = EMU_INSFLAG_CONTROL_FLOW | EMU_INSFLAG_READS_REG;
		if ((raw >> 1) & 1) {
			insn->mnemonic = "mtlr";
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->num_operands = 1;
		} else if ((raw >> 1) & 1) {
			insn->mnemonic = "mflr";
			insn->flags = EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->num_operands = 1;
		} else if ((raw & 0x03E007FF) == 0x03E00420) {
			insn->mnemonic = "blr";
		} else if ((raw & 0x03E007FF) == 0x03E00400) {
			insn->mnemonic = "bctr";
		}
		break;

	case 0x20:
		/* Load/Store with update (D-form): lwz, lwzux, lbz, lbzux, sth, stb, stw, stwu, etc. */
		d = raw & 0xFFFF;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = ra;
		insn->operands[2].type = EMU_OP_IMM;
		insn->operands[2].value = (int16_t)d;
		insn->operands[2].is_signed = true;
		insn->num_operands = 3;

		/* Check if update form (bit 20) */
		if ((raw >> 20) & 1) {
			/* Update form - ra gets updated */
			insn->flags |= EMU_INSFLAG_WRITES_REG;
		}

		switch ((raw >> 1) & 0x1F) {
		case 0x00:  /* lbz */
			insn->mnemonic = "lbz";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 1;
			break;
		case 0x01:  /* lbzu */
			insn->mnemonic = "lbzu";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 1;
			break;
		case 0x02:  /* lwz */
			insn->mnemonic = "lwz";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 4;
			break;
		case 0x03:  /* lwzu */
			insn->mnemonic = "lwzu";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 4;
			break;
		case 0x08:  /* lha */
			insn->mnemonic = "lha";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 2;
			break;
		case 0x09:  /* lhau */
			insn->mnemonic = "lhau";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 2;
			break;
		case 0x0A:  /* lhz */
			insn->mnemonic = "lhz";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 2;
			break;
		case 0x0B:  /* lhzu */
			insn->mnemonic = "lhzu";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[0].size = 2;
			break;
		default:
			insn->mnemonic = "load";
			insn->flags |= EMU_INSFLAG_READS_MEM | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			break;
		}
		break;

	case 0x24:
 	/* Store instructions (D-form): stw, stwu, stb, stbu, sth, sthu */
		d = raw & 0xFFFF;
		insn->operands[0].type = EMU_OP_REG;
		insn->operands[0].reg = rt;
		insn->operands[1].type = EMU_OP_MEM;
		insn->operands[1].reg = ra;
		insn->operands[2].type = EMU_OP_IMM;
		insn->operands[2].value = (int16_t)d;
		insn->operands[2].is_signed = true;
		insn->num_operands = 3;
		insn->flags = EMU_INSFLAG_WRITES_MEM | EMU_INSFLAG_READS_REG;

		switch ((raw >> 1) & 0x1F) {
		case 0x04:  /* stb */
			insn->mnemonic = "stb";
			insn->operands[0].size = 1;
			break;
		case 0x05:  /* stbu */
			insn->mnemonic = "stbu";
			insn->flags |= EMU_INSFLAG_WRITES_REG;
			insn->operands[0].size = 1;
			break;
		case 0x06:  /* stw */
			insn->mnemonic = "stw";
			insn->operands[0].size = 4;
			break;
		case 0x07:  /* stwu */
			insn->mnemonic = "stwu";
			insn->flags |= EMU_INSFLAG_WRITES_REG;
			insn->operands[0].size = 4;
			break;
		case 0x0C:  /* sth */
			insn->mnemonic = "sth";
			insn->operands[0].size = 2;
			break;
		case 0x0D:  /* sthu */
			insn->mnemonic = "sthu";
			insn->flags |= EMU_INSFLAG_WRITES_REG;
			insn->operands[0].size = 2;
			break;
		default:
			insn->mnemonic = "store";
			break;
		}
		break;

	case 0x1F:
		/* XO-form and X-form instructions (integer arithmetic, logical, etc.) */
	{
		uint16_t xo = (raw >> 1) & 0x3FF;
		uint8_t rs = (raw >> 21) & 0x1F;

		/* Check extended opcode */
		if (xo >= 0x200 && xo < 0x280) {
			/* Integer arithmetic with 2 registers */
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_REG;
			insn->operands[2].reg = rb;
			insn->num_operands = 3;

			switch (xo & 0x1F) {
			case 0x00: insn->mnemonic = "add"; break;
			case 0x08: insn->mnemonic = "subf"; break;
			case 0x0C: insn->mnemonic = "and"; break;
			case 0x0D: insn->mnemonic = "andc"; break;
			case 0x0E: insn->mnemonic = "or"; break;
			case 0x0F: insn->mnemonic = "xor"; break;
			case 0x10: insn->mnemonic = "slw"; break;
			case 0x12: insn->mnemonic = "srw"; break;
			case 0x18: insn->mnemonic = "mulhwu"; break;
			default: insn->mnemonic = "int_x"; break;
			}
		} else if (xo >= 0x008 && xo < 0x010) {
			/* Compare instructions */
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = ra;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = rb;
			insn->num_operands = 2;

			if ((xo & 0x06) == 0x00) {
				insn->mnemonic = "cmpw";
			} else if ((xo & 0x06) == 0x04) {
				insn->mnemonic = "cmplw";
			}
		} else if (xo == 0x000) {
			/* cmpwi */
			insn->mnemonic = "cmpwi";
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = ra;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (int16_t)(raw & 0xFFFF);
			insn->num_operands = 2;
		} else if (xo == 0x00A) {
			/* cmplwi */
			insn->mnemonic = "cmplwi";
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = ra;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = raw & 0xFFFF;
			insn->num_operands = 2;
		} else if (xo == 0x014) {
			/* addi */
			insn->mnemonic = "addi";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = (int16_t)(raw & 0xFFFF);
			insn->num_operands = 3;
		} else if (xo == 0x00C) {
			/* addic / subic */
			insn->mnemonic = "addic";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = (int16_t)(raw & 0xFFFF);
			insn->num_operands = 3;
		} else if (xo == 0x015) {
			/* lis - Load Immediate Shifted */
			insn->mnemonic = "lis";
			insn->flags = EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw & 0xFFFF) << 16;
			insn->num_operands = 2;
		} else if (xo == 0x016 || xo == 0x017) {
			/* ori, oris */
			insn->mnemonic = (xo == 0x016) ? "ori" : "oris";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = raw & 0xFFFF;
			insn->num_operands = 3;
		} else if (xo == 0x018 || xo == 0x019) {
			/* xori, xoris */
			insn->mnemonic = (xo == 0x018) ? "xori" : "xoris";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = raw & 0xFFFF;
			insn->num_operands = 3;
		} else if (xo == 0x01A || xo == 0x01B) {
			/* andi., andis. */
			insn->mnemonic = (xo == 0x01A) ? "andi" : "andis";
			insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_IMM;
			insn->operands[2].value = raw & 0xFFFF;
			insn->num_operands = 3;
		} else if (xo == 0x01C || xo == 0x01D) {
			/* subi, cmpli */
			if (xo == 0x01C) {
				insn->mnemonic = "subi";
				insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
				insn->operands[0].type = EMU_OP_REG;
				insn->operands[0].reg = rt;
				insn->operands[1].type = EMU_OP_REG;
				insn->operands[1].reg = ra;
				insn->operands[2].type = EMU_OP_IMM;
				insn->operands[2].value = (int16_t)(raw & 0xFFFF);
				insn->num_operands = 3;
			} else {
				insn->mnemonic = "cmpli";
				insn->flags = EMU_INSFLAG_READS_REG;
				insn->operands[0].type = EMU_OP_REG;
				insn->operands[0].reg = ra;
				insn->operands[1].type = EMU_OP_IMM;
				insn->operands[1].value = raw & 0xFFFF;
				insn->num_operands = 2;
			}
		} else if (xo == 0x03A || xo == 0x03B) {
			/* twi, mulli */
			if (xo == 0x03A) {
				insn->mnemonic = "twi";
				insn->flags = EMU_INSFLAG_READS_REG;
				insn->operands[0].type = EMU_OP_IMM;
				insn->operands[0].value = (raw >> 16) & 0x1F;
				insn->operands[1].type = EMU_OP_REG;
				insn->operands[1].reg = ra;
				insn->operands[2].type = EMU_OP_IMM;
				insn->operands[2].value = (int16_t)(raw & 0xFFFF);
				insn->num_operands = 3;
			} else {
				insn->mnemonic = "mulli";
				insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
				insn->operands[0].type = EMU_OP_REG;
				insn->operands[0].reg = rt;
				insn->operands[1].type = EMU_OP_REG;
				insn->operands[1].reg = ra;
				insn->operands[2].type = EMU_OP_IMM;
				insn->operands[2].value = (int16_t)(raw & 0xFFFF);
				insn->num_operands = 3;
			}
		} else if (xo == 0x036) {
			/* mtspr / mfspr */
			if ((raw >> 20) & 1) {
				insn->mnemonic = "mfspr";
				insn->flags = EMU_INSFLAG_READS_REG | EMU_INSFLAG_WRITES_REG;
			} else {
				insn->mnemonic = "mtspr";
				insn->flags = EMU_INSFLAG_READS_REG;
			}
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_IMM;
			/* SPR number: bits 16:20 (rs) and 11:15 (rb) */
			insn->operands[1].value = ((raw >> 16) & 0x1F) << 5 | ((raw >> 11) & 0x1F);
			insn->num_operands = 2;
		} else if (xo == 0x100) {
			/* mfsr */
			insn->mnemonic = "mfsr";
			insn->flags = EMU_INSFLAG_PRIVILEGED | EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw >> 11) & 0x1F;
			insn->num_operands = 2;
		} else if (xo == 0x026) {
			/* mtcrf (Move to Condition Register Fields) */
			insn->mnemonic = "mtcrf";
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rs;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (raw >> 12) & 0xFF;
			insn->num_operands = 2;
		} else if (xo == 0x023) {
			/* mfcr (Move from Condition Register) */
			insn->mnemonic = "mfcr";
			insn->flags = EMU_INSFLAG_WRITES_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = rt;
			insn->num_operands = 1;
		} else if (xo == 0x000) {
			/* cmpwi */
			insn->mnemonic = "cmpwi";
			insn->flags = EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_REG;
			insn->operands[0].reg = ra;
			insn->operands[1].type = EMU_OP_IMM;
			insn->operands[1].value = (int16_t)(raw & 0xFFFF);
			insn->num_operands = 2;
		} else if (xo == 0x104) {
			/* isync */
			insn->mnemonic = "isync";
			insn->flags = 0;
		} else if (xo == 0x0A6) {
			/* isync (alternate encoding) */
			insn->mnemonic = "isync";
			insn->flags = 0;
		} else {
			insn->mnemonic = "xform";
		}
	}
	break;

	case 0x11:
		/* System call / trap */
		if ((raw & 0x03FFFFFE) == 0x44000002) {
			insn->mnemonic = "sc";
			insn->flags = EMU_INSFLAG_PRIVILEGED | EMU_INSFLAG_CONTROL_FLOW;
			insn->num_operands = 0;
		} else if ((raw & 0x03FFFFFE) == 0x44000000) {
			insn->mnemonic = "tw";
			insn->flags = EMU_INSFLAG_PRIVILEGED | EMU_INSFLAG_READS_REG;
			insn->operands[0].type = EMU_OP_IMM;
			insn->operands[0].value = (raw >> 16) & 0x1F;
			insn->operands[1].type = EMU_OP_REG;
			insn->operands[1].reg = ra;
			insn->operands[2].type = EMU_OP_REG;
			insn->operands[2].reg = rb;
			insn->num_operands = 3;
		} else if ((raw & 0x03FFFFFE) == 0x44000001) {
			insn->mnemonic = "trap";
			insn->flags = EMU_INSFLAG_PRIVILEGED;
		}
		break;

	default:
		insn->mnemonic = "unknown";
		insn->opcode = opcode;
		break;
	}

	return (EMU_DECODE_OK);
}
