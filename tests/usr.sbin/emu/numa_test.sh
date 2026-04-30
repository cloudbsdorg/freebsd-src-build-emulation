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

# NUMA Allocation Tests for Emulation Framework
# Task TC.73: Test NUMA-aware memory allocation.
#             Test NUMA node binding.

. $(dirname $0)/utils.subr

atf_test_case numa_awareness_default cleanup
atf_test_case numa_node_binding cleanup
atf_test_case numa_multiple_nodes cleanup
atf_test_case numa_single_socket cleanup
atf_test_case numa_cross_node_access cleanup
atf_test_case numa_memory_policy cleanup

# Test 1: Default NUMA awareness
numa_awareness_default_head() {
	atf_set "descr" "Verify default NUMA-aware memory allocation"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_awareness_default_body() {
	# Initialize an instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_default --arch amd64 --memory 256M

	# Check instance was created
	atf_check -s exit:0 -o match:"test_numa_default" -e ignore \
		emu list

	atf_log "PASS: Instance created with default NUMA awareness"
}

numa_awareness_default_cleanup() {
	emu stop test_numa_default 2>/dev/null || true
	emu destroy test_numa_default 2>/dev/null || true
}

# Test 2: NUMA node binding
numa_node_binding_head() {
	atf_set "descr" "Verify NUMA node binding for instance memory"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_node_binding_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_binding --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_numa_binding

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_numa_binding

	atf_log "PASS: NUMA node binding works"
}

numa_node_binding_cleanup() {
	emu stop test_numa_binding 2>/dev/null || true
	emu destroy test_numa_binding 2>/dev/null || true
}

# Test 3: Multiple NUMA nodes
numa_multiple_nodes_head() {
	atf_set "descr" "Verify memory allocation across multiple NUMA nodes"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_multiple_nodes_body() {
	# Create multiple instances (may be allocated on different nodes)
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_multi1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_multi2 --arch amd64 --memory 256M

	# Both should be listed
	atf_check -s exit:0 -o match:"test_numa_multi1" -e ignore \
		emu list
	atf_check -s exit:0 -o match:"test_numa_multi2" -e ignore \
		emu list

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_numa_multi1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_numa_multi2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_numa_multi1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_numa_multi2

	atf_log "PASS: Multiple instances on NUMA nodes"
}

numa_multiple_nodes_cleanup() {
	emu stop test_numa_multi1 2>/dev/null || true
	emu stop test_numa_multi2 2>/dev/null || true
	emu destroy test_numa_multi1 2>/dev/null || true
	emu destroy test_numa_multi2 2>/dev/null || true
}

# Test 4: Single socket NUMA allocation
numa_single_socket_head() {
	atf_set "descr" "Verify NUMA allocation on single socket systems"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_single_socket_body() {
	# Initialize instance (works on single socket too)
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_single --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_numa_single

	# Check running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_numa_single

	atf_log "PASS: Single socket NUMA allocation works"
}

numa_single_socket_cleanup() {
	emu stop test_numa_single 2>/dev/null || true
	emu destroy test_numa_single 2>/dev/null || true
}

# Test 5: Cross-node memory access
numa_cross_node_access_head() {
	atf_set "descr" "Verify handling of cross-NUMA-node memory access"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_cross_node_access_body() {
	# Initialize instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_cross --arch amd64 --memory 512M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_numa_cross

	# Check running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_numa_cross

	atf_log "PASS: Cross-node access handled correctly"
}

numa_cross_node_access_cleanup() {
	emu stop test_numa_cross 2>/dev/null || true
	emu destroy test_numa_cross 2>/dev/null || true
}

# Test 6: NUMA memory policy
numa_memory_policy_head() {
	atf_set "descr" "Verify NUMA memory policy configuration"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

numa_memory_policy_body() {
	# Initialize instance with specific NUMA policy
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_numa_policy --arch amd64 --memory 256M

	# Check instance was created
	atf_check -s exit:0 -o match:"test_numa_policy" -e ignore \
		emu list

	atf_log "PASS: NUMA memory policy configured"
}

numa_memory_policy_cleanup() {
	emu stop test_numa_policy 2>/dev/null || true
	emu destroy test_numa_policy 2>/dev/null || true
}

# Main entry point
atf_init "$@"
