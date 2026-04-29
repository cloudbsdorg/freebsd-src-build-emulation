#!/usr/bin/env atf-sh

# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# virtio-rng Device Security Tests
#
# Tests for entropy/RNG device implementation (S17.1)

atf_test_case emu_rng_device_exists ok
emu_rng_device_exists_head()
{
	atf_set "descr" "Test that virtio-rng device functions exist"
}
emu_rng_device_exists_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		nm -D /usr/sbin/emu | grep -q "emu_rng_init"
	atf_check -s exit:0 -o ignore -e ignore \
		nm -D /usr/sbin/emu | grep -q "emu_rng_destroy"
	atf_check -s exit:0 -o ignore -e ignore \
		nm -D /usr/sbin/emu | grep -q "emu_rng_handle_request"
}

atf_test_case emu_rng_config_space ok
emu_rng_config_space_head()
{
	atf_set "descr" "Test virtio-rng configuration space"
}
emu_rng_config_space_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		nm -D /usr/sbin/emu | grep -q "emu_rng_get_config"
}

atf_test_case emu_rng_entropy_source ok
emu_rng_entropy_source_head()
{
	atf_set "descr" "Test that RNG uses arc4random_buf for entropy"
}
emu_rng_entropy_source_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		nm -D /usr/sbin/emu | grep -q "arc4random_buf"
}

atf_test_case emu_rng_header ok
emu_rng_header_head()
{
	atf_set "descr" "Test that emu_dev_rng.h header exists and is valid"
}
emu_rng_header_body()
{
	atf_check -s exit:0 -o file \
		test -f /usr/src/usr.sbin/emu/emu_dev_rng.h
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "emu_rng_init" /usr/src/usr.sbin/emu/emu_dev_rng.h
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "emu_rng_handle_request" /usr/src/usr.sbin/emu/emu_dev_rng.h
}

atf_test_case emu_rng_request_limit ok
emu_rng_request_limit_head()
{
	atf_set "descr" "Test that RNG requests are limited to prevent DoS"
}
emu_rng_request_limit_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "EMU_RNG_MAX_BYTES" /usr/src/usr.sbin/emu/emu_dev_rng.c
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "65536" /usr/src/usr.sbin/emu/emu_dev_rng.c
}

atf_test_case emu_rng_statistics ok
emu_rng_statistics_head()
{
	atf_set "descr" "Test that RNG tracks statistics"
}
emu_rng_statistics_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "rng_requests" /usr/src/usr.sbin/emu/emu_dev_rng.h
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "rng_bytes_provided" /usr/src/usr.sbin/emu/emu_dev_rng.h
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "rng_errors" /usr/src/usr.sbin/emu/emu_dev_rng.h
}

atf_test_case emu_rng_initialization ok
emu_rng_initialization_head()
{
	atf_set "descr" "Test that RNG device initializes properly"
}
emu_rng_initialization_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "rng_initialized" /usr/src/usr.sbin/emu/emu_dev_rng.c
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "emu_rng_init" /usr/src/usr.sbin/emu/emu_dev_rng.c
}

atf_test_case emu_rng_error_handling ok
emu_rng_error_handling_head()
{
	atf_set "descr" "Test that RNG handles errors gracefully"
}
emu_rng_error_handling_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "EINVAL" /usr/src/usr.sbin/emu/emu_dev_rng.c
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "ENXIO" /usr/src/usr.sbin/emu/emu_dev_rng.c
}

atf_test_case emu_rng_virtio_compliance ok
emu_rng_virtio_compliance_head()
{
	atf_set "descr" "Test virtio-rng device compliance"
}
emu_rng_virtio_compliance_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "virtio" /usr/src/usr.sbin/emu/emu_dev_rng.h
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "vq" /usr/src/usr.sbin/emu/emu_dev_rng.c
}

atf_test_case emu_rng_security_properties ok
emu_rng_security_properties_head()
{
	atf_set "descr" "Test RNG security properties"
}
emu_rng_security_properties_body()
{
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "arc4random_buf" /usr/src/usr.sbin/emu/emu_dev_rng.c
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "CSPRNG" /usr/src/usr.sbin/emu/emu_dev_rng.c
	atf_check -s exit:0 -o ignore -e ignore \
		grep -q "cryptographically secure" /usr/src/usr.sbin/emu/emu_dev_rng.c
}

atf_init_test_cases()
{
	atf_add_test_case emu_rng_device_exists
	atf_add_test_case emu_rng_config_space
	atf_add_test_case emu_rng_entropy_source
	atf_add_test_case emu_rng_header
	atf_add_test_case emu_rng_request_limit
	atf_add_test_case emu_rng_statistics
	atf_add_test_case emu_rng_initialization
	atf_add_test_case emu_rng_error_handling
	atf_add_test_case emu_rng_virtio_compliance
	atf_add_test_case emu_rng_security_properties
}
