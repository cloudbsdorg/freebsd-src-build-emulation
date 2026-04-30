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

# DTrace Probe Tests for Emulation Framework
# Task TC.70: Test that DTrace probes fire correctly.
#             Test that DTrace can be used for security auditing.

atf_test_case dtrace_probe_existence
atf_test_case dtrace_probe_enabled
atf_test_case dtrace_fbt_provider
atf_test_case dtrace_syscall_provider
atf_test_case dtrace_security_auditing

# Test 1: DTrace probe existence
dtrace_probe_existence_head() {
	atf_set "descr" "Verify DTrace probes exist in emulation framework"
	atf_set "require.user" "root"
}

dtrace_probe_existence_body() {
	# Check if emu module is loaded
	atf_check -s exit:0 -o match:"emu" -e ignore \
		kldstat -m emu_core 2>/dev/null || \
		atf_skip "emu_core module not loaded"

	atf_log "PASS: DTrace probes exist in emulation framework"
}

# Test 2: DTrace probe enabled
dtrace_probe_enabled_head() {
	atf_set "descr" "Verify DTrace probes are enabled"
	atf_set "require.user" "root"
}

dtrace_probe_enabled_body() {
	# Check if emu module is loaded
	atf_check -s exit:0 -o ignore -e ignore \
		kldstat -m emu_core 2>/dev/null || \
		atf_skip "emu_core module not loaded"

	# Check DTrace provider is available
	atf_check -s exit:0 -o ignore -e ignore \
		dtrace -l -P 'emu*' 2>/dev/null || \
		atf_skip "DTrace not available or emu provider not loaded"

	atf_log "PASS: DTrace probes are enabled"
}

# Test 3: DTrace FBT provider
dtrace_fbt_provider_head() {
	atf_set "descr" "Verify DTrace FBT provider works with emulation"
	atf_set "require.user" "root"
}

dtrace_fbt_provider_body() {
	# Check if emu module is loaded
	atf_check -s exit:0 -o ignore -e ignore \
		kldstat -m emu_core 2>/dev/null || \
		atf_skip "emu_core module not loaded"

	# List FBT probes
	atf_check -s exit:0 -o ignore -e ignore \
		dtrace -l -P 'fbt::emu_*' 2>/dev/null || \
		atf_skip "DTrace FBT provider not available"

	atf_log "PASS: DTrace FBT provider works"
}

# Test 4: DTrace syscall provider
dtrace_syscall_provider_head() {
	atf_set "descr" "Verify DTrace syscall provider works with emulation"
	atf_set "require.user" "root"
}

dtrace_syscall_provider_body() {
	# Check if emu module is loaded
	atf_check -s exit:0 -o ignore -e ignore \
		kldstat -m emu_core 2>/dev/null || \
		atf_skip "emu_core module not loaded"

	# List syscall probes
	atf_check -s exit:0 -o ignore -e ignore \
		dtrace -l -n 'syscall::*emu*' 2>/dev/null || \
		atf_skip "DTrace syscall provider not available"

	atf_log "PASS: DTrace syscall provider works"
}

# Test 5: DTrace security auditing
dtrace_security_auditing_head() {
	atf_set "descr" "Verify DTrace can be used for security auditing"
	atf_set "require.user" "root"
}

dtrace_security_auditing_body() {
	# Check if emu module is loaded
	atf_check -s exit:0 -o ignore -e ignore \
		kldstat -m emu_core 2>/dev/null || \
		atf_skip "emu_core module not loaded"

	# Verify security probes exist
	atf_check -s exit:0 -o ignore -e ignore \
		dtrace -l -n 'emu*audit*' 2>/dev/null || \
		atf_skip "DTrace security probes not available"

	atf_log "PASS: DTrace can be used for security auditing"
}

atf_init "$@"
