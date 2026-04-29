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

# Signal Handling Security Tests (S15.3)
#
# Test signal handling infrastructure for the emulation framework.
# Verifies that signals are handled safely and appropriately.

. $(atf_get_srcdir)/utils.subr

atf_test_case sigterm_handling cleanup
sigterm_handling_head() {
	atf_set "descr" "Test SIGTERM signal handling (graceful shutdown)"
	atf_set "require.user" "root"
}
sigterm_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGTERM
	kill -TERM $(emu_get_pid $inst_id)
	
	# Wait for graceful shutdown (should complete within 5 seconds)
	local count=0
	while emu_instance_exists $inst_id && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done
	
	# Verify instance was destroyed
	if emu_instance_exists $inst_id; then
		atf_fail "Instance did not shut down after SIGTERM"
	fi
	
	# Verify clean shutdown in logs
	emu_check_log $inst_id "graceful shutdown"
}
sigterm_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sigint_handling cleanup
sigint_handling_head() {
	atf_set "descr" "Test SIGINT signal handling (graceful shutdown)"
	atf_set "require.user" "root"
}
sigint_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGINT
	kill -INT $(emu_get_pid $inst_id)
	
	# Wait for graceful shutdown
	local count=0
	while emu_instance_exists $inst_id && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done
	
	# Verify instance was destroyed
	if emu_instance_exists $inst_id; then
		atf_fail "Instance did not shut down after SIGINT"
	fi
	
	# Verify clean shutdown in logs
	emu_check_log $inst_id "graceful shutdown"
}
sigint_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sighup_handling cleanup
sighup_handling_head() {
	atf_set "descr" "Test SIGHUP signal handling (graceful shutdown)"
	atf_set "require.user" "root"
}
sighup_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGHUP
	kill -HUP $(emu_get_pid $inst_id)
	
	# Wait for graceful shutdown
	local count=0
	while emu_instance_exists $inst_id && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done
	
	# Verify instance was destroyed
	if emu_instance_exists $inst_id; then
		atf_fail "Instance did not shut down after SIGHUP"
	fi
	
	# Verify clean shutdown in logs
	emu_check_log $inst_id "graceful shutdown"
}
sighup_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sigpipe_handling cleanup
sigpipe_handling_head() {
	atf_set "descr" "Test SIGPIPE signal handling (broken pipe)"
	atf_set "require.user" "root"
}
sigpipe_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Close console pipe to trigger SIGPIPE
	emu_close_console $inst_id
	
	# Wait briefly for signal to be processed
	sleep 0.5
	
	# Verify instance continues running (SIGPIPE should log and continue)
	if ! emu_instance_exists $inst_id; then
		atf_fail "Instance should continue running after SIGPIPE"
	fi
	
	# Verify SIGPIPE was logged
	emu_check_log $inst_id "SIGPIPE"
}
sigpipe_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sigsegv_handling cleanup
sigsegv_handling_head() {
	atf_set "descr" "Test SIGSEGV signal handling (critical error shutdown)"
	atf_set "require.user" "root"
}
sigsegv_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGSEGV (simulating segmentation fault)
	kill -SEGV $(emu_get_pid $inst_id)
	
	# Wait for shutdown (should be immediate for critical error)
	local count=0
	while emu_instance_exists $inst_id && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done
	
	# Verify instance was destroyed
	if emu_instance_exists $inst_id; then
		atf_fail "Instance did not shut down after SIGSEGV"
	fi
	
	# Verify critical error logged
	emu_check_log $inst_id "SIGSEGV"
	emu_check_log $inst_id "segmentation fault"
}
sigsegv_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sigusr1_handling cleanup
sigusr1_handling_head() {
	atf_set "descr" "Test SIGUSR1 signal handling (user-defined)"
	atf_set "require.user" "root"
}
sigusr1_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGUSR1
	kill -USR1 $(emu_get_pid $inst_id)
	
	# Wait for signal to be processed
	sleep 0.5
	
	# Verify instance continues running (user signals should not terminate)
	if ! emu_instance_exists $inst_id; then
		atf_fail "Instance should continue running after SIGUSR1"
	fi
	
	# Verify user signal was logged
	emu_check_log $inst_id "SIGUSR1"
	emu_check_log $inst_id "User signal"
}
sigusr1_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case sigusr2_handling cleanup
sigusr2_handling_head() {
	atf_set "descr" "Test SIGUSR2 signal handling (user-defined)"
	atf_set "require.user" "root"
}
sigusr2_handling_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGUSR2
	kill -USR2 $(emu_get_pid $inst_id)
	
	# Wait for signal to be processed
	sleep 0.5
	
	# Verify instance continues running (user signals should not terminate)
	if ! emu_instance_exists $inst_id; then
		atf_fail "Instance should continue running after SIGUSR2"
	fi
	
	# Verify user signal was logged
	emu_check_log $inst_id "SIGUSR2"
	emu_check_log $inst_id "User signal"
}
sigusr2_handling_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case signal_flag_clearing cleanup
signal_flag_clearing_head() {
	atf_set "descr" "Test that signal flags are properly cleared after handling"
	atf_set "require.user" "root"
}
signal_flag_clearing_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send SIGUSR1
	kill -USR1 $(emu_get_pid $inst_id)
	
	# Wait for handling
	sleep 0.5
	
	# Send SIGUSR2
	kill -USR2 $(emu_get_pid $inst_id)
	
	# Wait for handling
	sleep 0.5
	
	# Verify only the latest signals are logged (flags should be cleared)
	# This ensures flags don't accumulate
	local usr1_count=$(emu_count_log $inst_id "SIGUSR1")
	local usr2_count=$(emu_count_log $inst_id "SIGUSR2")
	
	if [ "$usr1_count" -ne 1 ]; then
		atf_fail "SIGUSR1 should be logged exactly once (flags not cleared properly)"
	fi
	
	if [ "$usr2_count" -ne 1 ]; then
		atf_fail "SIGUSR2 should be logged exactly once (flags not cleared properly)"
	fi
}
signal_flag_clearing_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case multiple_signals cleanup
multiple_signals_head() {
	atf_set "descr" "Test handling of multiple simultaneous signals"
	atf_set "require.user" "root"
}
multiple_signals_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance
	local inst_id
	inst_id=$(emu_start_test_instance)
	
	# Send multiple signals in quick succession
	kill -USR1 $(emu_get_pid $inst_id)
	kill -USR2 $(emu_get_pid $inst_id)
	
	# Wait for handling
	sleep 0.5
	
	# Verify both signals were handled
	emu_check_log $inst_id "SIGUSR1"
	emu_check_log $inst_id "SIGUSR2"
	
	# Verify instance continues running
	if ! emu_instance_exists $inst_id; then
		atf_fail "Instance should continue running after user signals"
	fi
}
multiple_signals_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case signal_during_operation cleanup
signal_during_operation_head() {
	atf_set "descr" "Test signal handling during active emulation"
	atf_set "require.user" "root"
}
signal_during_operation_body() {
	atf_skip "Emulator binary not yet available for testing"
	
	# Start emulator instance with active workload
	local inst_id
	inst_id=$(emu_start_test_instance_with_workload)
	
	# Send SIGTERM during active execution
	kill -TERM $(emu_get_pid $inst_id)
	
	# Wait for graceful shutdown
	local count=0
	while emu_instance_exists $inst_id && [ $count -lt 50 ]; do
		sleep 0.1
		count=$((count + 1))
	done
	
	# Verify clean shutdown even during active workload
	if emu_instance_exists $inst_id; then
		atf_fail "Instance did not shut down cleanly during workload"
	fi
	
	# Verify workload was terminated gracefully
	emu_check_log $inst_id "graceful shutdown"
	emu_check_log $inst_id "workload terminated"
}
signal_during_operation_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_init_test_cases() {
	atf_add_test_case sigterm_handling
	atf_add_test_case sigint_handling
	atf_add_test_case sighup_handling
	atf_add_test_case sigpipe_handling
	atf_add_test_case sigsegv_handling
	atf_add_test_case sigusr1_handling
	atf_add_test_case sigusr2_handling
	atf_add_test_case signal_flag_clearing
	atf_add_test_case multiple_signals
	atf_add_test_case signal_during_operation
}
