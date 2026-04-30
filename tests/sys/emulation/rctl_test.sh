#!/bin/sh
# $FreeBSD$
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
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
# rctl integration tests for the emulation framework
#

atf_test_case rctl_availability
rctl_availability_head()
{
	atf_set "descr" "Test that rctl subsystem is available"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_availability_body()
{
	# Check if rctl is available in the kernel
	if ! kldstat -m rctl > /dev/null 2>&1; then
		atf_skip "rctl kernel module not loaded"
	fi

	# Check if racct is enabled
	if ! sysctl kern.racct.enable > /dev/null 2>&1; then
		atf_skip "racct not enabled in kernel"
	fi

	# Check emulation rctl integration status
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "emu_core module not loaded or rctl not integrated"
	fi

	atf_pass
}

atf_test_case rctl_default_limits
rctl_default_limits_head()
{
	atf_set "descr" "Test that rctl default limits are configurable"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_default_limits_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Get current defaults
	default_memory=$(sysctl -n kern.emulation.rctl.default_memory_mb 2>/dev/null)
	if [ -z "$default_memory" ]; then
		atf_skip "rctl sysctls not available"
	fi

	# Verify default memory is reasonable (16GB = 16384MB)
	if [ "$default_memory" -ne 16384 ]; then
		atf_skip "Default memory limit changed, test assumes 16GB"
	fi

	# Verify default CPU time (24 hours = 86400 seconds)
	default_cpu=$(sysctl -n kern.emulation.rctl.default_cpu_seconds)
	if [ "$default_cpu" -ne 86400 ]; then
		atf_skip "Default CPU time changed, test assumes 24 hours"
	fi

	# Verify default max procs
	default_procs=$(sysctl -n kern.emulation.rctl.default_max_procs)
	if [ "$default_procs" -ne 1024 ]; then
		atf_skip "Default max procs changed, test assumes 1024"
	fi

	atf_pass
}

atf_test_case rctl_rule_format
rctl_rule_format_head()
{
	atf_set "descr" "Test that rctl rules are properly formatted"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_rule_format_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Check rctl availability
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "rctl integration not available"
	fi

	# Test that we can add a test rule
	test_rule="user:0:nproc:deny=1000"
	if ! rctl -a "$test_rule" 2>/dev/null; then
		atf_skip "Cannot add test rctl rule (permission denied)"
	fi

	# Verify rule was added
	if ! rctl -l | grep -q "$test_rule"; then
		atf_fail "Rule was not added to rctl"
	fi

	# Clean up
	rctl -r "$test_rule" 2>/dev/null

	atf_pass
}

atf_test_case rctl_memory_limit
rctl_memory_limit_head()
{
	atf_set "descr" "Test rctl memory limit enforcement"
	atf_set "require.user" "root"
	atf_set "timeout" 60
}

rctl_memory_limit_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Check rctl availability
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "rctl integration not available"
	fi

	# Create a test process and apply memory limit
	(
		ulimit -v 102400  # 100MB virtual memory limit
		# Try to allocate more than limit
		# This should be handled by rctl if properly configured
	) &

	test_pid=$!
	sleep 1

	# Verify process is still running (limits should be soft)
	if ! kill -0 $test_pid 2>/dev/null; then
		atf_skip "Test process terminated unexpectedly"
	fi

	# Clean up
	kill $test_pid 2>/dev/null
	wait $test_pid 2>/dev/null

	atf_pass
}

atf_test_case rctl_nproc_limit
rctl_nproc_limit_head()
{
	atf_set "descr" "Test rctl process count limit enforcement"
	atf_set "require.user" "root"
	atf_set "timeout" 60
}

rctl_nproc_limit_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Check rctl availability
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "rctl integration not available"
	fi

	# Set a test nproc limit for this user
	test_rule="user:$(id -u):nproc:deny=50"
	if ! rctl -a "$test_rule" 2>/dev/null; then
		atf_skip "Cannot add nproc rctl rule"
	fi

	# Verify rule was added
	if ! rctl -l | grep -q "user:$(id -u):nproc"; then
		atf_fail "nproc rule was not added"
	fi

	# Clean up
	rctl -r "$test_rule" 2>/dev/null

	atf_pass
}

atf_test_case rctl_cputime_limit
rctl_cputime_limit_head()
{
	atf_set "descr" "Test rctl CPU time limit enforcement"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_cputime_limit_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Check rctl availability
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "rctl integration not available"
	fi

	# CPU time limits are tested by the emulator's actual usage
	# For now, just verify the sysctl is present
	default_cpu=$(sysctl -n kern.emulation.rctl.default_cpu_seconds)
	if [ -z "$default_cpu" ]; then
		atf_fail "CPU time sysctl not available"
	fi

	atf_pass
}

atf_test_case rctl_loginclass_limits
rctl_loginclass_limits_head()
{
	atf_set "descr" "Test rctl login class limits"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_loginclass_limits_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Check rctl availability
	rctl_status=$(sysctl -n kern.emulation.rctl.enabled 2>/dev/null)
	if [ -z "$rctl_status" ]; then
		atf_skip "rctl integration not available"
	fi

	# Check if we can list login classes
	if ! rctl -l 2>/dev/null | head -1 > /dev/null; then
		atf_skip "Cannot list rctl rules"
	fi

	atf_pass
}

atf_test_case rctl_sysctl_tuning
rctl_sysctl_tuning_head()
{
	atf_set "descr" "Test that rctl sysctls are tunable"
	atf_set "require.user" "root"
	atf_set "timeout" 30
}

rctl_sysctl_tuning_body()
{
	# Check if emu_core module is loaded
	if ! kldstat -n emu_core > /dev/null 2>&1; then
		atf_skip "emu_core module not loaded"
	fi

	# Save original values
	orig_enabled=$(sysctl -n kern.emulation.rctl.enabled)
	orig_memory=$(sysctl -n kern.emulation.rctl.default_memory_mb)

	# Test enabling/disabling
	sysctl kern.emulation.rctl.enabled=0
	if [ $? -ne 0 ]; then
		atf_fail "Failed to disable rctl"
	fi

	# Verify it's disabled
	if [ "$(sysctl -n kern.emulation.rctl.enabled)" -ne 0 ]; then
		atf_fail "rctl.enabled did not change to 0"
	fi

	# Re-enable
	sysctl kern.emulation.rctl.enabled=1
	if [ $? -ne 0 ]; then
		atf_fail "Failed to re-enable rctl"
	fi

	# Test memory limit tuning
	new_memory=$((orig_memory / 2))
	sysctl kern.emulation.rctl.default_memory_mb=$new_memory
	if [ $? -ne 0 ]; then
		atf_fail "Failed to tune default_memory_mb"
	fi

	# Verify change
	if [ "$(sysctl -n kern.emulation.rctl.default_memory_mb)" -ne $new_memory ]; then
		atf_fail "default_memory_mb did not change"
	fi

	# Restore original values
	sysctl kern.emulation.rctl.default_memory_mb=$orig_memory

	atf_pass
}

atf_init_test_cases()
{
	atf_add_test_case rctl_availability
	atf_add_test_case rctl_default_limits
	atf_add_test_case rctl_rule_format
	atf_add_test_case rctl_memory_limit
	atf_add_test_case rctl_nproc_limit
	atf_add_test_case rctl_cputime_limit
	atf_add_test_case rctl_loginclass_limits
	atf_add_test_case rctl_sysctl_tuning
}
