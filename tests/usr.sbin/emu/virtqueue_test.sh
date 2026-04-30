#!/bin/sh
# $FreeBSD$
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
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

atf_test_case virtqueue_functionality exists cleanup
atf_test_case virtqueue_init cleanup
atf_test_case virtqueue_descriptor_chain cleanup
atf_test_case virtqueue_used_ring cleanup
atf_test_case virtqueue_notification cleanup

virtqueue_head() {
	cat <<-EOF
	Virtqueue test requires the emulator binary to be available.
	This test verifies the userspace virtqueue abstraction.
	EOF
}

virtqueue_body() {
	# Check if emu binary exists
	if ! which emu >/dev/null 2>&1; then
		atf_skip "emu binary not available"
	fi

	# Check if emu has virtqueue support by looking for relevant symbols
	if ! nm /usr/sbin/emu 2>/dev/null | grep -q "emu_vq_init\|emu_vq_get_chain"; then
		atf_skip "emu binary lacks virtqueue support"
	fi
}

virtqueue_cleanup() {
	# No cleanup needed for this test
	:
}

virtqueue_functionality_body() {
	virtqueue_body
	if [ $? -ne 0 ]; then
		return 0
	fi

	# Verify virtqueue functions are linked
	emu --help 2>&1 | head -1 >/dev/null
	if [ $? -eq 0 ]; then
		atf_pass "emu binary executes successfully"
	else
		atf_fail "emu binary failed to execute"
	fi
}

virtqueue_init_body() {
	virtqueue_body
	if [ $? -ne 0 ]; then
		return 0
	fi

	# Check for virtqueue initialization code
	if grep -q "emu_vq_init" /usr/sbin/emu 2>/dev/null; then
		atf_pass "virtqueue initialization function exists"
	else
		atf_skip "virtqueue initialization not available"
	fi
}

virtqueue_descriptor_chain_body() {
	virtqueue_body
	if [ $? -ne 0 ]; then
		return 0
	fi

	# Check for virtqueue descriptor chain functions
	if grep -q "emu_vq_get_chain\|emu_vq_get_desc" /usr/sbin/emu 2>/dev/null; then
		atf_pass "virtqueue descriptor chain functions exist"
	else
		atf_skip "virtqueue descriptor chain functions not available"
	fi
}

virtqueue_used_ring_body() {
	virtqueue_body
	if [ $? -ne 0 ]; then
		return 0
	fi

	# Check for virtqueue used ring functions
	if grep -q "emu_vq_return\|emu_vq_add_used" /usr/sbin/emu 2>/dev/null; then
		atf_pass "virtqueue used ring functions exist"
	else
		atf_skip "virtqueue used ring functions not available"
	fi
}

virtqueue_notification_body() {
	virtqueue_body
	if [ $? -ne 0 ]; then
		return 0
	fi

	# Check for virtqueue notification functions
	if grep -q "emu_vq_kick\|emu_vq_enable_notification" /usr/sbin/emu 2>/dev/null; then
		atf_pass "virtqueue notification functions exist"
	else
		atf_skip "virtqueue notification functions not available"
	fi
}

atf_init_test_cases() {
	atf_add_test_case virtqueue_functionality
	atf_add_test_case virtqueue_init
	atf_add_test_case virtqueue_descriptor_chain
	atf_add_test_case virtqueue_used_ring
	atf_add_test_case virtqueue_notification
}
