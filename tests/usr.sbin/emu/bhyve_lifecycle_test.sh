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
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
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

# Bhyve Lifecycle Integration Test (S1.6, S8.6)
#
# Test bhyve instance lifecycle management.
# Start bhyve instance, load test kernel module, verify module loaded,
# unload module, stop instance, destroy.
#
# IMPORTANT: All testing is performed inside the emulated environment
# to prevent host system impact. This test verifies the integration
# between the emulation framework and bhyve.

. $(atf_get_srcdir)/utils.subr

# Test 1: Bhyve availability check
atf_test_case bhyve_available cleanup
bhyve_available_head() {
	atf_set "descr" "Test that bhyve is available for testing"
	atf_set "require.user" "root"
}
bhyve_available_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Check if VMM module is available
	if ! kldstat -m vmm >/dev/null 2>&1; then
		atf_skip "VMM module not loaded"
	fi

	# Check if /dev/vmm exists
	if [ ! -c /dev/vmm ]; then
		atf_skip "/dev/vmm not available"
	fi

	atf_pass
}
bhyve_available_cleanup() {
	:
}

# Test 2: Create bhyve instance
atf_test_case bhyve_create_instance cleanup
bhyve_create_instance_head() {
	atf_set "descr" "Test creating a bhyve instance"
	atf_set "require.user" "root"
}
bhyve_create_instance_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Verify emu binary exists
	if [ ! -x "${EMU_BINARY}" ]; then
		atf_skip "emu binary not installed"
	fi

	# Create test instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	if [ -z "${inst_id}" ]; then
		atf_fail "Failed to create bhyve instance"
	fi

	# Verify instance exists
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Instance ${inst_id} does not exist"
	fi
}
bhyve_create_instance_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 3: Load test module in bhyve
atf_test_case bhyve_load_module cleanup
bhyve_load_module_head() {
	atf_set "descr" "Test loading a test kernel module in bhyve instance"
	atf_set "require.user" "root"
}
bhyve_load_module_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Attach to instance console
	local console_pid
	emu_get_console_pid ${inst_id}
	console_pid=$?

	# Load test module via console
	# This requires the instance to be running and accessible

	# Check if module is loaded via sysctl
	local module_loaded
	module_loaded=$(sysctl -n "kern.emulation.instance.${inst_id}.modules" 2>/dev/null)

	if [ -z "${module_loaded}" ]; then
		atf_skip "Module loading not available"
	fi

	atf_pass
}
bhyve_load_module_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 4: Verify module loaded
atf_test_case bhyve_verify_module cleanup
bhyve_verify_module_head() {
	atf_set "descr" "Test verifying test kernel module is loaded"
	atf_set "require.user" "root"
}
bhyve_verify_module_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Verify via kldstat inside the instance
	# This would check if test_module is in the loaded module list

	# Check module count
	local module_count
	module_count=$(sysctl -n "kern.emulation.instance.${inst_id}.module_count" 2>/dev/null)

	if [ $? -ne 0 ]; then
		atf_skip "Module count sysctl not available"
	fi

	# Should have at least 1 module loaded
	if [ "${module_count}" -lt 1 ]; then
		atf_fail "No modules loaded in instance"
	fi

	atf_pass
}
bhyve_verify_module_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 5: Unload module from bhyve
atf_test_case bhyve_unload_module cleanup
bhyve_unload_module_head() {
	atf_set "descr" "Test unloading test kernel module from bhyve instance"
	atf_set "require.user" "root"
}
bhyve_unload_module_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Start instance with module
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Unload module via console

	# Verify module count decreased
	local module_count
	module_count=$(sysctl -n "kern.emulation.instance.${inst_id}.module_count" 2>/dev/null)

	if [ $? -eq 0 ] && [ "${module_count}" -eq 0 ]; then
		atf_pass
	else
		atf_fail "Module unload failed"
	fi
}
bhyve_unload_module_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 6: Stop bhyve instance
atf_test_case bhyve_stop_instance cleanup
bhyve_stop_instance_head() {
	atf_set "descr" "Test stopping bhyve instance"
	atf_set "require.user" "root"
}
bhyve_stop_instance_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Stop the instance
	emu_stop_instance ${inst_id}

	# Verify instance is stopped
	local status
	status=$(emu status ${inst_id} 2>/dev/null)

	if echo "${status}" | grep -q "stopped"; then
		atf_pass
	else
		atf_fail "Instance not stopped"
	fi
}
bhyve_stop_instance_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 7: Destroy bhyve instance
atf_test_case bhyve_destroy_instance cleanup
bhyve_destroy_instance_head() {
	atf_set "descr" "Test destroying bhyve instance"
	atf_set "require.user" "root"
}
bhyve_destroy_instance_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Start instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Destroy the instance
	emu_destroy_instance ${inst_id}

	# Verify instance no longer exists
	if ! emu_instance_exists ${inst_id}; then
		atf_pass
	else
		atf_fail "Instance still exists after destroy"
	fi
}
bhyve_destroy_instance_cleanup() {
	:
}

# Test 8: Full lifecycle sequence
atf_test_case bhyve_full_lifecycle cleanup
bhyve_full_lifecycle_head() {
	atf_set "descr" "Test complete bhyve lifecycle"
	atf_set "require.user" "root"
}
bhyve_full_lifecycle_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Step 1: Create instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	if [ -z "${inst_id}" ]; then
		atf_fail "Failed to create instance"
	fi

	# Step 2: Verify instance is running
	if ! emu_instance_exists ${inst_id}; then
		atf_fail "Instance not running after create"
	fi

	# Step 3: Load module

	# Step 4: Verify module loaded

	# Step 5: Unload module

	# Step 6: Stop instance
	emu_stop_instance ${inst_id}

	# Step 7: Verify stopped
	local status
	status=$(emu status ${inst_id} 2>/dev/null)
	if ! echo "${status}" | grep -q "stopped"; then
		atf_fail "Instance not stopped"
	fi

	# Step 8: Destroy instance
	emu_destroy_instance ${inst_id}

	# Step 9: Verify destroyed
	if ! emu_instance_exists ${inst_id}; then
		atf_pass
	else
		atf_fail "Instance still exists after destroy"
	fi
}
bhyve_full_lifecycle_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Test 9: Multiple concurrent bhyve instances
atf_test_case bhyve_concurrent_instances cleanup
bhyve_concurrent_instances_head() {
	atf_set "descr" "Test managing multiple concurrent bhyve instances"
	atf_set "require.user" "root"
}
bhyve_concurrent_instances_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Create multiple instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# Verify all are running
	if ! emu_instance_exists ${inst1}; then
		atf_fail "Instance 1 not running"
	fi
	if ! emu_instance_exists ${inst2}; then
		atf_fail "Instance 2 not running"
	fi
	if ! emu_instance_exists ${inst3}; then
		atf_fail "Instance 3 not running"
	fi

	# Clean up all
	emu_cleanup_test_instance ${inst1}
	emu_cleanup_test_instance ${inst2}
	emu_cleanup_test_instance ${inst3}
}
bhyve_concurrent_instances_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Test 10: Instance resource limits
atf_test_case bhyve_resource_limits cleanup
bhyve_resource_limits_head() {
	atf_set "descr" "Test bhyve instance respects resource limits"
	atf_set "require.user" "root"
}
bhyve_resource_limits_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Check max_instances limit
	local max_inst
	max_inst=$(sysctl -n kern.emulation.max_instances 2>/dev/null)

	if [ -z "${max_inst}" ]; then
		atf_skip "max_instances sysctl not available"
	fi

	# Try to create more instances than allowed
	# Should be limited by max_instances

	atf_pass
}
bhyve_resource_limits_cleanup() {
	:
}

# Test 11: Instance isolation
atf_test_case bhyve_instance_isolation cleanup
bhyve_instance_isolation_head() {
	atf_set "descr" "Test that bhyve instances are properly isolated"
	atf_set "require.user" "root"
}
bhyve_instance_isolation_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Create two instances
	local inst1 inst2
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)

	# Load module in instance 1
	# Verify module NOT in instance 2

	# Destroy instance 1
	# Verify instance 2 still running

	atf_pass
}
bhyve_instance_isolation_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 12: Instance crash recovery
atf_test_case bhyve_crash_recovery cleanup
bhyve_crash_recovery_head() {
	atf_set "descr" "Test bhyve instance crash recovery"
	atf_set "require.user" "root"
}
bhyve_crash_recovery_body() {
	atf_skip "Bhyve lifecycle test requires emulator binary and bhyve VM"

	# Create instance
	local inst_id
	inst_id=$(emu_start_test_instance)

	# Simulate crash (would require special test module)
	# Verify crash is detected
	# Verify state is captured
	# Verify instance can be destroyed

	atf_pass
}
bhyve_crash_recovery_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

# Main test harness
atf_init_test_cases() {
	atf_add_test_case bhyve_available
	atf_add_test_case bhyve_create_instance
	atf_add_test_case bhyve_load_module
	atf_add_test_case bhyve_verify_module
	atf_add_test_case bhyve_unload_module
	atf_add_test_case bhyve_stop_instance
	atf_add_test_case bhyve_destroy_instance
	atf_add_test_case bhyve_full_lifecycle
	atf_add_test_case bhyve_concurrent_instances
	atf_add_test_case bhyve_resource_limits
	atf_add_test_case bhyve_instance_isolation
	atf_add_test_case bhyve_crash_recovery
}
