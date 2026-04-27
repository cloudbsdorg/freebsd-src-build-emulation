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

#ifndef _EMU_DECODER_H_
#define	_EMU_DECODER_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include "emu_engine.h"

/*
 * Emulation Framework - Safe Instruction Decoder
 *
 * This module provides a framework for safely decoding guest instructions.
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

/* Maximum instruction length (x86-64 limit is 15 bytes) */
#define	EMU_MAX_INSN_LEN	15

/* Instruction decode result codes */
enum emu_decode_result {
	EMU_DECODE_OK = 0,
	EMU_DECODE_ERR_NULL,			/* NULL pointer */
	EMU_DECODE_ERR_BOUNDS,			/* Instruction fetch out of bounds */
	EMU_DECODE_ERR_LENGTH,			/* Instruction too long */
	EMU_DECODE_ERR_INVALID,			/* Invalid instruction encoding */
	EMU_DECODE_ERR_UNSUPPORTED,		/* Unsupported instruction */
	EMU_DECODE_ERR_PRIVILEGED,		/* Privileged instruction */
	EMU_DECODE_ERR_ILLEGAL,			/* Illegal instruction */
	EMU_DECODE_ERR_MEMORY,			/* Memory access error */
	EMU_DECODE_ERR_OVERFLOW			/* Operand overflow */
};

/* Operand types */
enum emu_operand_type {
	EMU_OP_NONE = 0,
	EMU_OP_REG,				/* Register operand */
	EMU_OP_MEM,				/* Memory operand */
	EMU_OP_IMM,				/* Immediate operand */
	EMU_OP_DISP				/* Displacement only */
};

/* Operand descriptor */
struct emu_operand {
	enum emu_operand_type	type;
	uint8_t			size;		/* Operand size in bytes */
	uint8_t			reg;		/* Register number (if reg operand) */
	uint64_t		value;		/* Immediate value or displacement */
	uint64_t		address;	/* Effective address (if memory) */
	bool			is_signed;	/* Signed operand */
};

/* Decoded instruction structure */
struct emu_insn {
	uint8_t			opcode;		/* Primary opcode */
	uint8_t			opcode2;	/* Secondary opcode (if any) */
	uint8_t			prefixes;	/* Instruction prefixes */
	uint8_t			modrm;		/* ModR/M byte (if present) */
	uint8_t			sib;		/* SIB byte (if present) */
	uint8_t			length;		/* Total instruction length */
	uint8_t			num_operands;	/* Number of operands */
	uint8_t			flags;		/* Instruction flags */
	uint64_t		rip;		/* Instruction address */
	struct emu_operand	operands[4];	/* Up to 4 operands */
	const char		*mnemonic;	/* Instruction mnemonic */
};

/* Instruction flags */
#define	EMU_INSFLAG_READS_MEM		0x01	/* Instruction reads memory */
#define	EMU_INSFLAG_WRITES_MEM		0x02	/* Instruction writes memory */
#define	EMU_INSFLAG_READS_REG		0x04	/* Instruction reads register */
#define	EMU_INSFLAG_WRITES_REG		0x08	/* Instruction writes register */
#define	EMU_INSFLAG_CONTROL_FLOW	0x10	/* Control flow instruction */
#define	EMU_INSFLAG_PRIVILEGED		0x20	/* Privileged instruction */
#define	EMU_INSFLAG_SIMD			0x40	/* SIMD instruction */
#define	EMU_INSFLAG_FPU			0x80	/* FPU instruction */

/* CPU context structure (architecture-specific) */
struct emu_cpu_state {
	/* General purpose registers */
	uint64_t	rax, rbx, rcx, rdx;
	uint64_t	rsi, rdi, rbp, rsp;
	uint64_t	r8, r9, r10, r11;
	uint64_t	r12, r13, r14, r15;

	/* Instruction pointer and flags */
	uint64_t	rip;
	uint64_t	rflags;

	/* Segment registers */
	uint16_t	cs, ds, es, fs, gs, ss;

	/* Control registers */
	uint64_t	cr0, cr2, cr3, cr4;

	/* Architecture identifier */
	int		arch;

	/* CPU identifier and state */
	uint32_t	cpu_id;
	bool		halted;
};

/*
 * Instruction decoder API
 */

/* Initialize decoder for specific architecture */
int emu_decoder_init(int arch);

/* Decode instruction at given address */
enum emu_decode_result emu_decode_instruction(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);

/* Decode instruction from buffer */
enum emu_decode_result emu_decode_instruction_buffer(const uint8_t *buf,
    size_t len, uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);

/* Get instruction mnemonic */
const char *emu_insn_mnemonic(uint8_t opcode, uint8_t opcode2);

/* Get instruction flags */
uint8_t emu_insn_flags(const struct emu_insn *insn);

/* Calculate effective address for memory operand */
enum emu_decode_result emu_calc_effective_address(struct emu_cpu_state *cpu,
    const struct emu_operand *operand, uint64_t *effective_addr);

/* Read operand value */
enum emu_decode_result emu_read_operand(struct emu_guest_mem *mem,
    struct emu_cpu_state *cpu, const struct emu_operand *operand,
    uint64_t *value);

/* Write operand value */
enum emu_decode_result emu_write_operand(struct emu_guest_mem *mem,
    struct emu_cpu_state *cpu, const struct emu_operand *operand,
    uint64_t value);

/* Check if instruction is valid */
bool emu_insn_is_valid(const struct emu_insn *insn);

/* Check if instruction is privileged */
bool emu_insn_is_privileged(const struct emu_insn *insn);

/* Check if instruction accesses memory */
bool emu_insn_accesses_memory(const struct emu_insn *insn);

/* Get instruction length */
uint8_t emu_insn_length(const struct emu_insn *insn);

/* Convert decode result to string */
const char *emu_decode_result_str(enum emu_decode_result result);

/* Architecture-specific decoders */
enum emu_decode_result emu_decode_x86_64(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);
enum emu_decode_result emu_decode_x86(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);
enum emu_decode_result emu_decode_arm64(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);
enum emu_decode_result emu_decode_arm(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);
enum emu_decode_result emu_decode_riscv(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);
enum emu_decode_result emu_decode_powerpc(struct emu_guest_mem *mem,
    uint64_t rip, struct emu_insn *insn, struct emu_cpu_state *cpu);

#endif /* !_EMU_DECODER_H_ */
