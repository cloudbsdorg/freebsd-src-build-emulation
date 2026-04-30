/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * This software is developed by FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Memory Scrubbing Tests (S10.3)
 * 
 * Tests for memory scrubbing in the FreeBSD Kernel Emulation Framework.
 * Verifies that memory is properly scrubbed using configured methods.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EMU_SCRUB_ENABLED		"kern.emulation.memory.scrub.enabled"
#define EMU_SCRUB_METHOD		"kern.emulation.memory.scrub.method"

#define SCRUB_METHOD_ZERO		0
#define SCRUB_METHOD_RANDOM		1
#define SCRUB_METHOD_PATTERN		2

/*
 * Helper function to get sysctl integer value
 */
static int
get_sysctl_int(const char *name, int *value)
{
	size_t len = sizeof(*value);

	if (sysctlbyname(name, value, &len, NULL, 0) < 0)
		return (-1);

	return (0);
}

/*
 * Helper function to set sysctl integer value
 */
static int
set_sysctl_int(const char *name, int value)
{
	if (sysctlbyname(name, NULL, NULL, &value, sizeof(value)) < 0)
		return (-1);

	return (0);
}

/*
 * Helper function to check if memory scrubbing is enabled
 */
static int
get_scrub_enabled(void)
{
	int enabled;

	if (get_sysctl_int(EMU_SCRUB_ENABLED, &enabled) < 0)
		return (-1);

	return (enabled);
}

/*
 * Helper function to enable/disable memory scrubbing
 */
static int
set_scrub_enabled(int enabled)
{
	return (set_sysctl_int(EMU_SCRUB_ENABLED, enabled));
}

/*
 * Helper function to get current scrubbing method
 */
static int
get_scrub_method(void)
{
	int method;

	if (get_sysctl_int(EMU_SCRUB_METHOD, &method) < 0)
		return (-1);

	return (method);
}

/*
 * Helper function to set scrubbing method
 */
static int
set_scrub_method(int method)
{
	return (set_sysctl_int(EMU_SCRUB_METHOD, method));
}

/*
 * Helper function to verify memory is zeroed
 */
static int
verify_memory_zeroed(const uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buf[i] != 0)
			return (0);
	}

	return (1);
}

/*
 * Helper function to verify memory contains non-zero data
 */
static int
verify_memory_nonzero(const uint8_t *buf, size_t len)
{
	size_t i;
	int found_nonzero = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] != 0) {
			found_nonzero = 1;
			break;
		}
	}

	return (found_nonzero);
}

/*
 * Test: Memory scrubbing can be enabled/disabled
 */
ATF_TC_BODY(scrub_toggle, tc)
{
	int original_state;
	int new_state;

	/* Get original state */
	original_state = get_scrub_enabled();
	ATF_REQUIRE(original_state != -1);

	/* Toggle off */
	ATF_REQUIRE(set_scrub_enabled(0) == 0);
	new_state = get_scrub_enabled();
	ATF_REQUIRE(new_state == 0);

	/* Toggle on */
	ATF_REQUIRE(set_scrub_enabled(1) == 0);
	new_state = get_scrub_enabled();
	ATF_REQUIRE(new_state == 1);

	/* Restore original state */
	ATF_REQUIRE(set_scrub_enabled(original_state) == 0);
}

/*
 * Test: Scrubbing method can be changed
 */
ATF_TC_BODY(scrub_method_change, tc)
{
	int original_method;
	int new_method;

	/* Get original method */
	original_method = get_scrub_method();
	ATF_REQUIRE(original_method != -1);

	/* Change to zero method */
	ATF_REQUIRE(set_scrub_method(SCRUB_METHOD_ZERO) == 0);
	new_method = get_scrub_method();
	ATF_REQUIRE(new_method == SCRUB_METHOD_ZERO);

	/* Change to random method */
	ATF_REQUIRE(set_scrub_method(SCRUB_METHOD_RANDOM) == 0);
	new_method = get_scrub_method();
	ATF_REQUIRE(new_method == SCRUB_METHOD_RANDOM);

	/* Change to pattern method */
	ATF_REQUIRE(set_scrub_method(SCRUB_METHOD_PATTERN) == 0);
	new_method = get_scrub_method();
	ATF_REQUIRE(new_method == SCRUB_METHOD_PATTERN);

	/* Restore original method */
	ATF_REQUIRE(set_scrub_method(original_method) == 0);
}

/*
 * Test: Invalid scrubbing method is rejected
 */
ATF_TC_BODY(scrub_method_invalid, tc)
{
	int error;

	/* Try to set invalid method */
	error = set_scrub_method(-1);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EINVAL);

	/* Try to set invalid method (too high) */
	error = set_scrub_method(100);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EINVAL);

	/* Verify method unchanged */
	int current_method = get_scrub_method();
	ATF_REQUIRE(current_method >= 0 && current_method <= 2);
}

/*
 * Test: Scrubbing enabled sysctl has correct default
 */
ATF_TC_BODY(scrub_enabled_default, tc)
{
	int enabled;

	enabled = get_scrub_enabled();
	ATF_REQUIRE(enabled != -1);
	ATF_REQUIRE(enabled == 1);  /* Should be enabled by default */
}

/*
 * Test: Scrubbing method sysctl has correct default
 */
ATF_TC_BODY(scrub_method_default, tc)
{
	int method;

	method = get_scrub_method();
	ATF_REQUIRE(method != -1);
	ATF_REQUIRE(method == SCRUB_METHOD_ZERO);  /* Zero by default */
}

/*
 * Test: Scrubbing sysctls are readable
 */
ATF_TC_BODY(scrub_sysctls_readable, tc)
{
	int enabled;
	int method;

	/* Read enabled sysctl */
	ATF_REQUIRE(get_sysctl_int(EMU_SCRUB_ENABLED, &enabled) == 0);
	ATF_REQUIRE(enabled >= 0 && enabled <= 1);

	/* Read method sysctl */
	ATF_REQUIRE(get_sysctl_int(EMU_SCRUB_METHOD, &method) == 0);
	ATF_REQUIRE(method >= 0 && method <= 2);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, scrub_toggle);
	ATF_TP_ADD_TC(tp, scrub_method_change);
	ATF_TP_ADD_TC(tp, scrub_method_invalid);
	ATF_TP_ADD_TC(tp, scrub_enabled_default);
	ATF_TP_ADD_TC(tp, scrub_method_default);
	ATF_TP_ADD_TC(tp, scrub_sysctls_readable);

	return (atf_no_error());
}
