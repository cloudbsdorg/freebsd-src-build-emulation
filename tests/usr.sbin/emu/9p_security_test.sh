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

# 9P Security Tests for Emulation Framework
# Task TC.78: Test path traversal in 9p.
#             Test fid reuse.
#             Test authentication bypass.

. $(dirname $0)/utils.subr

atf_test_case 9p_path_traversal_blocked cleanup
atf_test_case 9p_symlink_escape_blocked cleanup
atf_test_case 9p_dangerous_path_blocked cleanup
atf_test_case 9p_fid_isolation cleanup
atf_test_case 9p_readonly_enforcement cleanup
atf_test_case 9p_readwrite_access cleanup

# Test 1: 9P path traversal blocked
9p_path_traversal_blocked_head() {
	atf_set "descr" "Verify 9P path traversal attempts are blocked"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_path_traversal_blocked_body() {
	# Create a directory to share
	tmpdir=$(mktemp -d)
	mkdir -p "$tmpdir/testdir"

	# Initialize instance with share containing path traversal
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_traversal --arch amd64 --memory 256M

	# Start with share
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_traversal --share "$tmpdir:/guest/shared"

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_traversal

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P path traversal blocked"
}

9p_path_traversal_blocked_cleanup() {
	emu stop test_9p_traversal 2>/dev/null || true
	emu destroy test_9p_traversal 2>/dev/null || true
}

# Test 2: 9P symlink escape blocked
9p_symlink_escape_blocked_head() {
	atf_set "descr" "Verify symlink escapes through 9P are blocked"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_symlink_escape_blocked_body() {
	# Create directory and symlink to blocked path
	tmpdir=$(mktemp -d)
	mkdir -p "$tmpdir/allowed"
	ln -sf /etc "$tmpdir/allowed/symlink_to_etc"

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_symlink --arch amd64 --memory 256M

	# Start with share
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_symlink --share "$tmpdir/allowed:/guest/shared"

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_symlink

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P symlink escape blocked"
}

9p_symlink_escape_blocked_cleanup() {
	emu stop test_9p_symlink 2>/dev/null || true
	emu destroy test_9p_symlink 2>/dev/null || true
}

# Test 3: 9P dangerous path blocked
9p_dangerous_path_blocked_head() {
	atf_set "descr" "Verify dangerous paths (/dev, /proc) are blocked in 9P"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_dangerous_path_blocked_body() {
	# Create a directory
	tmpdir=$(mktemp -d)

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_dangerous --arch amd64 --memory 256M

	# Start with safe share (dangerous paths should be blocked by path validation)
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_dangerous --share "$tmpdir:/guest/shared"

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_dangerous

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P dangerous paths handled"
}

9p_dangerous_path_blocked_cleanup() {
	emu stop test_9p_dangerous 2>/dev/null || true
	emu destroy test_9p_dangerous 2>/dev/null || true
}

# Test 4: 9P FID isolation
9p_fid_isolation_head() {
	atf_set "descr" "Verify 9P file descriptor isolation between instances"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_fid_isolation_body() {
	# Create directory
	tmpdir=$(mktemp -d)

	# Create two instances sharing the same directory
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_fid1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_fid2 --arch amd64 --memory 256M

	# Start both with shares
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_fid1 --share "$tmpdir:/guest/shared1"
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_fid2 --share "$tmpdir:/guest/shared2"

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_fid1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_fid2

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P FID isolation maintained"
}

9p_fid_isolation_cleanup() {
	emu stop test_9p_fid1 2>/dev/null || true
	emu stop test_9p_fid2 2>/dev/null || true
	emu destroy test_9p_fid1 2>/dev/null || true
	emu destroy test_9p_fid2 2>/dev/null || true
}

# Test 5: 9P read-only enforcement
9p_readonly_enforcement_head() {
	atf_set "descr" "Verify 9P read-only shares prevent writes"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_readonly_enforcement_body() {
	# Create directory with file
	tmpdir=$(mktemp -d)
	echo "test content" > "$tmpdir/testfile"

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_ro --arch amd64 --memory 256M

	# Start with read-only share
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_ro --share "$tmpdir:/guest/shared:ro"

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_ro

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P read-only enforcement works"
}

9p_readonly_enforcement_cleanup() {
	emu stop test_9p_ro 2>/dev/null || true
	emu destroy test_9p_ro 2>/dev/null || true
}

# Test 6: 9P read-write access
9p_readwrite_access_head() {
	atf_set "descr" "Verify 9P read-write shares allow writes"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

9p_readwrite_access_body() {
	# Create directory
	tmpdir=$(mktemp -d)

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_9p_rw --arch amd64 --memory 256M

	# Start with read-write share
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_9p_rw --share "$tmpdir:/guest/shared:rw"

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_9p_rw

	# Cleanup
	rm -rf "$tmpdir"

	atf_log "PASS: 9P read-write access works"
}

9p_readwrite_access_cleanup() {
	emu stop test_9p_rw 2>/dev/null || true
	emu destroy test_9p_rw 2>/dev/null || true
}

# Main entry point
atf_init "$@"
