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

# Multi-Instance Management Integration Test (S4.7, S6.7, S8.11)
#
# Test multi-instance management capabilities.
# Verifies that multiple instances can run simultaneously,
# instances operate independently, and operations on one
# instance don't affect others.

. $(atf_get_srcdir)/utils.subr

# Test 1: Multiple instance creation
atf_test_case multi_instance_creation cleanup
multi_instance_creation_head() {
	atf_set "descr" "Test creating multiple instances"
	atf_set "require.user" "root"
}
multi_instance_creation_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start first instance
	local inst1
	inst1=$(emu_start_test_instance)

	# Start second instance
	local inst2
	inst2=$(emu_start_test_instance)

	# Start third instance
	local inst3
	inst3=$(emu_start_test_instance)

	# Verify all instances exist
	if ! emu_instance_exists ${inst1}; then
		atf_fail "First instance failed to start"
	fi

	if ! emu_instance_exists ${inst2}; then
		atf_fail "Second instance failed to start"
	fi

	if ! emu_instance_exists ${inst3}; then
		atf_fail "Third instance failed to start"
	fi

	# Verify instances have unique IDs
	if [ "${inst1}" = "${inst2}" ] || [ "${inst1}" = "${inst3}" ] || \
	   [ "${inst2}" = "${inst3}" ]; then
		atf_fail "Instance IDs should be unique"
	fi
}
multi_instance_creation_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Test 2: Independent instance operation
atf_test_case independent_operation cleanup
independent_operation_head() {
	atf_set "descr" "Test that instances operate independently"
	atf_set "require.user" "root"
}
independent_operation_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start two instances
	local inst1 inst2
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)

	# Get status of each instance
	local status1 status2
	status1=$(emu status ${inst1} 2>/dev/null)
	status2=$(emu status ${inst2} 2>/dev/null)

	# Both should be running
	if ! echo "${status1}" | grep -q "running"; then
		atf_fail "First instance should be running"
	fi

	if ! echo "${status2}" | grep -q "running"; then
		atf_fail "Second instance should be running"
	fi

	# Verify status outputs are different (contain different instance info)
	if [ "${status1}" = "${status2}" ]; then
		atf_fail "Instance statuses should differ"
	fi
}
independent_operation_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 3: Stop one instance, others unaffected
atf_test_case stop_one_unaffected cleanup
stop_one_unaffected_head() {
	atf_set "descr" "Test stopping one instance doesn't affect others"
	atf_set "require.user" "root"
}
stop_one_unaffected_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start two instances
	local inst1 inst2
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)

	# Stop first instance
	emu stop ${inst1}

	# Wait for first instance to stop
	local count=0
	while emu_instance_exists ${inst1} && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done

	# Verify first instance is stopped
	if emu_instance_exists ${inst1}; then
		atf_fail "First instance should be stopped"
	fi

	# Verify second instance is still running
	if ! emu_instance_exists ${inst2}; then
		atf_fail "Second instance should still be running after stopping first"
	fi
}
stop_one_unaffected_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 4: List all instances
atf_test_case list_all_instances cleanup
list_all_instances_head() {
	atf_set "descr" "Test listing all instances"
	atf_set "require.user" "root"
}
list_all_instances_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start three instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# List all instances
	local instances
	instances=$(emu list 2>/dev/null)

	# Verify all instances are in the list
	if ! echo "${instances}" | grep -q "${inst1}"; then
		atf_fail "First instance not in list"
	fi

	if ! echo "${instances}" | grep -q "${inst2}"; then
		atf_fail "Second instance not in list"
	fi

	if ! echo "${instances}" | grep -q "${inst3}"; then
		atf_fail "Third instance not in list"
	fi
}
list_all_instances_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Test 5: Resource isolation
atf_test_case resource_isolation cleanup
resource_isolation_head() {
	atf_set "descr" "Test resource isolation between instances"
	atf_set "require.user" "root"
}
resource_isolation_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start two instances
	local inst1 inst2
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)

	# Get memory for each instance
	local mem1 mem2
	mem1=$(sysctl -n "kern.emulation.instance.${inst1}.memory" 2>/dev/null)
	mem2=$(sysctl -n "kern.emulation.instance.${inst2}.memory" 2>/dev/null)

	# Both should have memory configured
	if [ -z "${mem1}" ] || [ -z "${mem2}" ]; then
		atf_skip "Memory sysctls not available"
	fi

	# Memory should be reported correctly (may or may not be equal)
	# The key is that each instance has its own memory space
	if ! echo "${mem1}" | grep -qE '^[0-9]+$'; then
		atf_fail "First instance memory should be numeric"
	fi

	if ! echo "${mem2}" | grep -qE '^[0-9]+$'; then
		atf_fail "Second instance memory should be numeric"
	fi
}
resource_isolation_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 6: Concurrent operations
atf_test_case concurrent_operations cleanup
concurrent_operations_head() {
	atf_set "descr" "Test concurrent operations on multiple instances"
	atf_set "require.user" "root"
}
concurrent_operations_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start two instances
	local inst1 inst2
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)

	# Perform concurrent operations (in background)
	# Start status monitoring for both instances
	local status1 status2
	status1=$(emu status ${inst1} 2>/dev/null) &
	local pid1=$!
	status2=$(emu status ${inst2} 2>/dev/null) &
	local pid2=$!

	# Wait for both to complete
	wait ${pid1}
	local ret1=$?
	wait ${pid2}
	local ret2=$?

	# Both operations should succeed
	if [ ${ret1} -ne 0 ]; then
		atf_fail "First status command failed"
	fi

	if [ ${ret2} -ne 0 ]; then
		atf_fail "Second status command failed"
	fi

	# Both should have running status
	if ! echo "${status1}" | grep -q "running"; then
		atf_fail "First instance should be running"
	fi

	if ! echo "${status2}" | grep -q "running"; then
		atf_fail "Second instance should be running"
	fi
}
concurrent_operations_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 7: Instance naming
atf_test_case instance_naming cleanup
instance_naming_head() {
	atf_set "descr" "Test instance naming and uniqueness"
	atf_set "require.user" "root"
}
instance_naming_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Create named instance
	local inst1
	inst1=$(emu_start_test_instance)

	# Verify instance has a name
	local name
	name=$(sysctl -n "kern.emulation.instance.${inst1}.name" 2>/dev/null)
	if [ -z "${name}" ]; then
		atf_skip "Instance name sysctl not available"
	fi

	# Name should be non-empty
	if [ -z "${name}" ]; then
		atf_fail "Instance name should not be empty"
	fi

	# Verify name is unique among all instances
	local all_names
	all_names=$(emu list 2>/dev/null | grep -oE 'name=[^ ]+' | sort | uniq -d)
	if [ -n "${all_names}" ]; then
		atf_fail "Duplicate instance names found"
	fi
}
instance_naming_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
}

# Test 8: Bulk destroy
atf_test_case bulk_destroy cleanup
bulk_destroy_head() {
	atf_set "descr" "Test destroying multiple instances"
	atf_set "require.user" "root"
}
bulk_destroy_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start three instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# Verify all are running
	if ! emu_instance_exists ${inst1} || \
	   ! emu_instance_exists ${inst2} || \
	   ! emu_instance_exists ${inst3}; then
		atf_fail "All instances should be running"
	fi

	# Destroy all instances
	emu destroy ${inst1}
	emu destroy ${inst2}
	emu destroy ${inst3}

	# Wait for all to be destroyed
	sleep 1

	# Verify no instances are running
	local remaining
	remaining=$(emu list 2>/dev/null | wc -l)
	if [ "${remaining}" -gt 0 ]; then
		atf_fail "All instances should be destroyed"
	fi
}
bulk_destroy_cleanup() {
	:
}

# Test 9: Instance limits
atf_test_case instance_limits cleanup
instance_limits_head() {
	atf_set "descr" "Test instance creation limits"
	atf_set "require.user" "root"
}
instance_limits_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Check maximum instances limit
	local max_instances
	max_instances=$(sysctl -n kern.emulation.max_instances 2>/dev/null)
	if [ $? -ne 0 ]; then
		atf_skip "max_instances sysctl not available"
	fi

	# Verify max_instances is reasonable
	if [ "${max_instances}" -lt 1 ]; then
		atf_fail "max_instances should be at least 1"
	fi

	# Try to create instances up to the limit
	# Note: This might take a while if the limit is high
	local created=0
	local inst_id

	while [ ${created} -lt ${max_instances} ]; do
		inst_id=$(emu_start_test_instance)
		if ! emu_instance_exists ${inst_id}; then
			break
		fi
		created=$((created + 1))
	done

	# Verify we hit the limit
	if [ ${created} -ge ${max_instances} ]; then
		# Try to create one more - should fail
		local result
		result=$(emu_start_test_instance 2>&1)
		if emu_instance_exists $(echo "${result}" | grep -oE 'inst_[^ ]+' || echo ""); then
			atf_fail "Should not be able to exceed max_instances"
		fi
	fi

	# Clean up created instances
	for i in $(seq 1 ${created}); do
		emu_cleanup_test_instance
	done
}
instance_limits_cleanup() {
	:
}

# Test 10: Cross-architecture multi-instance
atf_test_case cross_arch_multi_instance cleanup
cross_arch_multi_instance_head() {
	atf_set "descr" "Test multiple instances of different architectures"
	atf_set "require.user" "root"
}
cross_arch_multi_instance_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Get host architecture
	local host_arch
	host_arch=$(uname -m)

	# Find different architectures
	local arch1 arch2
	case "${host_arch}" in
	amd64)
		arch1="amd64"
		arch2="arm64"
		;;
	arm64|aarch64)
		arch1="arm64"
		arch2="amd64"
		;;
	*)
		atf_skip "Need at least two architectures"
		;;
	esac

	# Start instance for first architecture
	local inst1
	inst1=$(emu_start_test_instance --arch ${arch1})

	# Start instance for second architecture
	local inst2
	inst2=$(emu_start_test_instance --arch ${arch2})

	# Verify both are running
	if ! emu_instance_exists ${inst1}; then
		atf_fail "First instance failed to start"
	fi

	if ! emu_instance_exists ${inst2}; then
		atf_fail "Second instance failed to start"
	fi

	# Verify architectures are correct
	local inst_arch1 inst_arch2
	inst_arch1=$(sysctl -n "kern.emulation.instance.${inst1}.arch" 2>/dev/null)
	inst_arch2=$(sysctl -n "kern.emulation.instance.${inst2}.arch" 2>/dev/null)

	if [ "${inst_arch1}" != "${arch1}" ]; then
		atf_fail "First instance architecture mismatch"
	fi

	if [ "${inst_arch2}" != "${arch2}" ]; then
		atf_fail "Second instance architecture mismatch"
	fi
}
cross_arch_multi_instance_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
}

# Test 11: Instance state persistence
atf_test_case state_persistence cleanup
state_persistence_head() {
	atf_set "descr" "Test instance state persistence"
	atf_set "require.user" "root"
}
state_persistence_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start an instance
	local inst1
	inst1=$(emu_start_test_instance)

	# Get instance state
	local state1
	state1=$(sysctl -n "kern.emulation.instance.${inst1}.state" 2>/dev/null)

	# State should be "running"
	if [ "${state1}" != "running" ]; then
		atf_fail "Initial state should be running"
	fi

	# Wait a bit and check state again
	sleep 1

	local state2
	state2=$(sysctl -n "kern.emulation.instance.${inst1}.state" 2>/dev/null)

	# State should still be "running"
	if [ "${state2}" != "running" ]; then
		atf_fail "State should persist as running"
	fi
}
state_persistence_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
}

# Test 12: Destroy while others run
atf_test_case destroy_while_others_run cleanup
destroy_while_others_run_head() {
	atf_set "descr" "Test destroying instance while others run"
	atf_set "require.user" "root"
}
destroy_while_others_run_body() {
	atf_skip "Emulator binary not yet available for testing"

	# Start three instances
	local inst1 inst2 inst3
	inst1=$(emu_start_test_instance)
	inst2=$(emu_start_test_instance)
	inst3=$(emu_start_test_instance)

	# Destroy middle instance
	emu destroy ${inst2}

	# Verify inst2 is gone
	if emu_instance_exists ${inst2}; then
		atf_fail "Second instance should be destroyed"
	fi

	# Verify inst1 and inst3 are still running
	if ! emu_instance_exists ${inst1}; then
		atf_fail "First instance should still be running"
	fi

	if ! emu_instance_exists ${inst3}; then
		atf_fail "Third instance should still be running"
	fi

	# Get list and verify only 2 instances remain
	local remaining
	remaining=$(emu list 2>/dev/null | grep -c "${inst1}\|${inst3}" || echo 0)
	if [ "${remaining}" -ne 2 ]; then
		atf_fail "Should have exactly 2 instances remaining"
	fi
}
destroy_while_others_run_cleanup() {
	emu_cleanup_test_instance ${inst1:-}
	emu_cleanup_test_instance ${inst2:-}
	emu_cleanup_test_instance ${inst3:-}
}

# Main test harness
atf_init_test_cases() {
	atf_add_test_case multi_instance_creation
	atf_add_test_case independent_operation
	atf_add_test_case stop_one_unaffected
	atf_add_test_case list_all_instances
	atf_add_test_case resource_isolation
	atf_add_test_case concurrent_operations
	atf_add_test_case instance_naming
	atf_add_test_case bulk_destroy
	atf_add_test_case instance_limits
	atf_add_test_case cross_arch_multi_instance
	atf_add_test_case state_persistence
	atf_add_test_case destroy_while_others_run
}
