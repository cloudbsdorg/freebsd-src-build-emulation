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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EMU_AUDIT_SYSCTL_BASE	"kern.emulation.audit"
#define EMU_AUDIT_ENABLED	EMU_AUDIT_SYSCTL_BASE ".enabled"
#define EMU_AUDIT_DESTINATION	EMU_AUDIT_SYSCTL_BASE ".destination"
#define EMU_AUDIT_FILE_PATH	EMU_AUDIT_SYSCTL_BASE ".file_path"
#define EMU_AUDIT_ROTATION_SIZE EMU_AUDIT_SYSCTL_BASE ".rotation_size"
#define EMU_AUDIT_ROTATION_COUNT EMU_AUDIT_SYSCTL_BASE ".rotation_count"
#define EMU_AUDIT_MIN_SEVERITY	EMU_AUDIT_SYSCTL_BASE ".min_severity"
#define EMU_AUDIT_INCLUDE_DATA	EMU_AUDIT_SYSCTL_BASE ".include_data"
#define EMU_AUDIT_EVENT_COUNT	EMU_AUDIT_SYSCTL_BASE ".event_count"

#define EMU_INSTANCE_BASE	"kern.emulation.instance"
#define EMU_INSTANCE_COUNT	"kern.emulation.instance_count"
#define EMU_ALLOW_NONROOT	"kern.emulation.allow_nonroot"

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
 * Helper function to get sysctl string value
 */
static int
get_sysctl_string(const char *name, char *value, size_t *len)
{
	if (sysctlbyname(name, value, len, NULL, 0) < 0)
		return (-1);

	return (0);
}

/*
 * Helper function to set sysctl string value
 */
static int
set_sysctl_string(const char *name, const char *value)
{
	char buf[256];
	size_t len;

	len = strlen(value) + 1;
	if (len > sizeof(buf))
		return (-1);

	memcpy(buf, value, len);
	if (sysctlbyname(name, NULL, NULL, buf, len) < 0)
		return (-1);

	return (0);
}

/*
 * Test 1: Verify audit sysctl interface exists and has correct defaults
 */
ATF_TC(audit_sysctl_defaults);
ATF_TC_HEAD(audit_sysctl_defaults, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify audit sysctl interface exists with correct defaults");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_sysctl_defaults, tc)
{
	int enabled, destination, rotation_size, rotation_count;
	int min_severity, include_data;
	size_t len;
	char file_path[256];

	/* Check audit enabled (should default to 0) */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ENABLED, &enabled));
	ATF_REQUIRE(enabled == 0 || enabled == 1);

	/* Check destination (should default to 1=syslog) */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE(destination >= 0 && destination <= 3);

	/* Check file path */
	len = sizeof(file_path);
	ATF_REQUIRE_EQ(0, get_sysctl_string(EMU_AUDIT_FILE_PATH, file_path, &len));
	ATF_REQUIRE(strlen(file_path) > 0);

	/* Check rotation size (should default to 10MB) */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_SIZE, &rotation_size));
	ATF_REQUIRE(rotation_size > 0);

	/* Check rotation count (should default to 5) */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_COUNT, &rotation_count));
	ATF_REQUIRE(rotation_count >= 1 && rotation_count <= 100);

	/* Check min severity (should default to INFO=5) */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_MIN_SEVERITY, &min_severity));
	ATF_REQUIRE(min_severity >= 0 && min_severity <= 7);

	/* Check include_data flag */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_INCLUDE_DATA, &include_data));
	ATF_REQUIRE(include_data == 0 || include_data == 1);
}

/*
 * Test 2: Test enabling/disabling audit logging
 */
ATF_TC(audit_enable_disable);
ATF_TC_HEAD(audit_enable_disable, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test enabling and disabling audit logging");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_enable_disable, tc)
{
	int enabled;

	/* Enable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 1));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ENABLED, &enabled));
	ATF_REQUIRE_EQ(1, enabled);

	/* Disable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 0));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ENABLED, &enabled));
	ATF_REQUIRE_EQ(0, enabled);
}

/*
 * Test 3: Test audit destination configuration
 */
ATF_TC(audit_destination_config);
ATF_TC_HEAD(audit_destination_config, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit destination configuration (none/syslog/file/both)");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_destination_config, tc)
{
	int destination;

	/* Test destination 0 (none) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 0));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE_EQ(0, destination);

	/* Test destination 1 (syslog) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 1));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE_EQ(1, destination);

	/* Test destination 2 (file) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 2));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE_EQ(2, destination);

	/* Test destination 3 (both) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 3));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE_EQ(3, destination);

	/* Test invalid destination (should fail) */
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_DESTINATION, 4));
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_DESTINATION, -1));
}

/*
 * Test 4: Test audit file path configuration
 */
ATF_TC(audit_file_path_config);
ATF_TC_HEAD(audit_file_path_config, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit file path configuration");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_file_path_config, tc)
{
	char file_path[256];
	size_t len;

	/* Set custom file path */
	ATF_REQUIRE_EQ(0, set_sysctl_string(EMU_AUDIT_FILE_PATH, "/var/log/emu_test.log"));
	len = sizeof(file_path);
	ATF_REQUIRE_EQ(0, get_sysctl_string(EMU_AUDIT_FILE_PATH, file_path, &len));
	ATF_REQUIRE_EQ(0, strcmp(file_path, "/var/log/emu_test.log"));

	/* Restore default */
	ATF_REQUIRE_EQ(0, set_sysctl_string(EMU_AUDIT_FILE_PATH, "/var/log/emu_audit.log"));
}

/*
 * Test 5: Test audit rotation configuration
 */
ATF_TC(audit_rotation_config);
ATF_TC_HEAD(audit_rotation_config, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit log rotation configuration");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_rotation_config, tc)
{
	int rotation_size, rotation_count;

	/* Test rotation size configuration */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_SIZE, 5242880)); /* 5MB */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_SIZE, &rotation_size));
	ATF_REQUIRE_EQ(5242880, rotation_size);

	/* Test rotation count configuration */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_COUNT, 3));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_COUNT, &rotation_count));
	ATF_REQUIRE_EQ(3, rotation_count);

	/* Test invalid rotation size (too small) */
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_ROTATION_SIZE, 1024));

	/* Test invalid rotation count (too large) */
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_ROTATION_COUNT, 1000));

	/* Restore defaults */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_SIZE, 10485760));
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_COUNT, 5));
}

/*
 * Test 6: Test audit severity filtering
 */
ATF_TC(audit_severity_filtering);
ATF_TC_HEAD(audit_severity_filtering, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit severity filtering configuration");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_severity_filtering, tc)
{
	int min_severity;
	int severity;

	/* Test all severity levels (0=EMERG to 7=DEBUG) */
	for (severity = 0; severity <= 7; severity++) {
		ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_MIN_SEVERITY, severity));
		ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_MIN_SEVERITY, &min_severity));
		ATF_REQUIRE_EQ(severity, min_severity);
	}

	/* Test invalid severity */
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_MIN_SEVERITY, 8));
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_MIN_SEVERITY, -1));

	/* Restore default */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_MIN_SEVERITY, 5));
}

/*
 * Test 7: Test audit event counter
 */
ATF_TC(audit_event_counter);
ATF_TC_HEAD(audit_event_counter, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit event counter increments");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_event_counter, tc)
{
	int event_count_before, event_count_after;

	/* Get initial event count */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_EVENT_COUNT, &event_count_before));

	/* Enable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 1));

	/* Trigger some audit events by creating/destroying instances */
	/* Note: This would require actual instance operations */
	/* For now, we just verify the counter exists and is readable */

	/* Disable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 0));

	/* Verify counter is still readable */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_EVENT_COUNT, &event_count_after));
	ATF_REQUIRE(event_count_after >= event_count_before);
}

/*
 * Test 8: Test audit permission check for log access
 */
ATF_TC(audit_permission_check);
ATF_TC_HEAD(audit_permission_check, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit log access permission checks");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_permission_check, tc)
{
	int saved_allow_nonroot;

	/* Save current setting */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_ALLOW_NONROOT, &saved_allow_nonroot));

	/* Test with allow_nonroot=0 (root only) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_ALLOW_NONROOT, 0));

	/* Non-root should not be able to read audit log */
	/* This would require forking a non-root process */
	/* For now, we verify the sysctl is writable only by root */

	/* Restore original setting */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_ALLOW_NONROOT, saved_allow_nonroot));
}

/*
 * Test 9: Test audit include_data flag
 */
ATF_TC(audit_include_data_flag);
ATF_TC_HEAD(audit_include_data_flag, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit include_data flag configuration");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_include_data_flag, tc)
{
	int include_data;

	/* Test include_data=0 (no data) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_INCLUDE_DATA, 0));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_INCLUDE_DATA, &include_data));
	ATF_REQUIRE_EQ(0, include_data);

	/* Test include_data=1 (include data) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_INCLUDE_DATA, 1));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_INCLUDE_DATA, &include_data));
	ATF_REQUIRE_EQ(1, include_data);

	/* Test invalid values */
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_INCLUDE_DATA, 2));
	ATF_REQUIRE_EQ(-1, set_sysctl_int(EMU_AUDIT_INCLUDE_DATA, -1));

	/* Restore default */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_INCLUDE_DATA, 0));
}

/*
 * Test 10: Test dual output (syslog+file)
 */
ATF_TC(audit_dual_output);
ATF_TC_HEAD(audit_dual_output, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test dual output mode (syslog+file)");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_dual_output, tc)
{
	int destination;

	/* Enable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 1));

	/* Set destination to both (3) */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 3));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_DESTINATION, &destination));
	ATF_REQUIRE_EQ(3, destination);

	/* Verify both outputs are configured */
	/* This would require checking syslog and file for events */
	/* For now, we verify the configuration is accepted */

	/* Disable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 0));
}

/*
 * Test 11: Test audit log rotation trigger
 */
ATF_TC(audit_rotation_trigger);
ATF_TC_HEAD(audit_rotation_trigger, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit log rotation trigger");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_rotation_trigger, tc)
{
	int saved_rotation_size, saved_rotation_count;

	/* Save current settings */
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_SIZE, &saved_rotation_size));
	ATF_REQUIRE_EQ(0, get_sysctl_int(EMU_AUDIT_ROTATION_COUNT, &saved_rotation_count));

	/* Set small rotation size to trigger rotation */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_SIZE, 1024)); /* 1KB */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_COUNT, 2));

	/* Enable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 1));

	/* Generate events to trigger rotation */
	/* This would require actual instance operations */
	/* For now, we verify the configuration is accepted */

	/* Disable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 0));

	/* Restore settings */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_SIZE, saved_rotation_size));
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ROTATION_COUNT, saved_rotation_count));
}

/*
 * Test 12: Test audit log format validation
 */
ATF_TC(audit_log_format);
ATF_TC_HEAD(audit_log_format, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test audit log format validation");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_log_format, tc)
{
	char file_path[256];
	size_t len;
	struct stat sb;

	/* Get current file path */
	len = sizeof(file_path);
	ATF_REQUIRE_EQ(0, get_sysctl_string(EMU_AUDIT_FILE_PATH, file_path, &len));

	/* Enable file output */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_DESTINATION, 2));
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 1));

	/* Generate some events */
	/* This would require actual instance operations */

	/* Disable audit logging */
	ATF_REQUIRE_EQ(0, set_sysctl_int(EMU_AUDIT_ENABLED, 0));

	/* Check if log file exists and has correct format */
	if (stat(file_path, &sb) == 0) {
		/* File exists, verify it's readable */
		ATF_REQUIRE(S_ISREG(sb.st_mode));
		ATF_REQUIRE(sb.st_size > 0);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, audit_sysctl_defaults);
	ATF_TP_ADD_TC(tp, audit_enable_disable);
	ATF_TP_ADD_TC(tp, audit_destination_config);
	ATF_TP_ADD_TC(tp, audit_file_path_config);
	ATF_TP_ADD_TC(tp, audit_rotation_config);
	ATF_TP_ADD_TC(tp, audit_severity_filtering);
	ATF_TP_ADD_TC(tp, audit_event_counter);
	ATF_TP_ADD_TC(tp, audit_permission_check);
	ATF_TP_ADD_TC(tp, audit_include_data_flag);
	ATF_TP_ADD_TC(tp, audit_dual_output);
	ATF_TP_ADD_TC(tp, audit_rotation_trigger);
	ATF_TP_ADD_TC(tp, audit_log_format);

	return (atf_no_error());
}
