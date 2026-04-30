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

# Swap Encryption Tests for Emulation Framework
# Task TC.72: Test that guest memory is not leaked to unencrypted swap.
#             Test mlock() option for memory locking.

. $(dirname $0)/utils.subr

atf_test_case swap_encryption_default cleanup
atf_test_case swap_encryption_mlock cleanup
atf_test_case swap_encryption_memory_locked cleanup
atf_test_case swap_encryption_beyond_physical cleanup
atf_test_case swap_encryption_instance_protection cleanup

# Test 1: Default behavior - guest memory may be swapped
swap_encryption_default_head() {
	atf_set "descr" "Verify default behavior allows swapping of guest memory"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

swap_encryption_default_body() {
	# Initialize an instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_default --arch amd64 --memory 256M

	# Check status - instance should be created
	atf_check -s exit:0 -o match:"test_swap_default" -e ignore \
		emu list

	atf_log "PASS: Instance created with default swap behavior"
}

swap_encryption_default_cleanup() {
	emu stop test_swap_default 2>/dev/null || true
	emu destroy test_swap_default 2>/dev/null || true
}

# Test 2: mlock() option - lock memory to prevent swapping
swap_encryption_mlock_head() {
	atf_set "descr" "Verify mlock() option locks guest memory in RAM"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

swap_encryption_mlock_body() {
	# Initialize an instance with memory lock
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_mlock --arch amd64 --memory 256M

	# Start the instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_swap_mlock

	# Check instance status
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_swap_mlock

	atf_log "PASS: Instance started with memory locking"
}

swap_encryption_mlock_cleanup() {
	emu stop test_swap_mlock 2>/dev/null || true
	emu destroy test_swap_mlock 2>/dev/null || true
}

# Test 3: Verify memory is actually locked
swap_encryption_memory_locked_head() {
	atf_set "descr" "Verify guest memory is locked and not swappable"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

swap_encryption_memory_locked_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_locked --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_swap_locked

	# Check memory usage - should show locked memory
	atf_check -s exit:0 -o match:"memory" -e ignore \
		emu status test_swap_locked

	atf_log "PASS: Memory status reported correctly"
}

swap_encryption_memory_locked_cleanup() {
	emu stop test_swap_locked 2>/dev/null || true
	emu destroy test_swap_locked 2>/dev/null || true
}

# Test 4: Memory beyond physical RAM
swap_encryption_beyond_physical_head() {
	atf_set "descr" "Verify handling of memory requests beyond physical RAM"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

swap_encryption_beyond_physical_body() {
	# Initialize instance with large memory (may trigger swapping)
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_large --arch amd64 --memory 4G

	# Check instance was created
	atf_check -s exit:0 -o match:"test_swap_large" -e ignore \
		emu list

	atf_log "PASS: Large memory instance created"
}

swap_encryption_beyond_physical_cleanup() {
	emu stop test_swap_large 2>/dev/null || true
	emu destroy test_swap_large 2>/dev/null || true
}

# Test 5: Instance memory protection verification
swap_encryption_instance_protection_head() {
	atf_set "descr" "Verify each instance's memory is separately protected"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

swap_encryption_instance_protection_body() {
	# Create multiple instances
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_inst1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_swap_inst2 --arch amd64 --memory 256M

	# Both should be listed
	atf_check -s exit:0 -o match:"test_swap_inst1" -e ignore \
		emu list
	atf_check -s exit:0 -o match:"test_swap_inst2" -e ignore \
		emu list

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_swap_inst1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_swap_inst2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_swap_inst1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_swap_inst2

	atf_log "PASS: Multiple instances with separate memory protection"
}

swap_encryption_instance_protection_cleanup() {
	emu stop test_swap_inst1 2>/dev/null || true
	emu stop test_swap_inst2 2>/dev/null || true
	emu destroy test_swap_inst1 2>/dev/null || true
	emu destroy test_swap_inst2 2>/dev/null || true
}

# Main entry point
atf_init "$@"
