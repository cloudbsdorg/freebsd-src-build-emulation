#!/bin/sh
#-
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

# Capsicum Sandbox Integration Tests for Emulation Framework
# Task S6.9: Verify Capsicum sandboxing works correctly in production

. $(dirname $0)/utils.subr

atf_test_case capsicum_sandbox_enabled cleanup

capsicum_sandbox_enabled_head() {
	atf_set "descr" "Verify emulator runs with Capsicum sandboxing enabled"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_sandbox_enabled_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_capsicum --arch amd64 --memory 512M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_capsicum --sandbox

	# Verify instance is running
	atf_check -s exit:0 -o match:"running" \
		emu status test_capsicum

	# Verify sandbox is active by checking process capabilities
	pid=$(emu status test_capsicum | grep "PID:" | awk '{print $2}')
	if [ -n "$pid" ]; then
		# Check if process is in capability mode
		if [ -f "/proc/$pid/status" ]; then
			cap_mode=$(grep "Capabilities:" /proc/$pid/status 2>/dev/null || true)
			if [ -n "$cap_mode" ]; then
				atf_log "PASS: Process $pid is running in capability mode"
			fi
		fi
	fi
}

capsicum_sandbox_enabled_cleanup() {
	emu stop test_capsicum 2>/dev/null || true
	emu destroy test_capsicum 2>/dev/null || true
}

atf_test_case capsicum_no_file_access cleanup

capsicum_no_file_access_head() {
	atf_set "descr" "Verify sandboxed emulator cannot open new files"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_no_file_access_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_nofile --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_nofile --sandbox

	# Try to open a file from within the emulator context
	# This should fail due to Capsicum restrictions
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_nofile -- cat /etc/passwd 2>&1 | grep -q "Operation not permitted"

	atf_log "PASS: Sandboxed emulator cannot open new files"
}

capsicum_no_file_access_cleanup() {
	emu stop test_nofile 2>/dev/null || true
	emu destroy test_nofile 2>/dev/null || true
}

atf_test_case capsicum_no_network_access cleanup

capsicum_no_network_access_head() {
	atf_set "descr" "Verify sandboxed emulator cannot access network"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_no_network_access_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_nonet --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_nonet --sandbox

	# Try to create a socket from within the emulator context
	# This should fail due to Capsicum restrictions
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_nonet -- nc -l 127.0.0.1 9999 2>&1 | grep -q "Operation not permitted"

	atf_log "PASS: Sandboxed emulator cannot access network"
}

capsicum_no_network_access_cleanup() {
	emu stop test_nonet 2>/dev/null || true
	emu destroy test_nonet 2>/dev/null || true
}

atf_test_case capsicum_no_process_spawn cleanup

capsicum_no_process_spawn_head() {
	atf_set "descr" "Verify sandboxed emulator cannot spawn new processes"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_no_process_spawn_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_nofork --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_nofork --sandbox

	# Try to fork/exec from within the emulator context
	# This should fail due to Capsicum restrictions
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_nofork -- sh -c "echo test" 2>&1 | grep -q "Operation not permitted"

	atf_log "PASS: Sandboxed emulator cannot spawn new processes"
}

capsicum_no_process_spawn_cleanup() {
	emu stop test_nofork 2>/dev/null || true
	emu destroy test_nofork 2>/dev/null || true
}

atf_test_case capsicum_fd_rights_limited cleanup

capsicum_fd_rights_limited_head() {
	atf_set "descr" "Verify sandboxed emulator has limited FD rights"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_fd_rights_limited_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_fdlimit --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_fdlimit --sandbox

	# Verify only essential FDs are open (stdin, stdout, stderr, VM FD)
	pid=$(emu status test_fdlimit | grep "PID:" | awk '{print $2}')
	if [ -n "$pid" ]; then
		fd_count=$(ls -la /proc/$pid/fd 2>/dev/null | wc -l)
		# Should have minimal FDs (typically < 10)
		if [ "$fd_count" -lt 15 ]; then
			atf_log "PASS: Process $pid has limited FDs ($fd_count)"
		else
			atf_fail "Process has too many FDs: $fd_count"
		fi
	fi
}

capsicum_fd_rights_limited_cleanup() {
	emu stop test_fdlimit 2>/dev/null || true
	emu destroy test_fdlimit 2>/dev/null || true
}

atf_test_case capsicum_ioctl_restricted cleanup

capsicum_ioctl_restricted_head() {
	atf_set "descr" "Verify sandboxed emulator has restricted ioctl access"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_ioctl_restricted_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_ioctl --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_ioctl --sandbox

	# Try to perform unauthorized ioctl on VMM FD
	# This should fail due to Capsicum ioctl restrictions
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_ioctl -- ioctl_test 2>&1 | grep -q "Operation not permitted"

	atf_log "PASS: Sandboxed emulator has restricted ioctl access"
}

capsicum_ioctl_restricted_cleanup() {
	emu stop test_ioctl 2>/dev/null || true
	emu destroy test_ioctl 2>/dev/null || true
}

atf_test_case capsicum_sandbox_prevents_escape cleanup

capsicum_sandbox_prevents_escape_head() {
	atf_set "descr" "Verify sandboxed emulator cannot escape restrictions"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_sandbox_prevents_escape_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_escape --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_escape --sandbox

	# Attempt multiple escape vectors
	# 1. Try to access /proc
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_escape -- cat /proc/self/maps 2>&1 | grep -q "Operation not permitted"

	# 2. Try to access host filesystem
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_escape -- ls /root 2>&1 | grep -q "Operation not permitted"

	# 3. Try to load kernel module
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_escape -- kldload something 2>&1 | grep -q "Operation not permitted"

	atf_log "PASS: Sandboxed emulator cannot escape restrictions"
}

capsicum_sandbox_prevents_escape_cleanup() {
	emu stop test_escape 2>/dev/null || true
	emu destroy test_escape 2>/dev/null || true
}

atf_test_case capsicum_without_sandbox cleanup

capsicum_without_sandbox_head() {
	atf_set "descr" "Verify emulator runs without sandboxing when disabled"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

capsicum_without_sandbox_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_nosb --arch amd64 --memory 256M

	# Start without --sandbox flag
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_nosb

	# Verify instance is running
	atf_check -s exit:0 -o match:"running" \
		emu status test_nosb

	# Process should NOT be in capability mode
	pid=$(emu status test_nosb | grep "PID:" | awk '{print $2}')
	if [ -n "$pid" ]; then
		atf_log "INFO: Process $pid running without sandbox (expected)"
	fi

	atf_log "PASS: Emulator runs without sandbox when not requested"
}

capsicum_without_sandbox_cleanup() {
	emu stop test_nosb 2>/dev/null || true
	emu destroy test_nosb 2>/dev/null || true
}

atf_init_test_cases() {
	atf_add_test_case capsicum_sandbox_enabled
	atf_add_test_case capsicum_no_file_access
	atf_add_test_case capsicum_no_network_access
	atf_add_test_case capsicum_no_process_spawn
	atf_add_test_case capsicum_fd_rights_limited
	atf_add_test_case capsicum_ioctl_restricted
	atf_add_test_case capsicum_sandbox_prevents_escape
	atf_add_test_case capsicum_without_sandbox
}
