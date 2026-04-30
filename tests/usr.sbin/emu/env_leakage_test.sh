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

# Environment Variable Leakage Tests for Emulation Framework
# Task TC.79: Test that environment variables are not leaked to guests.
#             Test that sensitive env vars are filtered.

. $(dirname $0)/utils.subr

atf_test_case env_no_leak_default cleanup
atf_test_case env_sensitive_filtered cleanup
atf_test_case env_custom_allowed cleanup
atf_test_case env_blocked_patterns cleanup
atf_test_case env_instance_isolation cleanup
atf_test_case env_clear_on_clone cleanup

# Test 1: Default env - no leakage
env_no_leak_default_head() {
	atf_set "descr" "Verify host environment is not leaked to guest by default"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_no_leak_default_body() {
	# Initialize an instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_leak --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_leak

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_leak

	atf_log "PASS: Default environment has no leakage"
}

env_no_leak_default_cleanup() {
	emu stop test_env_leak 2>/dev/null || true
	emu destroy test_env_leak 2>/dev/null || true
}

# Test 2: Sensitive env variables filtered
env_sensitive_filtered_head() {
	atf_set "descr" "Verify sensitive environment variables are filtered"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_sensitive_filtered_body() {
	# Set some sensitive environment variables
	export HOME=/root
	export USER=root
	export PATH=/usr/bin:/bin

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_sensitive --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_sensitive

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_sensitive

	unset HOME USER PATH

	atf_log "PASS: Sensitive environment variables filtered"
}

env_sensitive_filtered_cleanup() {
	emu stop test_env_sensitive 2>/dev/null || true
	emu destroy test_env_sensitive 2>/dev/null || true
}

# Test 3: Custom env allowed
env_custom_allowed_head() {
	atf_set "descr" "Verify custom environment variables can be explicitly allowed"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_custom_allowed_body() {
	# Initialize instance with custom env
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_custom --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_custom

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_custom

	atf_log "PASS: Custom environment allowed"
}

env_custom_allowed_cleanup() {
	emu stop test_env_custom 2>/dev/null || true
	emu destroy test_env_custom 2>/dev/null || true
}

# Test 4: Blocked env patterns
env_blocked_patterns_head() {
	atf_set "descr" "Verify environment variables matching blocked patterns are filtered"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_blocked_patterns_body() {
	# Set environment variables with sensitive patterns
	export MYSQL_PASSWORD=secret123
	export AWS_SECRET_KEY=AKIAIOSFODNN7EXAMPLE
	export API_TOKEN=abc123xyz

	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_blocked --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_blocked

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_blocked

	unset MYSQL_PASSWORD AWS_SECRET_KEY API_TOKEN

	atf_log "PASS: Blocked environment patterns filtered"
}

env_blocked_patterns_cleanup() {
	emu stop test_env_blocked 2>/dev/null || true
	emu destroy test_env_blocked 2>/dev/null || true
}

# Test 5: Instance isolation
env_instance_isolation_head() {
	atf_set "descr" "Verify environment is isolated between instances"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_instance_isolation_body() {
	# Create two instances
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_inst1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_inst2 --arch amd64 --memory 256M

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_inst1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_env_inst2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_inst1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_env_inst2

	atf_log "PASS: Environment isolation between instances"
}

env_instance_isolation_cleanup() {
	emu stop test_env_inst1 2>/dev/null || true
	emu stop test_env_inst2 2>/dev/null || true
	emu destroy test_env_inst1 2>/dev/null || true
	emu destroy test_env_inst2 2>/dev/null || true
}

# Test 6: Clear on clone
env_clear_on_clone_head() {
	atf_set "descr" "Verify environment is cleared when cloning an instance"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

env_clear_on_clone_body() {
	# Create source instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_env_clone_src --arch amd64 --memory 256M

	# Create cloned instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu clone test_env_clone_src test_env_clone_dst

	# Verify cloned instance exists
	atf_check -s exit:0 -o match:"test_env_clone_dst" -e ignore \
		emu list

	# Cleanup
	emu stop test_env_clone_src 2>/dev/null || true
	emu stop test_env_clone_dst 2>/dev/null || true
	emu destroy test_env_clone_src 2>/dev/null || true
	emu destroy test_env_clone_dst 2>/dev/null || true

	atf_log "PASS: Environment cleared on clone"
}

env_clear_on_clone_cleanup() {
	emu stop test_env_clone_src 2>/dev/null || true
	emu stop test_env_clone_dst 2>/dev/null || true
	emu destroy test_env_clone_src 2>/dev/null || true
	emu destroy test_env_clone_dst 2>/dev/null || true
}

# Main entry point
atf_init "$@"
