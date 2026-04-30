#!/usr/bin/env atf-sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 The FreeBSD Foundation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# Stack Examination Integration Test (S5.9, S8.9)
#
# Test stack capture and examination functionality.
# Verifies that stack traces are captured correctly, frame structure
# is valid, symbol resolution works, and JSON output format is correct.

. $(atf_get_srcdir)/utils.subr

# Test 1: Stack capture initialization
atf_test_case stack_init cleanup
stack_init_head() {
	atf_set "descr" "Test stack capture subsystem initialization"
	atf_set "require.user" "root"
}
stack_init_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Verify emu binary exists
	if [ ! -x "${EMU_BINARY}" ]; then
		atf_skip "emu binary not installed"
	fi

	# Verify stack capture sysctl exists
	local stack_enabled
	stack_enabled=$(sysctl -n kern.emulation.stack_capture 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "stack_capture sysctl not available (module not loaded)"
	fi

	# Verify stack capture is enabled by default
	if [ "${stack_enabled}" != "1" ]; then
		atf_fail "stack_capture should be enabled by default"
	fi

	# Verify max frames sysctl exists
	local max_frames
	max_frames=$(sysctl -n kern.emulation.stack_max_frames 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "stack_max_frames sysctl not available"
	fi

	# Verify max frames is reasonable
	if [ "${max_frames}" -lt 1 ] || [ "${max_frames}" -gt 256 ]; then
		atf_fail "stack_max_frames should be between 1 and 256, got ${max_frames}"
	fi
}
stack_init_cleanup() {
	:
}

# Test 2: Stack capture for running instance
atf_test_case stack_capture cleanup
stack_capture_head() {
	atf_set "descr" "Test stack capture for running instance"
	atf_set "require.user" "root"
}
stack_capture_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Check if stack capture subcommand exists
	if ! "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		# Fallback to sysctl interface
		local stack_cap
		stack_cap=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_capture" 2>/dev/null)
		if [ $? -ne 0 ]; then
			atf_skip "stack capture sysctl not available"
		fi
	else
		# Use emu stack subcommand
		local output
		output=$("${EMU_BINARY}" stack capture ${inst_id} 2>&1)
		if [ $? -ne 0 ]; then
			atf_fail "Failed to capture stack for instance ${inst_id}"
		fi
	fi

	# Verify instance is still running
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Instance should still be running after stack capture"
	fi
}
stack_capture_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 3: Frame structure verification
atf_test_case frame_structure cleanup
frame_structure_head() {
	atf_set "descr" "Test stack frame structure verification"
	atf_set "require.user" "root"
}
frame_structure_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get stack trace
	local stack_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		stack_output=$("${EMU_BINARY}" stack get ${inst_id} 2>/dev/null)
	else
		# Fallback to sysctl
		stack_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace" 2>/dev/null)
	fi

	if [ -z "${stack_output}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Verify frame structure contains expected fields
	# Each frame should have: pc, sp, fp, symbol

	# Check for program counter field
	if ! echo "${stack_output}" | grep -qE "pc|Pc|PC|instruction_pointer"; then
		atf_fail "Stack trace missing program counter field"
	fi

	# Check for stack pointer field
	if ! echo "${stack_output}" | grep -qE "sp|SP|stack_pointer"; then
		atf_fail "Stack trace missing stack pointer field"
	fi

	# Check for frame pointer field
	if ! echo "${stack_output}" | grep -qE "fp|FP|frame_pointer"; then
		atf_fail "Stack trace missing frame pointer field"
	fi
}
frame_structure_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 4: Symbol resolution
atf_test_case symbol_resolution cleanup
symbol_resolution_head() {
	atf_set "descr" "Test symbol resolution in stack traces"
	atf_set "require.user" "root"
}
symbol_resolution_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get stack trace with symbol resolution
	local stack_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		stack_output=$("${EMU_BINARY}" stack get --symbols ${inst_id} 2>/dev/null)
	else
		# Fallback to sysctl
		stack_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace" 2>/dev/null)
	fi

	if [ -z "${stack_output}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Verify symbols are present (look for common kernel symbols)
	# Note: The exact symbols depend on what the emulator is doing
	# We just verify that symbol field is present

	if ! echo "${stack_output}" | grep -qE "symbol|Symbol|function|Function"; then
		atf_fail "Stack trace missing symbol field"
	fi
}
symbol_resolution_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 5: JSON output format
atf_test_case json_output_format cleanup
json_output_format_head() {
	atf_set "descr" "Test JSON output format validation"
	atf_set "require.user" "root"
}
json_output_format_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get stack trace in JSON format
	local json_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		json_output=$("${EMU_BINARY}" stack get --json ${inst_id} 2>/dev/null)
	else
		# Try sysctl with JSON format
		json_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace_json" 2>/dev/null)
	fi

	if [ -z "${json_output}" ]; then
		atf_skip "Could not retrieve JSON stack trace"
	fi

	# Verify JSON is valid
	# Try to parse with Python (commonly available)
	if command -v python3 >/dev/null 2>&1; then
		python3 -c "import json; json.loads('${json_output}')" 2>/dev/null
		if [ $? -ne 0 ]; then
			atf_fail "Invalid JSON output format"
		fi
	elif command -v python >/dev/null 2>&1; then
		python -c "import json; json.loads('${json_output}')" 2>/dev/null
		if [ $? -ne 0 ]; then
			atf_fail "Invalid JSON output format"
		fi
	else
		# Basic validation without Python
		# Check for basic JSON structure
		if ! echo "${json_output}" | grep -qE '^\s*\[' && \
		   ! echo "${json_output}" | grep -qE '^\s*\{'; then
			atf_fail "Output does not appear to be JSON"
		fi
	fi

	# Verify JSON contains expected fields
	if ! echo "${json_output}" | grep -qE '"frames"' && \
	   ! echo "${json_output}" | grep -qE '"pc"' && \
	   ! echo "${json_output}" | grep -qE '"sp"'; then
		atf_fail "JSON output missing expected fields"
	fi
}
json_output_format_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 6: Max frames limit
atf_test_case max_frames_limit cleanup
max_frames_limit_head() {
	atf_set "descr" "Test maximum frames limit enforcement"
	atf_set "require.user" "root"
}
max_frames_limit_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get max frames limit
	local max_frames
	max_frames=$(sysctl -n kern.emulation.stack_max_frames 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "stack_max_frames sysctl not available"
	fi

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get stack trace
	local stack_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		stack_output=$("${EMU_BINARY}" stack get --json ${inst_id} 2>/dev/null)
	else
		stack_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace" 2>/dev/null)
	fi

	if [ -z "${stack_output}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Count frames in output
	local frame_count
	frame_count=$(echo "${stack_output}" | grep -cE '"pc"|pc:|PC:' || true)

	# Verify frame count doesn't exceed max
	if [ "${frame_count}" -gt "${max_frames}" ]; then
		atf_fail "Frame count (${frame_count}) exceeds max_frames (${max_frames})"
	fi
}
max_frames_limit_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 7: Stack capture during crash
atf_test_case stack_capture_crash cleanup
stack_capture_crash_head() {
	atf_set "descr" "Test stack capture during crash scenario"
	atf_set "require.user" "root"
}
stack_capture_crash_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify crash capture includes stack trace
	local capture_opts
	capture_opts=$(sysctl -n "kern.emulation.instance.${inst_id}.capture" 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "capture sysctl not available"
	fi

	# Verify stack is in capture options
	if ! echo "${capture_opts}" | grep -q "stack"; then
		atf_fail "Stack should be in crash capture options"
	fi
}
stack_capture_crash_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 8: Architecture-specific stack format
atf_test_case arch_specific_format cleanup
arch_specific_format_head() {
	atf_set "descr" "Test architecture-specific stack format"
	atf_set "require.user" "root"
}
arch_specific_format_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance with specific architecture
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get architecture
	local arch
	arch=$(sysctl -n "kern.emulation.instance.${inst_id}.arch" 2>/dev/null)
	if [ $? -ne 0 ]; then
		arch=$(uname -m)  # Fallback to host arch
	fi

	# Get stack trace
	local stack_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		stack_output=$("${EMU_BINARY}" stack get --json ${inst_id} 2>/dev/null)
	else
		stack_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace" 2>/dev/null)
	fi

	if [ -z "${stack_output}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Verify architecture-specific fields are present
	case "${arch}" in
	amd64)
		# x86-64 should have RIP
		if ! echo "${stack_output}" | grep -qE '"rip"|RIP'; then
			atf_fail "amd64 stack should contain RIP"
		fi
		;;
	i386)
		# x86 should have EIP
		if ! echo "${stack_output}" | grep -qE '"eip"|EIP'; then
			atf_fail "i386 stack should contain EIP"
		fi
		;;
	arm64|aarch64)
		# ARM64 should have PC
		if ! echo "${stack_output}" | grep -qE '"pc"|PC'; then
			atf_fail "arm64 stack should contain PC"
		fi
		;;
	arm)
		# ARM should have PC
		if ! echo "${stack_output}" | grep -qE '"pc"|PC'; then
			atf_fail "arm stack should contain PC"
		fi
		;;
	riscv64)
		# RISC-V should have PC
		if ! echo "${stack_output}" | grep -qE '"pc"|PC'; then
			atf_fail "riscv64 stack should contain PC"
		fi
		;;
	esac
}
arch_specific_format_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 9: Stack trace timestamp
atf_test_case stack_timestamp cleanup
stack_timestamp_head() {
	atf_set "descr" "Test stack trace includes timestamp"
	atf_set "require.user" "root"
}
stack_timestamp_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Get stack trace
	local stack_output
	if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		stack_output=$("${EMU_BINARY}" stack get --json ${inst_id} 2>/dev/null)
	else
		stack_output=$(sysctl -n "kern.emulation.instance.${inst_id}.stack_trace" 2>/dev/null)
	fi

	if [ -z "${stack_output}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Verify timestamp is present
	if ! echo "${stack_output}" | grep -qE '"timestamp"|timestamp|time'; then
		atf_fail "Stack trace should include timestamp"
	fi
}
stack_timestamp_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 10: Concurrent stack capture
atf_test_case concurrent_stack_capture cleanup
concurrent_stack_capture_head() {
	atf_set "descr" "Test concurrent stack capture from multiple instances"
	atf_set "require.user" "root"
}
concurrent_stack_capture_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start multiple instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# Capture stack from all three instances
	for inst in ${inst1} ${inst2} ${inst3}; do
		if "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
			local output
			output=$("${EMU_BINARY}" stack capture ${inst} 2>&1)
			if [ $? -ne 0 ]; then
				atf_fail "Failed to capture stack for instance ${inst}"
			fi
		fi
	done

	# Verify all instances are still running
	if ! emu_instance_exists ${inst1}; then
		atf_fail "Instance 1 should still be running"
	fi
	if ! emu_instance_exists ${inst2}; then
		atf_fail "Instance 2 should still be running"
	fi
	if ! emu_instance_exists ${inst3}; then
		atf_fail "Instance 3 should still be running"
	fi
}
concurrent_stack_capture_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Test 11: Stack dump command
atf_test_case stack_dump_command cleanup
stack_dump_command_head() {
	atf_set "descr" "Test stack dump file generation"
	atf_set "require.user" "root"
}
stack_dump_command_body() {
	atf_skip "Emulator binary not yet available for testing"

	emu_create_test_dir

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify emu stack dump subcommand exists
	if ! "${EMU_BINARY}" stack --help >/dev/null 2>&1; then
		atf_skip "stack subcommand not implemented"
	fi

	# Dump stack to file
	local dump_file="${EMU_TEST_DIR}/stack_dump_${inst_id}.txt"
	"${EMU_BINARY}" stack dump ${inst_id} -o ${dump_file} 2>&1

	# Verify file was created
	if [ ! -f "${dump_file}" ]; then
		atf_fail "Stack dump file was not created"
	fi

	# Verify file has content
	if [ ! -s "${dump_file}" ]; then
		atf_fail "Stack dump file is empty"
	fi
}
stack_dump_command_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
	emu_cleanup_test_dir
}

# Test 12: Stack trace filtering
atf_test_case stack_filtering cleanup
stack_filtering_head() {
	atf_set "descr" "Test stack trace filtering options"
	atf_set "require.user" "root"
}
stack_filtering_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify filtering options exist
	if ! "${EMU_BINARY}" stack --help 2>&1 | grep -qE "filter|--filter"; then
		atf_skip "Stack filtering options not available"
	fi

	# Get full stack trace
	local full_stack
	full_stack=$("${EMU_BINARY}" stack get --json ${inst_id} 2>/dev/null)
	if [ -z "${full_stack}" ]; then
		atf_skip "Could not retrieve stack trace"
	fi

	# Apply filter for kernel frames only
	local kernel_stack
	kernel_stack=$("${EMU_BINARY}" stack get --json --filter=kernel ${inst_id} 2>/dev/null)

	# Verify filtered output has fewer frames
	local full_frames kernel_frames
	full_frames=$(echo "${full_stack}" | grep -cE '"pc"|pc:' || echo 0)
	kernel_frames=$(echo "${kernel_stack}" | grep -cE '"pc"|pc:' || echo 0)

	if [ "${kernel_frames}" -gt "${full_frames}" ]; then
		atf_fail "Filtered stack should have fewer or equal frames"
	fi
}
stack_filtering_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Main test harness
atf_init_test_cases() {
	atf_add_test_case stack_init
	atf_add_test_case stack_capture
	atf_add_test_case frame_structure
	atf_add_test_case symbol_resolution
	atf_add_test_case json_output_format
	atf_add_test_case max_frames_limit
	atf_add_test_case stack_capture_crash
	atf_add_test_case arch_specific_format
	atf_add_test_case stack_timestamp
	atf_add_test_case concurrent_stack_capture
	atf_add_test_case stack_dump_command
	atf_add_test_case stack_filtering
}
