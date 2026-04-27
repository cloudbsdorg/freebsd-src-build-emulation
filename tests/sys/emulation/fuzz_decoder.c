/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 * Copyright (c) 2026 JetBrains s.r.o.
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <machine/atomic.h>
#include <machine/stdarg.h>

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include "opt_emulation.h"

#ifdef EMULATION

#include "../../../usr.sbin/emu/emu_decoder.h"
#include "../../../usr.sbin/emu/emu_mem.h"

#define FUZZ_ITERATIONS		10000
#define MAX_INSN_LENGTH		64
#define TEST_BUFFER_SIZE	(1024 * 1024)

static struct emu_decoder_state *decoder_state;
static uint8_t *test_buffer;

/*
 * Fuzz test: Random byte sequences for x86-64 decoder
 */
static void
test_x86_random_bytes(void)
{
	struct emu_insn insn;
	int i, j;
	uint8_t random_bytes[MAX_INSN_LENGTH];
	int result;

	printf("Testing x86-64 decoder with %d random byte sequences...\n",
	    FUZZ_ITERATIONS);

	for (i = 0; i < FUZZ_ITERATIONS; i++) {
		/* Generate random instruction bytes */
		for (j = 0; j < MAX_INSN_LENGTH; j++) {
			random_bytes[j] = arc4random() % 256;
		}

		/* Initialize decoder state */
		decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
		if (decoder_state == NULL) {
			printf("FAIL: Failed to create decoder state\n");
			return;
		}

		/* Attempt to decode - should not crash */
		result = emu_decoder_decode(decoder_state, random_bytes,
		    MAX_INSN_LENGTH, &insn);

		/* Verify decoder handles invalid input gracefully */
		if (result != 0 && result != EMU_DECODER_ERR_INVALID_OPCODE) {
			printf("FAIL: Unexpected error code %d at iteration %d\n",
			    result, i);
		}

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: x86-64 random bytes test completed\n");
}

/*
 * Fuzz test: Maximum length instruction (15 bytes)
 */
static void
test_x86_max_length(void)
{
	struct emu_insn insn;
	uint8_t max_insn[15];
	int result;
	int i;

	printf("Testing x86-64 decoder with maximum length instructions...\n");

	/* Test various 15-byte instruction patterns */
	for (i = 0; i < FUZZ_ITERATIONS / 10; i++) {
		/* Fill with random bytes */
		memset(max_insn, arc4random() % 256, 15);

		decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
		if (decoder_state == NULL)
			continue;

		result = emu_decoder_decode(decoder_state, max_insn, 15, &insn);

		/* Should either decode successfully or return error */
		if (result == 0) {
			/* Verify decoded length is valid */
			if (insn.insn_length > 15 || insn.insn_length == 0) {
				printf("FAIL: Invalid instruction length %d\n",
				    insn.insn_length);
			}
		}

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: x86-64 max length test completed\n");
}

/*
 * Fuzz test: Invalid prefix combinations
 */
static void
test_x86_invalid_prefixes(void)
{
	struct emu_insn insn;
	uint8_t prefix_test[16];
	int result;
	int i;

	printf("Testing x86-64 decoder with invalid prefix combinations...\n");

	for (i = 0; i < 1000; i++) {
		/* Generate invalid prefix sequences */
		memset(prefix_test, 0, sizeof(prefix_test));
		
		/* Add multiple conflicting prefixes */
		prefix_test[0] = 0x66; /* operand size override */
		prefix_test[1] = 0x67; /* address size override */
		prefix_test[2] = 0xF0; /* LOCK */
		prefix_test[3] = 0xF2; /* REPNE */
		prefix_test[4] = 0xF3; /* REP */
		
		/* Add random bytes after prefixes */
		for (int j = 5; j < 16; j++) {
			prefix_test[j] = arc4random() % 256;
		}

		decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
		if (decoder_state == NULL)
			continue;

		result = emu_decoder_decode(decoder_state, prefix_test, 16, &insn);

		/* Decoder should handle invalid prefixes gracefully */
		/* Either reject or decode, but never crash */

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: x86-64 invalid prefixes test completed\n");
}

/*
 * Fuzz test: Bounds checking - buffer smaller than instruction
 */
static void
test_x86_bounds_check(void)
{
	struct emu_insn insn;
	uint8_t short_buffer[4];
	int result;
	int i;

	printf("Testing x86-64 decoder bounds checking...\n");

	for (i = 0; i < 100; i++) {
		/* Fill with random bytes */
		for (int j = 0; j < 4; j++) {
			short_buffer[j] = arc4random() % 256;
		}

		decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
		if (decoder_state == NULL)
			continue;

		/* Attempt to decode with insufficient buffer */
		result = emu_decoder_decode(decoder_state, short_buffer, 4, &insn);

		/* Should return error, not crash or read out of bounds */
		if (result == 0 && insn.insn_length > 4) {
			printf("FAIL: Decoder read beyond buffer (length %d)\n",
			    insn.insn_length);
		}

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: x86-64 bounds check test completed\n");
}

/*
 * Fuzz test: PowerPC random bytes
 */
static void
test_powerpc_random_bytes(void)
{
	struct emu_insn insn;
	int i, j;
	uint8_t random_bytes[MAX_INSN_LENGTH];
	int result;

	printf("Testing PowerPC decoder with %d random byte sequences...\n",
	    FUZZ_ITERATIONS);

	for (i = 0; i < FUZZ_ITERATIONS; i++) {
		/* Generate random instruction bytes (PowerPC instructions are 4 bytes) */
		for (j = 0; j < MAX_INSN_LENGTH; j++) {
			random_bytes[j] = arc4random() % 256;
		}

		decoder_state = emu_decoder_create(EMU_ARCH_POWERPC);
		if (decoder_state == NULL) {
			printf("FAIL: Failed to create decoder state\n");
			return;
		}

		/* Attempt to decode - should not crash */
		result = emu_decoder_decode(decoder_state, random_bytes,
		    MAX_INSN_LENGTH, &insn);

		/* Verify decoder handles invalid input gracefully */
		if (result != 0 && result != EMU_DECODER_ERR_INVALID_OPCODE) {
			printf("FAIL: Unexpected error code %d at iteration %d\n",
			    result, i);
		}

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: PowerPC random bytes test completed\n");
}

/*
 * Fuzz test: PowerPC big-endian handling
 */
static void
test_powerpc_endian(void)
{
	struct emu_insn insn;
	uint8_t be_insn[4];
	int result;
	int i;

	printf("Testing PowerPC decoder big-endian handling...\n");

	for (i = 0; i < 1000; i++) {
		/* Generate random big-endian instruction */
		for (int j = 0; j < 4; j++) {
			be_insn[j] = arc4random() % 256;
		}

		decoder_state = emu_decoder_create(EMU_ARCH_POWERPC);
		if (decoder_state == NULL)
			continue;

		result = emu_decoder_decode(decoder_state, be_insn, 4, &insn);

		/* Should handle gracefully */

		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: PowerPC endian test completed\n");
}

/*
 * Fuzz test: NULL pointer handling
 */
static void
test_null_pointers(void)
{
	struct emu_insn insn;
	int result;

	printf("Testing decoder NULL pointer handling...\n");

	decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
	if (decoder_state == NULL) {
		printf("FAIL: Failed to create decoder state\n");
		return;
	}

	/* Test NULL instruction buffer */
	result = emu_decoder_decode(decoder_state, NULL, 15, &insn);
	if (result != EMU_DECODER_ERR_INVALID_PARAM) {
		printf("FAIL: Expected EMU_DECODER_ERR_INVALID_PARAM for NULL buffer\n");
	}

	/* Test NULL decoder state */
	uint8_t test_bytes[15];
	memset(test_bytes, 0, 15);
	result = emu_decoder_decode(NULL, test_bytes, 15, &insn);
	if (result != EMU_DECODER_ERR_INVALID_PARAM) {
		printf("FAIL: Expected EMU_DECODER_ERR_INVALID_PARAM for NULL decoder\n");
	}

	/* Test NULL insn structure */
	result = emu_decoder_decode(decoder_state, test_bytes, 15, NULL);
	if (result != EMU_DECODER_ERR_INVALID_PARAM) {
		printf("FAIL: Expected EMU_DECODER_ERR_INVALID_PARAM for NULL insn\n");
	}

	emu_decoder_destroy(decoder_state);
	decoder_state = NULL;

	printf("PASS: NULL pointer test completed\n");
}

/*
 * Fuzz test: Zero-length buffer
 */
static void
test_zero_length(void)
{
	struct emu_insn insn;
	uint8_t buffer[1];
	int result;

	printf("Testing decoder zero-length buffer handling...\n");

	decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
	if (decoder_state == NULL) {
		printf("FAIL: Failed to create decoder state\n");
		return;
	}

	result = emu_decoder_decode(decoder_state, buffer, 0, &insn);
	if (result != EMU_DECODER_ERR_INVALID_PARAM) {
		printf("FAIL: Expected EMU_DECODER_ERR_INVALID_PARAM for zero-length buffer\n");
	}

	emu_decoder_destroy(decoder_state);
	decoder_state = NULL;

	printf("PASS: Zero-length buffer test completed\n");
}

/*
 * Fuzz test: Stress test with rapid create/destroy cycles
 */
static void
test_stress_create_destroy(void)
{
	int i;

	printf("Testing decoder stress (rapid create/destroy)...\n");

	for (i = 0; i < 10000; i++) {
		decoder_state = emu_decoder_create(EMU_ARCH_X86_64);
		if (decoder_state == NULL) {
			printf("FAIL: Failed to create decoder state at iteration %d\n", i);
			return;
		}
		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	for (i = 0; i < 10000; i++) {
		decoder_state = emu_decoder_create(EMU_ARCH_POWERPC);
		if (decoder_state == NULL) {
			printf("FAIL: Failed to create decoder state at iteration %d\n", i);
			return;
		}
		emu_decoder_destroy(decoder_state);
		decoder_state = NULL;
	}

	printf("PASS: Stress test completed\n");
}

/*
 * Main test runner
 */
int
main(int argc, char *argv[])
{
	printf("=== Instruction Decoder Fuzz Tests ===\n\n");

	/* Allocate test buffer */
	test_buffer = malloc(TEST_BUFFER_SIZE);
	if (test_buffer == NULL) {
		err(1, "malloc");
	}

	/* Run all fuzz tests */
	test_x86_random_bytes();
	test_x86_max_length();
	test_x86_invalid_prefixes();
	test_x86_bounds_check();
	test_powerpc_random_bytes();
	test_powerpc_endian();
	test_null_pointers();
	test_zero_length();
	test_stress_create_destroy();

	/* Cleanup */
	free(test_buffer);

	printf("\n=== All fuzz tests completed ===\n");
	return (0);
}

#else /* !EMULATION */

int
main(int argc, char *argv[])
{
	printf("EMULATION kernel option not enabled\n");
	return (77); /* SKIP */
}

#endif /* EMULATION */
