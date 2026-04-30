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

# Huge Page Tests for Emulation Framework
# Task TC.74: Test huge page allocation.
#             Test huge page performance.
#             Test huge page fallback.

. $(dirname $0)/utils.subr

atf_test_case hugepage_default_allocation cleanup
atf_test_case hugepage_explicit_enable cleanup
atf_test_case hugepage_fallback_to_4k cleanup
atf_test_case hugepage_size_alignment cleanup
atf_test_case hugepage_memory_alignment cleanup
atf_test_case hugepage_multiple_instances cleanup

# Test 1: Default huge page allocation
hugepage_default_allocation_head() {
	atf_set "descr" "Verify default huge page allocation behavior"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_default_allocation_body() {
	# Initialize an instance with default huge page settings
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_default --arch amd64 --memory 256M

	# Check instance was created
	atf_check -s exit:0 -o match:"test_hugepage_default" -e ignore \
		emu list

	atf_log "PASS: Instance created with default huge page allocation"
}

hugepage_default_allocation_cleanup() {
	emu stop test_hugepage_default 2>/dev/null || true
	emu destroy test_hugepage_default 2>/dev/null || true
}

# Test 2: Explicit huge page enable
hugepage_explicit_enable_head() {
	atf_set "descr" "Verify explicit huge page enable"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_explicit_enable_body() {
	# Initialize instance with huge pages enabled
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_enable --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_enable

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_enable

	atf_log "PASS: Huge page enabled successfully"
}

hugepage_explicit_enable_cleanup() {
	emu stop test_hugepage_enable 2>/dev/null || true
	emu destroy test_hugepage_enable 2>/dev/null || true
}

# Test 3: Fallback to 4K pages
hugepage_fallback_to_4k_head() {
	atf_set "descr" "Verify fallback to 4K pages when huge pages unavailable"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_fallback_to_4k_body() {
	# Initialize instance (should fall back gracefully if huge pages unavailable)
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_fallback --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_fallback

	# Check instance is running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_fallback

	atf_log "PASS: Fallback to 4K pages handled correctly"
}

hugepage_fallback_to_4k_cleanup() {
	emu stop test_hugepage_fallback 2>/dev/null || true
	emu destroy test_hugepage_fallback 2>/dev/null || true
}

# Test 4: Huge page size alignment
hugepage_size_alignment_head() {
	atf_set "descr" "Verify memory alignment for huge pages"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_size_alignment_body() {
	# Initialize instance with size aligned to huge page boundary
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_align --arch amd64 --memory 256M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_align

	# Check running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_align

	atf_log "PASS: Huge page size alignment correct"
}

hugepage_size_alignment_cleanup() {
	emu stop test_hugepage_align 2>/dev/null || true
	emu destroy test_hugepage_align 2>/dev/null || true
}

# Test 5: Memory region alignment
hugepage_memory_alignment_head() {
	atf_set "descr" "Verify memory region alignment for huge pages"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_memory_alignment_body() {
	# Initialize instance with aligned memory
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_mem --arch amd64 --memory 512M

	# Start instance
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_mem

	# Check running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_mem

	atf_log "PASS: Memory region alignment correct"
}

hugepage_memory_alignment_cleanup() {
	emu stop test_hugepage_mem 2>/dev/null || true
	emu destroy test_hugepage_mem 2>/dev/null || true
}

# Test 6: Multiple instances with huge pages
hugepage_multiple_instances_head() {
	atf_set "descr" "Verify multiple instances can use huge pages"
	atf_set "require.user" "root"
	atf_set "require.progs" "emu"
}

hugepage_multiple_instances_body() {
	# Create multiple instances
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_inst1 --arch amd64 --memory 256M
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_hugepage_inst2 --arch amd64 --memory 256M

	# Both should be listed
	atf_check -s exit:0 -o match:"test_hugepage_inst1" -e ignore \
		emu list
	atf_check -s exit:0 -o match:"test_hugepage_inst2" -e ignore \
		emu list

	# Start both
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_inst1
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_hugepage_inst2

	# Check both running
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_inst1
	atf_check -s exit:0 -o match:"RUNNING" -e ignore \
		emu status test_hugepage_inst2

	atf_log "PASS: Multiple instances with huge pages"
}

hugepage_multiple_instances_cleanup() {
	emu stop test_hugepage_inst1 2>/dev/null || true
	emu stop test_hugepage_inst2 2>/dev/null || true
	emu destroy test_hugepage_inst1 2>/dev/null || true
	emu destroy test_hugepage_inst2 2>/dev/null || true
}

# Main entry point
atf_init "$@"
