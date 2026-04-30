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

# Cross-Architecture Module Loading Integration Test (S5.10-5.15, S8.10)
#
# Test loading modules compiled for non-native architectures.
# Verifies that the emulator handles cross-architecture binaries correctly
# and that modules load and function as expected.

. $(atf_get_srcdir)/utils.subr

# List of supported architectures
SUPPORTED_ARCHS="amd64 arm arm64 i386 powerpc riscv"

# Test 1: Architecture detection
atf_test_case arch_detection cleanup
arch_detection_head() {
	atf_set "descr" "Test host architecture detection"
	atf_set "require.user" "root"
}
arch_detection_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Verify emu binary exists
	if [ ! -x "${EMU_BINARY}" ]; then
		atf_skip "emu binary not installed"
	fi

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Verify host architecture is recognized
	case "${host_arch}" in
	amd64|i386|arm|arm64|aarch64|powerpc|riscv64)
		;;
	*)
		atf_skip "Unknown host architecture: ${host_arch}"
		;;
	esac

	# Verify emu can report its supported architectures
	if "${EMU_BINARY}" --help 2>&1 | grep -qE "arch|ARCH"; then
		# Good - there's architecture help
		:
	else
		atf_skip "Architecture information not available in emu --help"
	fi
}
arch_detection_cleanup() {
	:
}

# Test 2: Native architecture module loading
atf_test_case native_arch_load cleanup
native_arch_load_head() {
	atf_set "descr" "Test loading module for native architecture"
	atf_set "require.user" "root"
}
native_arch_load_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Normalize architecture name for emu
	case "${host_arch}" in
	aarch64) host_arch="arm64" ;;
	esac

	# Start emulator instance for native architecture
	local inst_id
	inst_id=$(emu_start_test_instance --arch ${host_arch})

	# Verify instance started
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Failed to start native architecture instance"
	fi

	# Verify architecture is correct
	local inst_arch
	inst_arch=$(sysctl -n "kern.emulation.instance.${inst_id}.arch" 2>/dev/null)
	if [ "${inst_arch}" != "${host_arch}" ]; then
		atf_fail "Instance architecture mismatch: expected ${host_arch}, got ${inst_arch}"
	fi
}
native_arch_load_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 3: Cross-architecture instance creation
atf_test_case cross_arch_instance cleanup
cross_arch_instance_head() {
	atf_set "descr" "Test creating instance for non-native architecture"
	atf_set "require.user" "root"
}
cross_arch_instance_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Find a different architecture to test
	local target_arch
	case "${host_arch}" in
	amd64)
		target_arch="arm64"
		;;
	arm64|aarch64)
		target_arch="amd64"
		;;
	*)
		atf_skip "Need at least two architectures for cross-arch testing"
		;;
	esac

	# Verify target architecture is supported
	if ! echo "${SUPPORTED_ARCHS}" | grep -q "${target_arch}"; then
		atf_skip "Target architecture ${target_arch} not supported"
	fi

	# Start emulator instance for target architecture
	local inst_id
	inst_id=$(emu_start_test_instance --arch ${target_arch})

	# Verify instance started
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Failed to start ${target_arch} instance"
	fi

	# Verify architecture is correct
	local inst_arch
	inst_arch=$(sysctl -n "kern.emulation.instance.${inst_id}.arch" 2>/dev/null)
	if [ "${inst_arch}" != "${target_arch}" ]; then
		atf_fail "Instance architecture mismatch: expected ${target_arch}, got ${inst_arch}"
	fi
}
cross_arch_instance_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 4: Module format validation
atf_test_case module_format_validation cleanup
module_format_validation_head() {
	atf_set "descr" "Test module format validation for different architectures"
	atf_set "require.user" "root"
}
module_format_validation_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Create a test module directory
	emu_create_test_dir

	# Verify emu has module validation capability
	if ! "${EMU_BINARY}" module --help >/dev/null 2>&1; then
		atf_skip "Module subcommand not available"
	fi

	# Try to validate a non-existent module (should fail gracefully)
	local result
	result=$("${EMU_BINARY}" module validate "${EMU_TEST_DIR}/nonexistent.ko" 2>&1)
	if [ $? -eq 0 ]; then
		atf_fail "Should fail to validate non-existent module"
	fi

	# Verify error message is meaningful
	if echo "${result}" | grep -qE "not found|no such file"; then
		# Good - clear error message
		:
	else
		atf_fail "Error message should indicate file not found"
	fi
}
module_format_validation_cleanup() {
	emu_cleanup_test_dir
}

# Test 5: Module loading verification
atf_test_case module_load_verification cleanup
module_load_verification_head() {
	atf_set "descr" "Test module loading verification"
	atf_set "require.user" "root"
}
module_load_verification_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify module is not loaded initially
	if emu_module_loaded ${inst_id} test_module; then
		atf_fail "Module should not be loaded initially"
	fi

	# Note: Actual module loading would require a real test module
	# For now, verify the interface exists
	local module_list
	module_list=$(sysctl -n "kern.emulation.instance.${inst_id}.modules" 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "Module list sysctl not available"
	fi

	# Verify module list is accessible
	if [ -z "${module_list}" ]; then
		atf_fail "Module list should be accessible"
	fi
}
module_load_verification_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 6: Cross-architecture CPU emulation
atf_test_case cross_arch_cpu_emulation cleanup
cross_arch_cpu_emulation_head() {
	atf_set "descr" "Test CPU emulation for cross-architecture instance"
	atf_set "require.user" "root"
}
cross_arch_cpu_emulation_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Find a different architecture
	local target_arch
	case "${host_arch}" in
	amd64) target_arch="arm64" ;;
	arm64|aarch64) target_arch="amd64" ;;
	*) atf_skip "Need different architectures for cross-arch testing" ;;
	esac

	# Start emulator instance for target architecture
	local inst_id
	inst_id=$(emu_start_test_instance --arch ${target_arch})

	# Verify instance is running
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Failed to start ${target_arch} instance"
	fi

	# Get instance status
	local status
	status=$(sysctl -n "kern.emulation.instance.${inst_id}.status" 2>/dev/null)

	# Verify instance is in running state
	if [ "${status}" != "running" ]; then
		atf_fail "Instance should be running, got status: ${status}"
	fi
}
cross_arch_cpu_emulation_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 7: Cross-architecture memory handling
atf_test_case cross_arch_memory cleanup
cross_arch_memory_head() {
	atf_set "descr" "Test memory handling for cross-architecture instance"
	atf_set "require.user" "root"
}
cross_arch_memory_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify memory configuration is accessible
	local memory
	memory=$(sysctl -n "kern.emulation.instance.${inst_id}.memory" 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "Memory sysctl not available"
	fi

	# Verify memory value is reasonable
	if [ "${memory}" -lt 64 ] || [ "${memory}" -gt 1048576 ]; then
		atf_fail "Memory should be between 64MB and 1TB, got ${memory}"
	fi
}
cross_arch_memory_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 8: Architecture compatibility checking
atf_test_case arch_compat_check cleanup
arch_compat_check_head() {
	atf_set "descr" "Test architecture compatibility checking"
	atf_set "require.user" "root"
}
arch_compat_check_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Verify emulation capability detection
	local caps
	caps=$(sysctl -n kern.emulation.caps 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "Capabilities sysctl not available"
	fi

	# Verify host architecture is reported
	if ! echo "${caps}" | grep -qE "host_arch|native_arch"; then
		atf_fail "Capabilities should include host architecture"
	fi

	# Verify emulation capabilities are listed
	if ! echo "${caps}" | grep -qE "emulation|binary_translation"; then
		atf_fail "Capabilities should include emulation support"
	fi
}
arch_compat_check_cleanup() {
	:
}

# Test 9: Multiple cross-arch instances
atf_test_case multi_cross_arch_instances cleanup
multi_cross_arch_instances_head() {
	atf_set "descr" "Test running multiple cross-architecture instances"
	atf_set "require.user" "root"
}
multi_cross_arch_instances_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Check what other architectures are available
	local target_arch1 target_arch2
	case "${host_arch}" in
	amd64)
		target_arch1="arm64"
		target_arch2="i386"
		;;
	arm64|aarch64)
		target_arch1="amd64"
		target_arch2="arm"
		;;
	*)
		atf_skip "Need at least three architectures for this test"
		;;
	esac

	# Start instances for different architectures
	local inst1 inst2
	inst1=$(emu_start_test_instance --arch ${target_arch1})
	inst2=$(emu_start_test_instance --arch ${target_arch2})

	# Verify both instances are running
	if ! emu_instance_exists ${inst1}; then
		atf_fail "First instance failed to start"
	fi

	if ! emu_instance_exists ${inst2}; then
		atf_fail "Second instance failed to start"
	fi

	# Verify architectures are correct
	local arch1 arch2
	arch1=$(sysctl -n "kern.emulation.instance.${inst1}.arch" 2>/dev/null)
	arch2=$(sysctl -n "kern.emulation.instance.${inst2}.arch" 2>/dev/null)

	if [ "${arch1}" != "${target_arch1}" ]; then
		atf_fail "First instance architecture mismatch"
	fi

	if [ "${arch2}" != "${target_arch2}" ]; then
		atf_fail "Second instance architecture mismatch"
	fi
}
multi_cross_arch_instances_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 10: Invalid architecture rejection
atf_test_case invalid_arch_rejection cleanup
invalid_arch_rejection_head() {
	atf_set "descr" "Test rejection of invalid architecture"
	atf_set "require.user" "root"
}
invalid_arch_rejection_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Try to create instance with invalid architecture
	local result
	result=$("${EMU_BINARY}" init test_invalid_arch --arch nonexistent_arch 2>&1)

	# Should fail
	if [ $? -eq 0 ]; then
		atf_fail "Should reject invalid architecture"
	fi

	# Verify error message mentions architecture
	if ! echo "${result}" | grep -qiE "arch|architecture|unsupported"; then
		atf_fail "Error message should mention architecture"
	fi
}
invalid_arch_rejection_cleanup() {
	:
}

# Test 11: Architecture-specific sysctl paths
atf_test_case arch_specific_sysctl cleanup
arch_specific_sysctl_head() {
	atf_set "descr" "Test architecture-specific sysctl paths"
	atf_set "require.user" "root"
}
arch_specific_sysctl_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify architecture-specific sysctls exist
	# These should be different for each architecture

	# CPU type sysctl
	local cpu_type
	cpu_type=$(sysctl -n "kern.emulation.instance.${inst_id}.cpu_type" 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "cpu_type sysctl not available"
	fi

	# CPU features sysctl (architecture-specific format)
	local cpu_features
	cpu_features=$(sysctl -n "kern.emulation.instance.${inst_id}.cpu_features" 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "cpu_features sysctl not available"
	fi
}
arch_specific_sysctl_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 12: Cross-arch binary format handling
atf_test_case binary_format_handling cleanup
binary_format_handling_head() {
	atf_set "descr" "Test binary format handling for different architectures"
	atf_set "require.user" "root"
}
binary_format_handling_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Verify ELF class/type detection
	# ELF files have different formats for different architectures

	# Create a mock ELF header test
	emu_create_test_dir

	# Note: This test would ideally verify that emu can correctly
	# identify ELF headers for different architectures.
	# For now, we just verify the interface exists.

	local elf_detect
	elf_detect=$(sysctl -n kern.emulation.elf_detect 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "ELF detection sysctl not available"
	fi

	# Verify ELF detection is enabled
	if [ "${elf_detect}" != "1" ]; then
		atf_fail "ELF detection should be enabled"
	fi
}
binary_format_handling_cleanup() {
	emu_cleanup_test_dir
}

# Main test harness
atf_init_test_cases() {
	atf_add_test_case arch_detection
	atf_add_test_case native_arch_load
	atf_add_test_case cross_arch_instance
	atf_add_test_case module_format_validation
	atf_add_test_case module_load_verification
	atf_add_test_case cross_arch_cpu_emulation
	atf_add_test_case cross_arch_memory
	atf_add_test_case arch_compat_check
	atf_add_test_case multi_cross_arch_instances
	atf_add_test_case invalid_arch_rejection
	atf_add_test_case arch_specific_sysctl
	atf_add_test_case binary_format_handling
}
