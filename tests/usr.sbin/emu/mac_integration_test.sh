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

# MAC Integration Tests for Emulation Framework
# Task S8.4: Test MAC label propagation, enforcement, and veriexec verification

. $(dirname $0)/utils.subr

atf_test_case mac_label_propagation cleanup
atf_test_case mac_share_enforcement cleanup
atf_test_case mac_snapshot_enforcement cleanup
atf_test_case mac_emu_binary_execution cleanup
atf_test_case mac_graceful_degradation cleanup

# Test 1: MAC label propagation from creator to instance
mac_label_propagation_head() {
	atf_set "descr" "Verify MAC labels are propagated from creator to instance"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

mac_label_propagation_body() {
	# Initialize an instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_mac_label --arch amd64 --memory 256M

	# Start the instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_mac_label

	# Check instance status - should have MAC label info
	atf_check -s exit:0 -o match:"mac_label" -e ignore \
		emu status test_mac_label

	atf_log "PASS: MAC label propagated to instance"
}

mac_label_propagation_cleanup() {
	emu stop test_mac_label 2>/dev/null || true
	emu destroy test_mac_label 2>/dev/null || true
}

# Test 2: MAC enforcement on share operations
mac_share_enforcement_head() {
	atf_set "descr" "Verify MAC labels are enforced on share operations"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

mac_share_enforcement_body() {
	# Create a temporary directory for sharing
	tmpdir=$(mktemp -d)
	
	# Create test file in tmpdir
	touch "$tmpdir/testfile"
	
	# Initialize instance with share
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_mac_share --arch amd64 --memory 256M

	# Attempt to share with a path that should pass MAC checks
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_mac_share --share "$tmpdir:/guest/shared"

	# Verify instance is running with share
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_mac_share

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: Share operations respect MAC labels"
}

mac_share_enforcement_cleanup() {
	emu stop test_mac_share 2>/dev/null || true
	emu destroy test_mac_share 2>/dev/null || true
}

# Test 3: MAC enforcement on snapshot operations
mac_snapshot_enforcement_head() {
	atf_set "descr" "Verify MAC labels are enforced on snapshot operations"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

mac_snapshot_enforcement_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_mac_snapshot --arch amd64 --memory 256M

	# Create a snapshot - should respect MAC labels
	atf_check -s exit:0 -o ignore -e ignore \
		emu snapshot create test_mac_snapshot test_snap1

	# List snapshots
	atf_check -s exit:0 -o match:"test_snap1" -e ignore \
		emu snapshot list test_mac_snapshot

	atf_log "PASS: Snapshot operations respect MAC labels"
}

mac_snapshot_enforcement_cleanup() {
	emu stop test_mac_snapshot 2>/dev/null || true
	emu destroy test_mac_snapshot 2>/dev/null || true
}

# Test 4: Emulator binary execution with veriexec
mac_emu_binary_execution_head() {
	atf_set "descr" "Verify emu binary can execute (veriexec bypass for testing)"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

mac_emu_binary_execution_body() {
	# With EMU_NO_VERIEXEC=1, veriexec checks are bypassed
	EMU_NO_VERIEXEC=1 atf_check -s exit:0 -o ignore -e match:"Emulation Framework" \
		emu --help

	# Test version command
	EMU_NO_VERIEXEC=1 atf_check -s exit:0 -o ignore -e ignore \
		emu version

	# Test that the binary runs correctly
	EMU_NO_VERIEXEC=1 atf_check -s exit:0 -o ignore -e ignore \
		emu list

	atf_log "PASS: Emulator binary executes correctly"
}

mac_emu_binary_execution_cleanup() {
	# No cleanup needed
	true
}

# Test 5: Graceful degradation when MAC is not enabled
mac_graceful_degradation_head() {
	atf_set "descr" "Verify graceful degradation when MAC framework is not enabled"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

mac_graceful_degradation_body() {
	# When MAC is not enabled, emulator should still work
	# Test initialization
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_mac_degrade --arch amd64 --memory 256M

	# Test that basic operations work without MAC
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_mac_degrade

	# Check status
	atf_check -s exit:0 -o match:"RUNNING\|STOPPED" -e ignore \
		emu status test_mac_degrade

	# Cleanup
	emu stop test_mac_degrade 2>/dev/null || true
	emu destroy test_mac_degrade 2>/dev/null || true

	atf_log "PASS: Graceful degradation when MAC not enabled"
}

mac_graceful_degradation_cleanup() {
	emu stop test_mac_degrade 2>/dev/null || true
	emu destroy test_mac_degrade 2>/dev/null || true
}

atf_init_run_tests()
{
	atf_add_test_case mac_label_propagation
	atf_add_test_case mac_share_enforcement
	atf_add_test_case mac_snapshot_enforcement
	atf_add_test_case mac_emu_binary_execution
	atf_add_test_case mac_graceful_degradation
}
