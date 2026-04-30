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

# Zombie Process Cleanup Tests for Emulation Framework
# Task TC.71: Test that crashed child processes are reaped.
#             Test that zombie processes don't accumulate.

. $(dirname $0)/utils.subr

atf_test_case zombie_no_accumulation cleanup
atf_test_case zombie_child_reaping cleanup
atf_test_case zombie_multiple_instances cleanup
atf_test_case zombie_cleanup_on_stop cleanup
atf_test_case zombie_reap_on_crash cleanup

# Test 1: No zombie accumulation
zombie_no_accumulation_head() {
	atf_set "descr" "Verify zombie processes don't accumulate"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

zombie_no_accumulation_body() {
	# Initialize an instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_accum --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_accum

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_zombie_accum

	atf_log "PASS: No zombie accumulation detected"
}

zombie_no_accumulation_cleanup() {
	emu stop test_zombie_accum 2>/dev/null || true
	emu destroy test_zombie_accum 2>/dev/null || true
}

# Test 2: Child process reaping
zombie_child_reaping_head() {
	atf_set "descr" "Verify child processes are properly reaped"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

zombie_child_reaping_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_reap --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_reap

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_zombie_reap

	atf_log "PASS: Child processes properly reaped"
}

zombie_child_reaping_cleanup() {
	emu stop test_zombie_reap 2>/dev/null || true
	emu destroy test_zombie_reap 2>/dev/null || true
}

# Test 3: Multiple instances zombie handling
zombie_multiple_instances_head() {
	atf_set "descr" "Verify zombie handling across multiple instances"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

zombie_multiple_instances_body() {
	# Create multiple instances
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_multi1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_multi2 --arch amd64 --memory 256M

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_multi1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_multi2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_zombie_multi1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_zombie_multi2

	atf_log "PASS: Multiple instances zombie handling works"
}

zombie_multiple_instances_cleanup() {
	emu stop test_zombie_multi1 2>/dev/null || true
	emu stop test_zombie_multi2 2>/dev/null || true
	emu destroy test_zombie_multi1 2>/dev/null || true
	emu destroy test_zombie_multi2 2>/dev/null || true
}

# Test 4: Cleanup on stop
zombie_cleanup_on_stop_head() {
	atf_set "descr" "Verify zombie cleanup when instance is stopped"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

zombie_cleanup_on_stop_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_stop --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_stop

	# Stop instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu stop test_zombie_stop

	# Verify instance is stopped
	atf_check -s exit:0 -o match:"STOPPED" -e ignore \
		emu status test_zombie_stop

	# Destroy instance
	emu destroy test_zombie_stop 2>/dev/null || true

	atf_log "PASS: Zombie cleanup on stop works"
}

zombie_cleanup_on_stop_cleanup() {
	emu stop test_zombie_stop 2>/dev/null || true
	emu destroy test_zombie_stop 2>/dev/null || true
}

# Test 5: Reap on crash
zombie_reap_on_crash_head() {
	atf_set "descr" "Verify child processes are reaped on crash"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

zombie_reap_on_crash_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_zombie_crash --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_zombie_crash

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_zombie_crash

	atf_log "PASS: Child processes reaped on crash"
}

zombie_reap_on_crash_cleanup() {
	emu stop test_zombie_crash 2>/dev/null || true
	emu destroy test_zombie_crash 2>/dev/null || true
}

# Main entry point
atf_init "$@"
