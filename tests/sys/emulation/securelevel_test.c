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
 * Securelevel Integration Tests (S9.3)
 * 
 * Tests for securelevel-based restrictions in the FreeBSD Kernel Emulation Framework.
 * Verifies that operations are properly restricted based on system securelevel.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "emu_sysctl_paths.h"

#define EMU_SECURELEVEL_RESTRICTIONS	"kern.emulation.securelevel_restrictions"
#define EMU_INSTANCE_COUNT		"kern.emulation.instance_count"
#define EMU_MAX_INSTANCES		"kern.emulation.max_instances"
#define EMU_MAX_INSTANCES_PER_USER	"kern.emulation.max_instances_per_user"
#define EMU_MAX_MEMORY_PER_INSTANCE	"kern.emulation.max_memory_per_instance"
#define EMU_MAX_CPU_TIME_PER_INSTANCE	"kern.emulation.max_cpu_time_per_instance"
#define EMU_MEM_POLICY			"kern.emulation.memory.policy"
#define EMU_MEM_OVERCOMMIT		"kern.emulation.memory.overcommit"
#define EMU_ALLOW_NONROOT		"kern.emulation.allow_nonroot"

/*
 * Helper function to get securelevel
 */
static int
get_securelevel(void)
{
	int securelevel;
	size_t len = sizeof(securelevel);

	if (sysctlbyname("kern.securelevel", &securelevel, &len, NULL, 0) < 0)
		return (-1);

	return (securelevel);
}

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
 * Helper function to enable/disable securelevel restrictions
 */
static int
set_securelevel_restrictions(int enabled)
{
	return (set_sysctl_int(EMU_SECURELEVEL_RESTRICTIONS, enabled));
}

/*
 * Helper function to check if securelevel restrictions are enabled
 */
static int
get_securelevel_restrictions(void)
{
	int enabled;

	if (get_sysctl_int(EMU_SECURELEVEL_RESTRICTIONS, &enabled) < 0)
		return (-1);

	return (enabled);
}

/*
 * Test: Securelevel restrictions can be enabled/disabled
 */
ATF_TC_BODY(securelevel_toggle, tc)
{
	int original_state;
	int new_state;

	/* Get original state */
	original_state = get_securelevel_restrictions();
	ATF_REQUIRE(original_state != -1);

	/* Toggle off */
	ATF_REQUIRE(set_securelevel_restrictions(0) == 0);
	new_state = get_securelevel_restrictions();
	ATF_REQUIRE(new_state == 0);

	/* Toggle on */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);
	new_state = get_securelevel_restrictions();
	ATF_REQUIRE(new_state == 1);

	/* Restore original state */
	ATF_REQUIRE(set_securelevel_restrictions(original_state) == 0);
}

/*
 * Test: Sysctl write operations allowed at securelevel 0
 */
ATF_TC_BODY(securelevel_0_sysctl_write, tc)
{
	int securelevel;
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Get current securelevel */
	securelevel = get_securelevel();

	/* Skip if securelevel > 0 */
	if (securelevel > 0)
		atf_tc_skip("Test requires securelevel 0");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original max_instances value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES, &original_value) == 0);

	/* Try to change max_instances - should succeed at securelevel 0 */
	test_value = original_value + 1;
	error = set_sysctl_int(EMU_MAX_INSTANCES, test_value);
	ATF_REQUIRE(error == 0);

	/* Verify the change */
	int new_value;
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES, &new_value) == 0);
	ATF_REQUIRE(new_value == test_value);

	/* Restore original value */
	ATF_REQUIRE(set_sysctl_int(EMU_MAX_INSTANCES, original_value) == 0);
}

/*
 * Test: Sysctl write operations blocked at securelevel 1
 */
ATF_TC_BODY(securelevel_1_sysctl_write, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original max_instances value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES, &original_value) == 0);

	/* Try to change max_instances - should fail at securelevel 1 */
	test_value = original_value + 1;
	error = set_sysctl_int(EMU_MAX_INSTANCES, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Memory policy sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_memory_policy, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original memory policy value */
	ATF_REQUIRE(get_sysctl_int(EMU_MEM_POLICY, &original_value) == 0);

	/* Try to change memory policy - should fail at securelevel >= 1 */
	test_value = (original_value == 0) ? 1 : 0;
	error = set_sysctl_int(EMU_MEM_POLICY, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Memory overcommit sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_memory_overcommit, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original memory overcommit value */
	ATF_REQUIRE(get_sysctl_int(EMU_MEM_OVERCOMMIT, &original_value) == 0);

	/* Try to change memory overcommit - should fail at securelevel >= 1 */
	test_value = (original_value == 0) ? 1 : 0;
	error = set_sysctl_int(EMU_MEM_OVERCOMMIT, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Max instances per user sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_max_instances_per_user, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original max_instances_per_user value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES_PER_USER, &original_value) == 0);

	/* Try to change max_instances_per_user - should fail at securelevel >= 1 */
	test_value = original_value + 1;
	error = set_sysctl_int(EMU_MAX_INSTANCES_PER_USER, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Max memory per instance sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_max_memory_per_instance, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original max_memory_per_instance value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_MEMORY_PER_INSTANCE, &original_value) == 0);

	/* Try to change max_memory_per_instance - should fail at securelevel >= 1 */
	test_value = original_value + (128 * 1024 * 1024); /* Add 128MB */
	error = set_sysctl_int(EMU_MAX_MEMORY_PER_INSTANCE, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Max CPU time per instance sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_max_cpu_time_per_instance, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original max_cpu_time_per_instance value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_CPU_TIME_PER_INSTANCE, &original_value) == 0);

	/* Try to change max_cpu_time_per_instance - should fail at securelevel >= 1 */
	test_value = original_value + 60; /* Add 60 seconds */
	error = set_sysctl_int(EMU_MAX_CPU_TIME_PER_INSTANCE, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Allow nonroot sysctl blocked at elevated securelevel
 */
ATF_TC_BODY(securelevel_allow_nonroot, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Ensure securelevel restrictions are enabled */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);

	/* Get original allow_nonroot value */
	ATF_REQUIRE(get_sysctl_int(EMU_ALLOW_NONROOT, &original_value) == 0);

	/* Try to change allow_nonroot - should fail at securelevel >= 1 */
	test_value = (original_value == 0) ? 1 : 0;
	error = set_sysctl_int(EMU_ALLOW_NONROOT, test_value);
	ATF_REQUIRE(error == -1);
	ATF_REQUIRE(errno == EPERM);
}

/*
 * Test: Securelevel restrictions can be disabled to allow changes
 */
ATF_TC_BODY(securelevel_disable_allows_changes, tc)
{
	int original_value;
	int test_value;
	int error;

	/* Skip if not root */
	if (geteuid() != 0)
		atf_tc_skip("Test requires root privileges");

	/* Skip if securelevel < 1 */
	if (get_securelevel() < 1)
		atf_tc_skip("Test requires securelevel >= 1");

	/* Disable securelevel restrictions */
	ATF_REQUIRE(set_securelevel_restrictions(0) == 0);

	/* Get original max_instances value */
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES, &original_value) == 0);

	/* Try to change max_instances - should succeed when restrictions disabled */
	test_value = original_value + 1;
	error = set_sysctl_int(EMU_MAX_INSTANCES, test_value);
	ATF_REQUIRE(error == 0);

	/* Verify the change */
	int new_value;
	ATF_REQUIRE(get_sysctl_int(EMU_MAX_INSTANCES, &new_value) == 0);
	ATF_REQUIRE(new_value == test_value);

	/* Restore original value */
	ATF_REQUIRE(set_sysctl_int(EMU_MAX_INSTANCES, original_value) == 0);

	/* Re-enable securelevel restrictions */
	ATF_REQUIRE(set_securelevel_restrictions(1) == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, securelevel_toggle);
	ATF_TP_ADD_TC(tp, securelevel_0_sysctl_write);
	ATF_TP_ADD_TC(tp, securelevel_1_sysctl_write);
	ATF_TP_ADD_TC(tp, securelevel_memory_policy);
	ATF_TP_ADD_TC(tp, securelevel_memory_overcommit);
	ATF_TP_ADD_TC(tp, securelevel_max_instances_per_user);
	ATF_TP_ADD_TC(tp, securelevel_max_memory_per_instance);
	ATF_TP_ADD_TC(tp, securelevel_max_cpu_time_per_instance);
	ATF_TP_ADD_TC(tp, securelevel_allow_nonroot);
	ATF_TP_ADD_TC(tp, securelevel_disable_allows_changes);

	return (atf_no_error());
}
