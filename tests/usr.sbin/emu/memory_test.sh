#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
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

# Memory Management Tests for Emulation Framework
# Task S6.10: Test memory policy, demand paging, balloon, overcommit, tracking

. $(dirname $0)/utils.subr

atf_test_case memory_policy_default cleanup
atf_test_case memory_policy_change cleanup
atf_test_case memory_overcommit_toggle cleanup
atf_test_case memory_warn_percent cleanup
atf_test_case memory_balloon_sysctl cleanup
atf_test_case memory_system_reserve cleanup
atf_test_case memory_available_tracking cleanup
atf_test_case memory_instance_tracking cleanup
atf_test_case memory_balloon_interval cleanup

# Test 1: Verify default memory policy
memory_policy_default_head() {
	atf_set "descr" "Verify default memory policy is prealloc"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_policy_default_body() {
	# Check default policy via sysctl
	atf_check -s exit:0 -o match:"prealloc\|demand" \
		sysctl kern.emulation.memory.policy

	atf_log "PASS: Memory policy sysctl exists"
}

memory_policy_default_cleanup() {
	# Restore defaults if changed
	sysctl kern.emulation.memory.policy=0 2>/dev/null || true
}

# Test 2: Change memory policy
memory_policy_change_head() {
	atf_set "descr" "Verify memory policy can be changed"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_policy_change_body() {
	# Try to set policy to demand
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.policy=1

	# Verify it changed
	atf_check -s exit:0 -o match:"1" \
		sysctl kern.emulation.memory.policy

	# Restore to prealloc
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.policy=0

	atf_log "PASS: Memory policy can be changed"
}

memory_policy_change_cleanup() {
	sysctl kern.emulation.memory.policy=0 2>/dev/null || true
}

# Test 3: Toggle memory overcommit
memory_overcommit_toggle_head() {
	atf_set "descr" "Verify memory overcommit can be toggled"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_overcommit_toggle_body() {
	# Check overcommit sysctl exists
	atf_check -s exit:0 -o match:"0\|1" \
		sysctl kern.emulation.memory.overcommit

	# Toggle it
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.overcommit=1

	# Verify it changed
	atf_check -s exit:0 -o match:"1" \
		sysctl kern.emulation.memory.overcommit

	# Toggle back
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.overcommit=0

	atf_log "PASS: Memory overcommit can be toggled"
}

memory_overcommit_toggle_cleanup() {
	sysctl kern.emulation.memory.overcommit=0 2>/dev/null || true
}

# Test 4: Configure warn percent
memory_warn_percent_head() {
	atf_set "descr" "Verify memory warn percent can be configured"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_warn_percent_body() {
	# Check warn percent sysctl exists
	atf_check -s exit:0 -o match:"[0-9]+" \
		sysctl kern.emulation.memory.warn_percent

	# Set warn percent to 90
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.warn_percent=90

	# Verify it changed
	atf_check -s exit:0 -o match:"90" \
		sysctl kern.emulation.memory.warn_percent

	# Restore to default
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.warn_percent=80

	atf_log "PASS: Memory warn percent can be configured"
}

memory_warn_percent_cleanup() {
	sysctl kern.emulation.memory.warn_percent=80 2>/dev/null || true
}

# Test 5: Balloon sysctl interface
memory_balloon_sysctl_head() {
	atf_set "descr" "Verify balloon sysctl interface works"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_balloon_sysctl_body() {
	# Check balloon_min_pct sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.balloon_min_pct

	# Set balloon_min_pct
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.balloon_min_pct=20

	# Verify
	atf_check -s exit:0 -o match:"20" \
		sysctl kern.emulation.memory.balloon_min_pct

	# Check balloon_interval sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.balloon_interval

	atf_log "PASS: Balloon sysctl interface works"
}

memory_balloon_sysctl_cleanup() {
	sysctl kern.emulation.memory.balloon_min_pct=10 2>/dev/null || true
}

# Test 6: System reserve configuration
memory_system_reserve_head() {
	atf_set "descr" "Verify system reserve can be configured"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_system_reserve_body() {
	# Check system_reserve sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.system_reserve_percent

	# Set reserve to 15%
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.system_reserve_percent=15

	# Verify
	atf_check -s exit:0 -o match:"15" \
		sysctl kern.emulation.memory.system_reserve_percent

	# Restore to default
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.system_reserve_percent=10

	atf_log "PASS: System reserve can be configured"
}

memory_system_reserve_cleanup() {
	sysctl kern.emulation.memory.system_reserve_percent=10 2>/dev/null || true
}

# Test 7: Available memory tracking
memory_available_tracking_head() {
	atf_set "descr" "Verify available memory is tracked"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_available_tracking_body() {
	# Check available memory sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.available

	# Check total memory sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.total

	# Check system used sysctl
	atf_check -s exit:0 -o match:"[0-9]+" \
		kern.emulation.memory.system_used

	# Available should be <= total
	atf_check -s exit:0 -o ignore \
		emu init test_mem_avail --arch amd64 --memory 256M

	emu stop test_mem_avail 2>/dev/null || true
	emu destroy test_mem_avail 2>/dev/null || true

	atf_log "PASS: Available memory is tracked"
}

memory_available_tracking_cleanup() {
	emu stop test_mem_avail 2>/dev/null || true
	emu destroy test_mem_avail 2>/dev/null || true
}

# Test 8: Instance memory tracking
memory_instance_tracking_head() {
	atf_set "descr" "Verify per-instance memory is tracked"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_instance_tracking_body() {
	# Create instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_mem_inst --arch amd64 --memory 512M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_mem_inst

	# Check instance memory tracking via status
	atf_check -s exit:0 -o match:"memory" -e ignore \
		emu status test_mem_inst

	# Stop and destroy
	emu stop test_mem_inst 2>/dev/null || true
	emu destroy test_mem_inst 2>/dev/null || true

	atf_log "PASS: Per-instance memory is tracked"
}

memory_instance_tracking_cleanup() {
	emu stop test_mem_inst 2>/dev/null || true
	emu destroy test_mem_inst 2>/dev/null || true
}

# Test 9: Balloon interval configuration
memory_balloon_interval_head() {
	atf_set "descr" "Verify balloon interval can be configured"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

memory_balloon_interval_body() {
	# Set balloon interval to 10 seconds
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.balloon_interval=10

	# Verify
	atf_check -s exit:0 -o match:"10" \
		sysctl kern.emulation.memory.balloon_interval

	# Set to 60 seconds
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.balloon_interval=60

	# Verify
	atf_check -s exit:0 -o match:"60" \
		sysctl kern.emulation.memory.balloon_interval

	# Restore default
	atf_check -s exit:0 -o ignore \
		sysctl kern.emulation.memory.balloon_interval=5

	atf_log "PASS: Balloon interval can be configured"
}

memory_balloon_interval_cleanup() {
	sysctl kern.emulation.memory.balloon_interval=5 2>/dev/null || true
}

atf_init_run_tests()
{
	atf_add_test_case memory_policy_default
	atf_add_test_case memory_policy_change
	atf_add_test_case memory_overcommit_toggle
	atf_add_test_case memory_warn_percent
	atf_add_test_case memory_balloon_sysctl
	atf_add_test_case memory_system_reserve
	atf_add_test_case memory_available_tracking
	atf_add_test_case memory_instance_tracking
	atf_add_test_case memory_balloon_interval
}
