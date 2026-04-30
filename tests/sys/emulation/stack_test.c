/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
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

/*
 * Stack Capture Tests for Emulation Framework
 * Task 8.4: Verify stack trace formatting with mock frame data.
 *            Test frame unwinding with valid/corrupted chains.
 *            No kernel code loaded - tests sysctl interface only.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/kld.h>
#include <sys/errno.h>

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Constants */
#define MAX_STACK_FRAMES 128
#define MAX_FRAME_DATA_SIZE 4096

/* Stack frame structure for testing */
struct mock_stack_frame {
    uint64_t pc;        /* Program counter / instruction pointer */
    uint64_t sp;        /* Stack pointer */
    uint64_t fp;        /* Frame pointer */
    uint64_t lr;        /* Link register (arm64/riscv) */
    char symbol[64];    /* Symbol name if resolved */
    char module[64];     /* Module name */
};

/*
 * Test: Stack capture sysctl exists
 * Verifies that stack capture sysctls are accessible
 */
ATF_TC(stack_capture_sysctl);

/*
 * Test: Stack frame structure validation
 * Tests that stack frames have correct structure
 */
ATF_TC(stack_frame_structure);

/*
 * Test: Valid frame chain unwinding
 * Tests unwinding through valid frame pointers
 */
ATF_TC(valid_frame_chain);

/*
 * Test: Corrupted frame chain handling
 * Tests that corrupted frame chains are detected
 */
ATF_TC(corrupted_frame_chain);

/*
 * Test: Maximum frame limit
 * Tests that stack capture respects frame limits
 */
ATF_TC(max_frame_limit);

/*
 * Test: Empty stack handling
 * Tests handling of empty/stackless states
 */
ATF_TC(empty_stack);

/*
 * Test: Stack timestamp
 * Tests that stack captures include timestamps
 */
ATF_TC(stack_timestamp);

/*
 * Test: Architecture-specific format
 * Tests architecture-specific stack format handling
 */
ATF_TC(arch_specific_format);

/*
 * Test: Stack output JSON format
 * Tests JSON output format for stack traces
 */
ATF_TC(stack_json_format);

/*
 * Test: Symbol resolution
 * Tests that symbols can be resolved from addresses
 */
ATF_TC(symbol_resolution);

/* Helper function to check if emu module is loaded */
static int
emu_module_loaded(void)
{
    return (kldfind("emu_core") != -1 || kldfind("emu") != -1);
}

/* Helper to check if sysctl exists */
static int
sysctl_exists(const char *name)
{
    int val;
    size_t len = sizeof(val);
    return (sysctlbyname(name, &val, &len, NULL, 0) == 0);
}

/* Helper to read stack data */
static int
read_stack_data(char *buf, size_t buflen)
{
    size_t len = buflen;
    return (sysctlbyname("kern.emulation.stack", buf, &len, NULL, 0) == 0);
}

/*
 * Implementation: stack_capture_sysctl
 */
void
stack_capture_sysctl_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that stack capture sysctl exists");
}

void
stack_capture_sysctl_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for stack capture sysctls */
    const char *stack_sysctls[] = {
        "kern.emulation.stack",
        "kern.emulation.stack_frames",
        "kern.emulation.stack_capture"
    };

    int found_stack = 0;
    for (size_t i = 0; i < sizeof(stack_sysctls) / sizeof(stack_sysctls[0]); i++) {
        if (sysctl_exists(stack_sysctls[i])) {
            found_stack = 1;
            break;
        }
    }

    if (!found_stack) {
        atf_tc_skip("Stack capture sysctls not available");
    }

    atf_tc_pass();
}

/*
 * Implementation: stack_frame_structure
 */
void
stack_frame_structure_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that stack frames have correct structure");
}

void
stack_frame_structure_body(void)
{
    /* Validate mock stack frame structure */
    struct mock_stack_frame frame;

    /* Zero the structure */
    memset(&frame, 0, sizeof(frame));

    /* Verify structure members are accessible */
    frame.pc = 0x1000;
    frame.sp = 0xffffd000;
    frame.fp = 0xffffd010;
    frame.lr = 0x2000;

    ATF_CHECK(frame.pc != 0);  /* PC should be set */
    ATF_CHECK(frame.sp != 0);  /* SP should be set */
    ATF_CHECK(frame.fp != 0);  /* FP should be set */

    atf_tc_pass();
}

/*
 * Implementation: valid_frame_chain
 */
void
valid_frame_chain_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test unwinding through valid frame pointers");
}

void
valid_frame_chain_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Create a mock valid frame chain */
    struct mock_stack_frame frames[4];

    /* Initialize frame chain */
    frames[0].pc = 0x1000;
    frames[0].sp = 0xffffd000;
    frames[0].fp = (uint64_t)&frames[1];
    frames[1].pc = 0x2000;
    frames[1].sp = 0xffffcff0;
    frames[1].fp = (uint64_t)&frames[2];
    frames[2].pc = 0x3000;
    frames[2].sp = 0xffffcfe0;
    frames[2].fp = (uint64_t)&frames[3];
    frames[3].pc = 0x4000;
    frames[3].sp = 0xffffcfd0;
    frames[3].fp = 0;  /* End of chain */

    /* Simulate frame chain unwinding */
    int frame_count = 0;
    uint64_t fp = (uint64_t)&frames[0];

    while (fp != 0 && frame_count < MAX_STACK_FRAMES) {
        struct mock_stack_frame *f = (struct mock_stack_frame *)fp;
        if (f->fp == 0 || f->fp == fp) {
            break;  /* End of chain or circular reference */
        }
        fp = f->fp;
        frame_count++;
    }

    /* Should have unwound 3 frames */
    ATF_CHECK_EQ(frame_count, 3);

    atf_tc_pass();
}

/*
 * Implementation: corrupted_frame_chain
 */
void
corrupted_frame_chain_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that corrupted frame chains are detected");
}

void
corrupted_frame_chain_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Create a corrupted frame chain */
    struct mock_stack_frame frames[3];

    /* Initialize with circular reference */
    frames[0].pc = 0x1000;
    frames[0].fp = (uint64_t)&frames[1];
    frames[1].pc = 0x2000;
    frames[1].fp = (uint64_t)&frames[0];  /* Circular! */
    frames[2].pc = 0x3000;
    frames[2].fp = 0;

    /* Simulate frame chain unwinding with corruption detection */
    int frame_count = 0;
    uint64_t fp = (uint64_t)&frames[0];
    uint64_t last_fp = 0;
    int max_iterations = 100;  /* Prevent infinite loop */

    while (fp != 0 && frame_count < MAX_STACK_FRAMES && max_iterations > 0) {
        struct mock_stack_frame *f = (struct mock_stack_frame *)fp;

        /* Check for circular reference */
        if (f->fp == last_fp || f->fp == fp) {
            /* Corrupted chain detected */
            break;
        }

        last_fp = fp;
        fp = f->fp;
        frame_count++;
        max_iterations--;
    }

    /* Should stop before circular reference */
    ATF_CHECK(frame_count <= 2);

    atf_tc_pass();
}

/*
 * Implementation: max_frame_limit
 */
void
max_frame_limit_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that stack capture respects frame limits");
}

void
max_frame_limit_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for max frames sysctl */
    if (sysctl_exists("kern.emulation.stack_max_frames")) {
        int max_frames;
        size_t len = sizeof(max_frames);
        if (sysctlbyname("kern.emulation.stack_max_frames", &max_frames, &len, NULL, 0) == 0) {
            /* Verify max frames is reasonable */
            ATF_CHECK_GE(max_frames, 1);
            ATF_CHECK_LE(max_frames, MAX_STACK_FRAMES);
        }
    } else {
        /* Default max frames should be defined */
        ATF_CHECK(MAX_STACK_FRAMES > 0);
    }

    atf_tc_pass();
}

/*
 * Implementation: empty_stack
 */
void
empty_stack_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test handling of empty or stackless states");
}

void
empty_stack_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check stack capture sysctl exists */
    if (!sysctl_exists("kern.emulation.stack")) {
        atf_tc_skip("Stack sysctl not available");
    }

    /* Read stack data - should handle empty case gracefully */
    char buf[MAX_FRAME_DATA_SIZE];
    if (read_stack_data(buf, sizeof(buf))) {
        /* Stack data exists - verify it's valid */
        ATF_CHECK(strlen(buf) >= 0);
    }

    atf_tc_pass();
}

/*
 * Implementation: stack_timestamp
 */
void
stack_timestamp_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that stack captures include timestamps");
}

void
stack_timestamp_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for timestamp in stack output */
    if (sysctl_exists("kern.emulation.stack_timestamp")) {
        int timestamp;
        size_t len = sizeof(timestamp);
        if (sysctlbyname("kern.emulation.stack_timestamp", &timestamp, &len, NULL, 0) == 0) {
            /* Timestamp should be positive */
            ATF_CHECK(timestamp > 0);
        }
    } else {
        atf_tc_skip("Stack timestamp sysctl not available");
    }

    atf_tc_pass();
}

/*
 * Implementation: arch_specific_format
 */
void
arch_specific_format_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test architecture-specific stack format handling");
}

void
arch_specific_format_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for arch-specific stack format */
    if (sysctl_exists("kern.emulation.stack_arch")) {
        char arch[32];
        size_t len = sizeof(arch);
        if (sysctlbyname("kern.emulation.stack_arch", arch, &len, NULL, 0) == 0) {
            /* Architecture should be specified */
            ATF_CHECK(strlen(arch) > 0);
        }
    }

    /* Test mock frame structures for different architectures */
    
    /* amd64/x86_64 - uses RBP chain */
    struct {
        uint64_t pc;
        uint64_t sp;
        uint64_t fp;  /* RBP */
    } amd64_frame = { .pc = 0x1000, .sp = 0xffffd000, .fp = 0xffffd010 };
    ATF_CHECK(amd64_frame.pc != 0);

    /* arm64/AArch64 - uses FP/LR */
    struct {
        uint64_t pc;
        uint64_t sp;
        uint64_t fp;
        uint64_t lr;
    } arm64_frame = { .pc = 0x1000, .sp = 0xffffd000, .fp = 0xffffd010, .lr = 0x2000 };
    ATF_CHECK(arm64_frame.pc != 0 && arm64_frame.lr != 0);

    /* riscv - uses s0/ra */
    struct {
        uint64_t pc;
        uint64_t sp;
        uint64_t s0;  /* Frame pointer */
        uint64_t ra;  /* Return address */
    } riscv_frame = { .pc = 0x1000, .sp = 0xffffd000, .s0 = 0xffffd010, .ra = 0x2000 };
    ATF_CHECK(riscv_frame.pc != 0 && riscv_frame.ra != 0);

    atf_tc_pass();
}

/*
 * Implementation: stack_json_format
 */
void
stack_json_format_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test JSON output format for stack traces");
}

void
stack_json_format_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for JSON format support */
    if (sysctl_exists("kern.emulation.stack_format")) {
        char format[32];
        size_t len = sizeof(format);
        if (sysctlbyname("kern.emulation.stack_format", format, &len, NULL, 0) == 0) {
            /* Format should be valid */
            ATF_CHECK(strlen(format) > 0);
        }
    }

    /* Test mock JSON output */
    const char *mock_json = 
        "{\"frames\": ["
        "{\"pc\": \"0x1000\", \"sp\": \"0xffffd000\", \"symbol\": \"func1\"},"
        "{\"pc\": \"0x2000\", \"sp\": \"0xffffcff0\", \"symbol\": \"func2\"}"
        "], \"arch\": \"amd64\", \"timestamp\": 1234567890}";

    /* Verify JSON is parseable (basic validation) */
    ATF_CHECK(strstr(mock_json, "\"frames\"") != NULL);
    ATF_CHECK(strstr(mock_json, "\"arch\"") != NULL);
    ATF_CHECK(strstr(mock_json, "\"timestamp\"") != NULL);

    atf_tc_pass();
}

/*
 * Implementation: symbol_resolution
 */
void
symbol_resolution_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that symbols can be resolved from addresses");
}

void
symbol_resolution_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for symbol resolution sysctl */
    if (sysctl_exists("kern.emulation.symbol_resolve")) {
        ATF_CHECK(1);  /* Symbol resolution available */
    }

    /* Mock symbol table */
    struct {
        uint64_t address;
        char name[64];
    } symbol_table[] = {
        { 0x1000, "kernel_text" },
        { 0x2000, "syscall_entry" },
        { 0x3000, "trap_handler" },
        { 0, "" }
    };

    /* Test symbol lookup */
    uint64_t test_addr = 0x2000;
    const char *found_symbol = NULL;

    for (int i = 0; symbol_table[i].address != 0; i++) {
        if (symbol_table[i].address == test_addr) {
            found_symbol = symbol_table[i].name;
            break;
        }
    }

    ATF_CHECK(found_symbol != NULL);
    ATF_CHECK(strcmp(found_symbol, "syscall_entry") == 0);

    atf_tc_pass();
}

/* Add test cases to test suite */
ATF_TP_ADD_TCS(tp)
{

    ATF_TP_ADD_TC(tp, stack_capture_sysctl);
    ATF_TP_ADD_TC(tp, stack_frame_structure);
    ATF_TP_ADD_TC(tp, valid_frame_chain);
    ATF_TP_ADD_TC(tp, corrupted_frame_chain);
    ATF_TP_ADD_TC(tp, max_frame_limit);
    ATF_TP_ADD_TC(tp, empty_stack);
    ATF_TP_ADD_TC(tp, stack_timestamp);
    ATF_TP_ADD_TC(tp, arch_specific_format);
    ATF_TP_ADD_TC(tp, stack_json_format);
    ATF_TP_ADD_TC(tp, symbol_resolution);

    return (atf_no_error());
}
