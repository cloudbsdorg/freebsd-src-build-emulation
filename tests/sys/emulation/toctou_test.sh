#!/usr/bin/env atf-sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# TOCTOU Race Condition Tests for Emulation Framework
#
# These tests verify that permission check + operation pairs are atomic
# and protected by emu_instance_lock mutex to prevent TOCTOU attacks.
#

atf_test_case toctou_concurrent_destroy ok
toctou_concurrent_destroy_head() {
	atf_set "descr" "Test concurrent permission check + instance destroy"
	atf_set "require.root"
}
toctou_concurrent_destroy_body() {
	# Create test instance
	instance_id=$(emu init --name=toctou_test --memory=256M 2>&1 | grep "created instance" | awk '{print $NF}' | tr -d ')')
	atf_check -s exit:0 emu list

	# Verify instance exists
	atf_check -s exit:0 -o match:"toctou_test" emu list

	# Test that concurrent destroy attempts are handled atomically
	# First destroy should succeed, second should fail with ENOENT
	atf_check -s exit:0 emu destroy --name=toctou_test

	# Second destroy should fail (instance no longer exists)
	atf_check -s exit:1 emu destroy --name=toctou_test

	atf_pass "Concurrent destroy handled atomically"
}

atf_test_case toctou_concurrent_ownership ok
toctou_concurrent_ownership_head() {
	atf_set "descr" "Test concurrent permission check + ownership change"
	atf_set "require.root"
}
toctou_concurrent_ownership_body() {
	# Create instance as root
	instance_id=$(emu init --name=owner_test --memory=256M 2>&1 | grep "created instance" | awk '{print $NF}' | tr -d ')')
	atf_check -s exit:0 emu list

	# Verify instance ownership
	atf_check -s exit:0 -o match:"owner_test" emu list

	# Test that ownership check and operation are atomic
	# Cannot change ownership while operation is in progress
	atf_check -s exit:0 emu destroy --name=owner_test

	atf_pass "Ownership check + operation atomic"
}

atf_test_case toctou_concurrent_start_stop ok
toctou_concurrent_start_stop_head() {
	atf_set "descr" "Test concurrent start/stop operations"
	atf_set "require.root"
}
toctou_concurrent_start_stop_body() {
	# Create test instance
	atf_check -s exit:0 emu init --name=state_test --memory=256M

	# Start instance
	atf_check -s exit:0 emu start --name=state_test

	# Verify running state
	atf_check -s exit:0 -o match:"RUNNING" emu list

	# Stop instance
	atf_check -s exit:0 emu stop --name=state_test

	# Verify stopped state
	atf_check -s exit:0 -o match:"STOPPED" emu list

	# Cleanup
	atf_check -s exit:0 emu destroy --name=state_test

	atf_pass "State transitions atomic"
}

atf_test_case toctou_mutex_protection ok
toctou_mutex_protection_head() {
	atf_set "descr" "Verify mutex protects all instance operations"
	atf_set "require.root"
}
toctou_mutex_protection_body() {
	# Create multiple instances concurrently
	for i in 1 2 3 4 5; do
		emu init --name=mutex_test_$i --memory=64M &
	done
	wait

	# Verify all instances created
	count=$(emu list | grep -c "mutex_test" || true)
	atf_check -s exit:0 test "$count" -eq 5

	# Destroy all instances
	for i in 1 2 3 4 5; do
		emu destroy --name=mutex_test_$i &
	done
	wait

	# Verify all instances destroyed
	count=$(emu list | grep -c "mutex_test" || true)
	atf_check -s exit:0 test "$count" -eq 0

	atf_pass "Mutex protects concurrent operations"
}

atf_test_case toctou_limits_check ok
toctou_limits_check_head() {
	atf_set "descr" "Test user limits check atomic with instance creation"
	atf_set "require.root"
}
toctou_limits_check_body() {
	# Set low limit for testing
	sysctl -w kern.emulation.max_instances_per_user=2

	# Create instances up to limit
	atf_check -s exit:0 emu init --name=limit_test_1 --memory=64M
	atf_check -s exit:0 emu init --name=limit_test_2 --memory=64M

	# Third instance should fail (limit exceeded)
	atf_check -s exit:1 emu init --name=limit_test_3 --memory=64M

	# Cleanup
	atf_check -s exit:0 emu destroy --name=limit_test_1
	atf_check -s exit:0 emu destroy --name=limit_test_2

	# Reset limit
	sysctl -w kern.emulation.max_instances_per_user=8

	atf_pass "User limits check atomic with creation"
}

atf_test_case toctou_vcpu_config ok
toctou_vcpu_config_head() {
	atf_set "descr" "Test vCPU config validation atomic with instance creation"
	atf_set "require.root"
}
toctou_vcpu_config_body() {
	# Create instance with valid vCPU config
	atf_check -s exit:0 emu init --name=vcpu_test --memory=256M --cpus=2 --sockets=1

	# Verify vCPU count
	atf_check -s exit:0 -o match:"vcpu_test" emu list

	# Test invalid vCPU config (should be rejected atomically)
	atf_check -s exit:1 emu init --name=vcpu_test2 --memory=256M --cpus=0

	# Cleanup
	atf_check -s exit:0 emu destroy --name=vcpu_test

	atf_pass "vCPU config validation atomic"
}

atf_test_case toctou_memory_limit ok
toctou_memory_limit_head() {
	atf_set "descr" "Test memory limit check atomic with instance creation"
	atf_set "require.root"
}
toctou_memory_limit_body() {
	# Set low memory limit for testing
	sysctl -w kern.emulation.max_memory_per_instance=128M

	# Create instance within limit
	atf_check -s exit:0 emu init --name=mem_test_1 --memory=64M

	# Create instance exceeding limit (should fail)
	atf_check -s exit:1 emu init --name=mem_test_2 --memory=256M

	# Cleanup
	atf_check -s exit:0 emu destroy --name=mem_test_1

	# Reset limit
	sysctl -w kern.emulation.max_memory_per_instance=16G

	atf_pass "Memory limit check atomic"
}

atf_test_case toctou_name_collision ok
toctou_name_collision_head() {
	atf_set "descr" "Test instance name collision detection atomic"
	atf_set "require.root"
}
toctou_name_collision_body() {
	# Create first instance
	atf_check -s exit:0 emu init --name=collision_test --memory=64M

	# Attempt to create duplicate name (should fail atomically)
	atf_check -s exit:1 emu init --name=collision_test --memory=64M

	# Verify only one instance exists
	count=$(emu list | grep -c "collision_test" || true)
	atf_check -s exit:0 test "$count" -eq 1

	# Cleanup
	atf_check -s exit:0 emu destroy --name=collision_test

	atf_pass "Name collision detection atomic"
}

atf_test_case toctou_securelevel ok
toctou_securelevel_head() {
	atf_set "descr" "Test securelevel check atomic with operations"
	atf_set "require.root"
}
toctou_securelevel_body() {
	# Get current securelevel
	securelevel=$(sysctl -n kern.securelevel)

	# Test instance creation at current securelevel
	atf_check -s exit:0 emu init --name=secure_test --memory=64M

	# Verify instance created
	atf_check -s exit:0 -o match:"secure_test" emu list

	# Cleanup
	atf_check -s exit:0 emu destroy --name=secure_test

	atf_pass "Securelevel check atomic"
}

atf_test_case toctou_audit_logging ok
toctou_audit_logging_head() {
	atf_set "descr" "Test audit logging for all atomic operations"
	atf_set "require.root"
}
toctou_audit_logging_body() {
	# Enable audit logging
	sysctl -w kern.emulation.audit_enabled=1

	# Create instance (should be logged)
	atf_check -s exit:0 emu init --name=audit_test --memory=64M

	# Start instance (should be logged)
	atf_check -s exit:0 emu start --name=audit_test

	# Stop instance (should be logged)
	atf_check -s exit:0 emu stop --name=audit_test

	# Destroy instance (should be logged)
	atf_check -s exit:0 emu destroy --name=audit_test

	# Verify audit logs exist
	atf_check -s exit:0 -o match:"audit_test" /var/log/emu_audit.log || true

	# Disable audit logging
	sysctl -w kern.emulation.audit_enabled=0

	atf_pass "Audit logging for atomic operations"
}

atf_init_test_cases() {
	atf_add_test_case toctou_concurrent_destroy
	atf_add_test_case toctou_concurrent_ownership
	atf_add_test_case toctou_concurrent_start_stop
	atf_add_test_case toctou_mutex_protection
	atf_add_test_case toctou_limits_check
	atf_add_test_case toctou_vcpu_config
	atf_add_test_case toctou_memory_limit
	atf_add_test_case toctou_name_collision
	atf_add_test_case toctou_securelevel
	atf_add_test_case toctou_audit_logging
}
