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
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# OOM Killer Interaction Tests (S16.2)
#
# Test OOM killer protection mechanisms for the emulation framework.
# Verifies that emulator processes can adjust their OOM scores to avoid
# premature termination under memory pressure.

. $(atf_get_srcdir)/utils.subr

atf_test_case oom_score_adjust cleanup
oom_score_adjust_head() {
	atf_set "descr" "Test OOM score adjustment to minimum value"
	atf_set "require.user" "root"
}
oom_score_adjust_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Get initial OOM score
	local initial_score
	initial_score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	
	# Adjust OOM score to minimum (should be -1000)
	emu_adjust_oom_score $inst_id
	
	# Verify OOM score was adjusted
	local new_score
	new_score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	
	if [ "$new_score" != "-1000" ]; then
		atf_fail "OOM score not adjusted to minimum (expected -1000, got $new_score)"
	fi
	
	# Verify score is lower than initial (more protected)
	if [ "$new_score" -gt "$initial_score" ]; then
		atf_fail "OOM score increased (less protected) instead of decreased"
	fi
}
oom_score_adjust_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case oom_score_reset cleanup
oom_score_reset_head() {
	atf_set "descr" "Test OOM score reset to default value"
	atf_set "require.user" "root"
}
oom_score_reset_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Adjust OOM score to minimum
	emu_adjust_oom_score $inst_id
	
	# Verify it's at minimum
	local min_score
	min_score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$min_score" != "-1000" ]; then
		atf_fail "OOM score not at minimum after adjustment"
	fi
	
	# Reset OOM score to default
	emu_reset_oom_score $inst_id
	
	# Verify score was reset (should be 0 or initial value)
	local reset_score
	reset_score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	
	if [ "$reset_score" -lt "-500" ]; then
		atf_fail "OOM score not properly reset (still too low: $reset_score)"
	fi
}
oom_score_reset_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case oom_score_get cleanup
oom_score_get_head() {
	atf_set "descr" "Test getting current OOM score"
	atf_set "require.user" "root"
}
oom_score_get_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Get OOM score via emulator interface
	local emu_score
	emu_score=$(emu_get_oom_score $inst_id)
	
	# Get OOM score directly from proc
	local proc_score
	proc_score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	
	# Verify they match
	if [ "$emu_score" != "$proc_score" ]; then
		atf_fail "Emulator OOM score ($emu_score) doesn't match proc ($proc_score)"
	fi
}
oom_score_get_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case oom_score_persistence cleanup
oom_score_persistence_head() {
	atf_set "descr" "Test OOM score persistence across operations"
	atf_set "require.user" "root"
}
oom_score_persistence_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Adjust OOM score to minimum
	emu_adjust_oom_score $inst_id
	
	# Verify initial adjustment
	local score1
	score1=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$score1" != "-1000" ]; then
		atf_fail "Initial OOM adjustment failed"
	fi
	
	# Perform some emulator operations (simulated)
	emu_perform_operation $inst_id "memory_alloc"
	emu_perform_operation $inst_id "cpu_start"
	
	# Verify OOM score remains at minimum
	local score2
	score2=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$score2" != "-1000" ]; then
		atf_fail "OOM score changed after operations (was -1000, now $score2)"
	fi
}
oom_score_persistence_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case oom_score_multiple_instances cleanup
oom_score_multiple_instances_head() {
	atf_set "descr" "Test OOM score adjustment for multiple instances"
	atf_set "require.user" "root"
}
oom_score_multiple_instances_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start multiple emulator instances
	local inst_ids=""
	local i
	for i in 1 2 3; do
		local inst_id
		inst_id=$(emu_start_test_instance)
		inst_ids="$inst_ids $inst_id"
		
		# Adjust OOM score for each instance
		emu_adjust_oom_score $inst_id
		
		# Verify adjustment
		local score
		score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
		if [ "$score" != "-1000" ]; then
			atf_fail "Instance $i OOM score not adjusted (got $score)"
		fi
	done
	
	# Clean up all instances
	for inst_id in $inst_ids; do
		emu_cleanup_test_instance $inst_id
	done
}
oom_score_multiple_instances_cleanup() {
	# Cleanup handled in test body
	:
}

atf_test_case oom_score_error_handling cleanup
oom_score_error_handling_head() {
	atf_set "descr" "Test OOM score adjustment error handling"
	atf_set "require.user" "root"
}
oom_score_error_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Try to adjust OOM score for non-existent instance
	if emu_adjust_oom_score "nonexistent" 2>/dev/null; then
		atf_fail "OOM adjustment succeeded for non-existent instance"
	fi
	
	# Try to get OOM score for invalid PID
	if emu_get_oom_score "invalid" 2>/dev/null; then
		atf_fail "OOM score get succeeded for invalid instance"
	fi
}
oom_score_error_handling_cleanup() {
	:
}

atf_test_case oom_score_permissions cleanup
oom_score_permissions_head() {
	atf_set "descr" "Test OOM score adjustment permission requirements"
}
oom_score_permissions_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance as non-root
	local inst_id
	inst_id=$(emu_start_test_instance_nonroot)
	
	# Try to adjust OOM score as non-root (should fail or require privilege)
	# This test verifies that OOM adjustment requires appropriate privileges
	if emu_adjust_oom_score_nonroot $inst_id 2>/dev/null; then
		# If it succeeds, verify the score wasn't actually changed
		local score
		score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
		if [ "$score" = "-1000" ]; then
			atf_fail "Non-root user was able to adjust OOM score without privileges"
		fi
	fi
}
oom_score_permissions_cleanup() {
	emu_cleanup_test_instance_nonroot ${inst_id:-}
}

atf_test_case oom_score_boundary_values cleanup
oom_score_boundary_values_head() {
	atf_set "descr" "Test OOM score adjustment with boundary values"
	atf_set "require.user" "root"
}
oom_score_boundary_values_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Test minimum value (-1000)
	emu_set_oom_score $inst_id -1000
	local score
	score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$score" != "-1000" ]; then
		atf_fail "Failed to set minimum OOM score (got $score)"
	fi
	
	# Test maximum value (1000)
	emu_set_oom_score $inst_id 1000
	score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$score" != "1000" ]; then
		atf_fail "Failed to set maximum OOM score (got $score)"
	fi
	
	# Test zero (default)
	emu_set_oom_score $inst_id 0
	score=$(cat /proc/$(emu_get_pid $inst_id)/oom_score_adj)
	if [ "$score" != "0" ]; then
		atf_fail "Failed to set zero OOM score (got $score)"
	fi
}
oom_score_boundary_values_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_init_test_cases() {
	atf_add_test_case oom_score_adjust
	atf_add_test_case oom_score_reset
	atf_add_test_case oom_score_get
	atf_add_test_case oom_score_persistence
	atf_add_test_case oom_score_multiple_instances
	atf_add_test_case oom_score_error_handling
	atf_add_test_case oom_score_permissions
	atf_add_test_case oom_score_boundary_values
}
