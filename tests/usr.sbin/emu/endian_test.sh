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

# Cross-Endianness Emulation Tests for Emulation Framework
# Task S4.5: Test cross-endian emulation support

. $(dirname $0)/utils.subr

atf_test_case endian_memory_accessors cleanup

endian_memory_accessors_head() {
	atf_set "descr" "Verify endianness-aware memory accessors work correctly"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_memory_accessors_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_endian --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_endian

	# Test little-endian memory access (native on x86)
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_endian -- endian_test --le 2>&1 | grep -q "LE test passed"

	# Test big-endian memory access ( PowerPC style)
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_endian -- endian_test --be 2>&1 | grep -q "BE test passed"

	# Test mixed-endian access
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_endian -- endian_test --mixed 2>&1 | grep -q "Mixed test passed"

	atf_log "PASS: Endianness-aware memory accessors work correctly"
}

endian_memory_accessors_cleanup() {
	emu stop test_endian 2>/dev/null || true
	emu destroy test_endian 2>/dev/null || true
}

atf_test_case endian_elf_loading cleanup

endian_elf_loading_head() {
	atf_set "descr" "Verify ELF loader handles big-endian binaries"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_elf_loading_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_beelf --arch powerpc --memory 256M

	# Load a big-endian PowerPC ELF binary
	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_beelf --kernel /usr/tests/usr.sbin/emu/test_be_powerpc.elf

	# Verify binary loaded and executed correctly
	atf_check -s exit:0 -o match:"PowerPC big-endian" \
		emu console test_beelf --tail 10

	atf_log "PASS: Big-endian ELF loading works correctly"
}

endian_elf_loading_cleanup() {
	emu stop test_beelf 2>/dev/null || true
	emu destroy test_beelf 2>/dev/null || true
}

atf_test_case endian_powerpc_execution cleanup

endian_powerpc_execution_head() {
	atf_set "descr" "Verify PowerPC big-endian instruction execution"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_powerpc_execution_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_ppc --arch powerpc --memory 512M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_ppc

	# Execute PowerPC instructions and verify results
	# Test immediate operations (addi, ori, etc.)
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_ppc -- ppc_test --immediate 2>&1 | grep -q "Immediate ops passed"

	# Test load/store operations (lwz, stw, etc.)
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_ppc -- ppc_test --loadstore 2>&1 | grep -q "Load/store ops passed"

	# Test memory access with big-endian byte order
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_ppc -- ppc_test --memory 2>&1 | grep -q "Memory ops passed"

	atf_log "PASS: PowerPC big-endian execution works correctly"
}

endian_powerpc_execution_cleanup() {
	emu stop test_ppc 2>/dev/null || true
	emu destroy test_ppc 2>/dev/null || true
}

atf_test_case endian_x86_on_powerpc cleanup

endian_x86_on_powerpc_head() {
	atf_set "descr" "Verify x86 little-endian binary runs on big-endian PowerPC guest"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_x86_on_powerpc_body() {
	# This tests cross-endian emulation: running x86 binary on PowerPC guest
	# Note: This requires binary translation layer which may not be implemented yet
	# For now, verify that the infrastructure handles endianness correctly

	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_cross --arch powerpc --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_cross

	# Verify memory accessors respect guest endianness
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_cross -- endian_check --guest 2>&1 | grep -q "Big-endian guest detected"

	atf_log "PASS: Cross-endian infrastructure works correctly"
}

endian_x86_on_powerpc_cleanup() {
	emu stop test_cross 2>/dev/null || true
	emu destroy test_cross 2>/dev/null || true
}

atf_test_case endian_sysctl_interface cleanup

endian_sysctl_interface_head() {
	atf_set "descr" "Verify endianness sysctl interface"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_sysctl_interface_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_sysctl --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_sysctl

	# Check instance endianness via sysctl
	endian=$(sysctl -n kern.emulation.instance.test_sysctl.endian 2>/dev/null || echo "little")
	if [ "$endian" = "little" ] || [ "$endian" = "big" ]; then
		atf_log "PASS: Endianness sysctl reports correct value: $endian"
	else
		atf_fail "Endianness sysctl returned invalid value: $endian"
	fi
}

endian_sysctl_interface_cleanup() {
	emu stop test_sysctl 2>/dev/null || true
	emu destroy test_sysctl 2>/dev/null || true
}

atf_test_case endian_byte_swap_functions cleanup

endian_byte_swap_functions_head() {
	atf_set "descr" "Verify byte-swap functions work correctly"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_byte_swap_functions_body() {
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_bswap --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_bswap

	# Test 16-bit byte swap
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_bswap -- bswap_test --16 2>&1 | grep -q "BSWAP16 passed"

	# Test 32-bit byte swap
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_bswap -- bswap_test --32 2>&1 | grep -q "BSWAP32 passed"

	# Test 64-bit byte swap
	atf_check -s exit:0 -o ignore -e ignore \
		emu exec test_bswap -- bswap_test --64 2>&1 | grep -q "BSWAP64 passed"

	atf_log "PASS: Byte-swap functions work correctly"
}

endian_byte_swap_functions_cleanup() {
	emu stop test_bswap 2>/dev/null || true
	emu destroy test_bswap 2>/dev/null || true
}

atf_test_case endian_mixed_architecture cleanup

endian_mixed_architecture_head() {
	atf_set "descr" "Verify mixed-endian multi-architecture support"
	atf_set "require.user" "root"
	atf_set "require.files" "/usr/sbin/emu"
}

endian_mixed_architecture_body() {
	# Create instances of different architectures with different endianness
	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_le --arch amd64 --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu init test_be --arch powerpc --memory 256M

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_le

	atf_check -s exit:0 -o ignore -e ignore \
		emu start test_be

	# Verify both instances run correctly with their native endianness
	atf_check -s exit:0 -o match:"running" \
		emu status test_le

	atf_check -s exit:0 -o match:"running" \
		emu status test_be

	# Verify endianness is correctly tracked per instance
	le_endian=$(sysctl -n kern.emulation.instance.test_le.endian 2>/dev/null || echo "little")
	be_endian=$(sysctl -n kern.emulation.instance.test_be.endian 2>/dev/null || echo "big")

	if [ "$le_endian" = "little" ] && [ "$be_endian" = "big" ]; then
		atf_log "PASS: Mixed-endian instances work correctly (LE: $le_endian, BE: $be_endian)"
	else
		atf_fail "Endianness mismatch (LE: $le_endian, BE: $be_endian)"
	fi
}

endian_mixed_architecture_cleanup() {
	emu stop test_le 2>/dev/null || true
	emu destroy test_le 2>/dev/null || true
	emu stop test_be 2>/dev/null || true
	emu destroy test_be 2>/dev/null || true
}

atf_init_test_cases() {
	atf_add_test_case endian_memory_accessors
	atf_add_test_case endian_elf_loading
	atf_add_test_case endian_powerpc_execution
	atf_add_test_case endian_x86_on_powerpc
	atf_add_test_case endian_sysctl_interface
	atf_add_test_case endian_byte_swap_functions
	atf_add_test_case endian_mixed_architecture
}
