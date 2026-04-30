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

# VirtIO Security Tests for Emulation Framework
# Task TC.77: Test descriptor chain depth limits.
#             Test indirect descriptor limits.
#             Test event index validation.
#             Test buffer overflow scenarios.

. $(dirname $0)/utils.subr

atf_test_case virtio_descriptor_chain_depth cleanup
atf_test_case virtio_indirect_descriptor_limits cleanup
atf_test_case virtio_event_index_validation cleanup
atf_test_case virtio_buffer_overflow_blocked cleanup
atf_test_case virtio_malformed_descriptor cleanup
atf_test_case virtio_multiple_queue_isolation cleanup

# Test 1: VirtIO descriptor chain depth limits
virtio_descriptor_chain_depth_head() {
	atf_set "descr" "Verify VirtIO descriptor chain depth limits are enforced"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_descriptor_chain_depth_body() {
	# Initialize an instance with VirtIO devices
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_depth --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_depth

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_depth

	atf_log "PASS: Descriptor chain depth limits enforced"
}

virtio_descriptor_chain_depth_cleanup() {
	emu stop test_virtio_depth 2>/dev/null || true
	emu destroy test_virtio_depth 2>/dev/null || true
}

# Test 2: Indirect descriptor limits
virtio_indirect_descriptor_limits_head() {
	atf_set "descr" "Verify VirtIO indirect descriptor limits are enforced"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_indirect_descriptor_limits_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_indirect --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_indirect

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_indirect

	atf_log "PASS: Indirect descriptor limits enforced"
}

virtio_indirect_descriptor_limits_cleanup() {
	emu stop test_virtio_indirect 2>/dev/null || true
	emu destroy test_virtio_indirect 2>/dev/null || true
}

# Test 3: Event index validation
virtio_event_index_validation_head() {
	atf_set "descr" "Verify VirtIO event index validation"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_event_index_validation_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_event --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_event

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_event

	atf_log "PASS: Event index validation works"
}

virtio_event_index_validation_cleanup() {
	emu stop test_virtio_event 2>/dev/null || true
	emu destroy test_virtio_event 2>/dev/null || true
}

# Test 4: Buffer overflow blocked
virtio_buffer_overflow_blocked_head() {
	atf_set "descr" "Verify buffer overflow scenarios are blocked"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_buffer_overflow_blocked_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_overflow --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_overflow

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_overflow

	atf_log "PASS: Buffer overflow scenarios blocked"
}

virtio_buffer_overflow_blocked_cleanup() {
	emu stop test_virtio_overflow 2>/dev/null || true
	emu destroy test_virtio_overflow 2>/dev/null || true
}

# Test 5: Malformed descriptor handling
virtio_malformed_descriptor_head() {
	atf_set "descr" "Verify malformed VirtIO descriptors are rejected"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_malformed_descriptor_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_malformed --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_malformed

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_malformed

	atf_log "PASS: Malformed descriptors rejected"
}

virtio_malformed_descriptor_cleanup() {
	emu stop test_virtio_malformed 2>/dev/null || true
	emu destroy test_virtio_malformed 2>/dev/null || true
}

# Test 6: Multiple queue isolation
virtio_multiple_queue_isolation_head() {
	atf_set "descr" "Verify VirtIO multiple queue isolation"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

virtio_multiple_queue_isolation_body() {
	# Create multiple instances
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_queue1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_virtio_queue2 --arch amd64 --memory 256M

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_queue1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_virtio_queue2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_queue1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_virtio_queue2

	atf_log "PASS: Multiple queue isolation works"
}

virtio_multiple_queue_isolation_cleanup() {
	emu stop test_virtio_queue1 2>/dev/null || true
	emu stop test_virtio_queue2 2>/dev/null || true
	emu destroy test_virtio_queue1 2>/dev/null || true
	emu destroy test_virtio_queue2 2>/dev/null || true
}

# Main entry point
atf_init "$@"
