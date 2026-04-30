#!/usr/bin/env atf-sh
#
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
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# Firmware Security Tests (S19.4)
#
# Test firmware blob verification mechanisms including SHA-256 hashing,
# GPG signature verification, and version checking.

. $(atf_get_srcdir)/utils.subr

atf_test_case firmware_sha256_verify cleanup
firmware_sha256_verify_head() {
	atf_set "descr" "Test SHA-256 verification of firmware blobs"
	atf_set "require.user" "root"
}
firmware_sha256_verify_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Create a test firmware blob
	local test_blob="/tmp/test_firmware.bin"
	dd if=/dev/random of=$test_blob bs=1024 count=10
	
	# Compute expected SHA-256 hash
	local expected_hash
	expected_hash=$(sha256 -q $test_blob)
	
	# Test verify command with correct hash
	if ! emu blob verify test_blob 2>/dev/null; then
		atf_fail "SHA-256 verification failed for valid blob"
	fi
	
	# Tamper with the blob
	echo "tampered" >> $test_blob
	
	# Test verify command should fail with tampered blob
	if emu blob verify test_blob 2>/dev/null; then
		atf_fail "SHA-256 verification succeeded for tampered blob"
	fi
}
firmware_sha256_verify_cleanup() {
	rm -f /tmp/test_firmware.bin
}

atf_test_case firmware_sha256_mismatch cleanup
firmware_sha256_mismatch_head() {
	atf_set "descr" "Test SHA-256 verification detects hash mismatch"
	atf_set "require.user" "root"
}
firmware_sha256_mismatch_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Create test blob with known content
	local test_blob="/tmp/test_blob_mismatch.bin"
	echo "test content" > $test_blob
	
	# Use wrong hash for verification
	if emu blob verify test_blob --wrong-hash 2>/dev/null; then
		atf_fail "SHA-256 verification succeeded with wrong hash"
	fi
}
firmware_sha256_mismatch_cleanup() {
	rm -f /tmp/test_blob_mismatch.bin
}

atf_test_case firmware_gpg_signature cleanup
firmware_gpg_signature_head() {
	atf_set "descr" "Test GPG signature verification of firmware blobs"
	atf_set "require.user" "root"
}
firmware_gpg_signature_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Create test blob and signature
	local test_blob="/tmp/test_signed_blob.bin"
	local test_sig="/tmp/test_signed_blob.bin.sig"
	
	echo "signed content" > $test_blob
	
	# Test verify-gpg command with valid signature
	if ! emu blob verify-gpg test_blob 2>/dev/null; then
		atf_fail "GPG signature verification failed for valid signature"
	fi
	
	# Tamper with blob
	echo "tampered" >> $test_blob
	
	# Test verify-gpg should fail with tampered blob
	if emu blob verify-gpg test_blob 2>/dev/null; then
		atf_fail "GPG signature verification succeeded for tampered blob"
	fi
}
firmware_gpg_signature_cleanup() {
	rm -f /tmp/test_signed_blob.bin /tmp/test_signed_blob.bin.sig
}

atf_test_case firmware_gpg_missing_signature cleanup
firmware_gpg_missing_signature_head() {
	atf_set "descr" "Test GPG verification fails when signature missing"
	atf_set "require.user" "root"
}
firmware_gpg_missing_signature_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Create test blob without signature
	local test_blob="/tmp/test_no_sig.bin"
	echo "unsigned content" > $test_blob
	
	# Test verify-gpg should fail without signature
	if emu blob verify-gpg test_blob 2>/dev/null; then
		atf_fail "GPG verification succeeded without signature file"
	fi
}
firmware_gpg_missing_signature_cleanup() {
	rm -f /tmp/test_no_sig.bin
}

atf_test_case firmware_version_check cleanup
firmware_version_check_head() {
	atf_set "descr" "Test firmware version checking against vulnerable versions"
	atf_set "require.user" "root"
}
firmware_version_check_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Test version check with safe version
	if ! emu blob check-version safe_blob 2>/dev/null; then
		atf_fail "Version check failed for safe version"
	fi
	
	# Test version check with vulnerable version
	if emu blob check-version vulnerable_blob 2>/dev/null; then
		atf_fail "Version check succeeded for vulnerable version"
	fi
}
firmware_version_check_cleanup() {
	:
}

atf_test_case firmware_version_no_data cleanup
firmware_version_no_data_head() {
	atf_set "descr" "Test version check handles missing vulnerability data"
	atf_set "require.user" "root"
}
firmware_version_no_data_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Test version check when no vulnerability data available
	# Should succeed with informational message
	if ! emu blob check-version no_data_blob 2>/dev/null; then
		atf_fail "Version check failed when vulnerability data unavailable"
	fi
}
firmware_version_no_data_cleanup() {
	:
}

atf_test_case firmware_blob_not_found cleanup
firmware_blob_not_found_head() {
	atf_set "descr" "Test blob operations handle non-existent blobs"
	atf_set "require.user" "root"
}
firmware_blob_not_found_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Test verify with non-existent blob
	if emu blob verify nonexistent_blob 2>/dev/null; then
		atf_fail "Verify succeeded for non-existent blob"
	fi
	
	# Test verify-gpg with non-existent blob
	if emu blob verify-gpg nonexistent_blob 2>/dev/null; then
		atf_fail "Verify-gpg succeeded for non-existent blob"
	fi
	
	# Test check-version with non-existent blob
	if emu blob check-version nonexistent_blob 2>/dev/null; then
		atf_fail "Check-version succeeded for non-existent blob"
	fi
}
firmware_blob_not_found_cleanup() {
	:
}

atf_test_case firmware_blob_list cleanup
firmware_blob_list_head() {
	atf_set "descr" "Test listing available firmware blobs"
	atf_set "require.user" "root"
}
firmware_blob_list_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Test blob list command
	if ! emu blob list 2>/dev/null; then
		atf_fail "Blob list command failed"
	fi
}
firmware_blob_list_cleanup() {
	:
}

atf_test_case firmware_blob_delete cleanup
firmware_blob_delete_head() {
	atf_set "descr" "Test deleting firmware blobs"
	atf_set "require.user" "root"
}
firmware_blob_delete_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Create test blob
	local test_blob="/tmp/test_delete_blob.bin"
	echo "delete me" > $test_blob
	
	# Test delete command
	if ! emu blob delete test_delete_blob 2>/dev/null; then
		atf_fail "Blob delete command failed"
	fi
	
	# Verify blob was deleted
	if [ -f $test_blob ]; then
		atf_fail "Blob file still exists after delete"
	fi
}
firmware_blob_delete_cleanup() {
	rm -f /tmp/test_delete_blob.bin
}

atf_test_case firmware_blob_delete_in_use cleanup
firmware_blob_delete_in_use_head() {
	atf_set "descr" "Test deleting blob in use by instance fails"
	atf_set "require.user" "root"
}
firmware_blob_delete_in_use_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Start instance with blob
	local inst_id
	inst_id=$(emu_start_test_instance_with_blob "test_in_use_blob")
	
	# Try to delete blob in use (should fail with EBUSY)
	if emu blob delete test_in_use_blob 2>/dev/null; then
		atf_fail "Delete succeeded for blob in use"
	fi
}
firmware_blob_delete_in_use_cleanup() {
	emu_cleanup_test_instance ${inst_id:-}
}

atf_test_case firmware_blob_info cleanup
firmware_blob_info_head() {
	atf_set "descr" "Test getting firmware blob information"
	atf_set "require.user" "root"
}
firmware_blob_info_body() {
	atf_skip "Emulator blob command not yet available for testing"
	
	# Test blob info command
	if ! emu blob info test_blob 2>/dev/null; then
		atf_fail "Blob info command failed"
	fi
}
firmware_blob_info_cleanup() {
	:
}

atf_init_test_cases() {
	atf_add_test_case firmware_sha256_verify
	atf_add_test_case firmware_sha256_mismatch
	atf_add_test_case firmware_gpg_signature
	atf_add_test_case firmware_gpg_missing_signature
	atf_add_test_case firmware_version_check
	atf_add_test_case firmware_version_no_data
	atf_add_test_case firmware_blob_not_found
	atf_add_test_case firmware_blob_list
	atf_add_test_case firmware_blob_delete
	atf_add_test_case firmware_blob_delete_in_use
	atf_add_test_case firmware_blob_info
}
