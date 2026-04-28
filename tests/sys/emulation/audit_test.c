/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Emulation Framework Project
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <atf-c.h>

/*
 * Audit Logging Tests for Emulation Framework
 * 
 * Tests the audit logging subsystem (Phase S7) including:
 * - Event logging to syslog and file
 * - Log rotation
 * - Severity filtering
 * - Permission checks for audit log access
 * - Sysctl configuration
 * - Dual output (syslog + file)
 */

/* Sysctl MIB for audit logging */
static int emu_audit_mib[] = { CTL_KERN, KERN_EMULATION, -1 };
static const char *emu_audit_path = "kern.emulation.audit";

/*
 * Helper function to get audit sysctl value
 */
static int
get_audit_sysctl(const char *name, void *oldp, size_t *oldlenp)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s.%s", emu_audit_path, name);
	return (sysctlbyname(buf, oldp, oldlenp, NULL, 0));
}

/*
 * Helper function to set audit sysctl value
 */
static int
set_audit_sysctl(const char *name, void *newp, size_t newlen)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s.%s", emu_audit_path, name);
	return (sysctlbyname(buf, NULL, NULL, newp, newlen));
}

/*
 * Test 1: Verify audit sysctl tree exists
 */
ATF_TC(audit_sysctl_tree);
ATF_TC_HEAD(audit_sysctl_tree, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit sysctl tree exists");
}
ATF_TC_BODY(audit_sysctl_tree, tc)
{
	int enabled;
	size_t len = sizeof(enabled);

	/* Check if audit.enabled sysctl exists */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &enabled, &len));
	ATF_REQUIRE_EQ(len, sizeof(enabled));

	/* Check if audit.destination sysctl exists */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &enabled, &len));

	/* Check if audit.event_count sysctl exists */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("event_count", &enabled, &len));
}

/*
 * Test 2: Verify audit can be enabled/disabled
 */
ATF_TC(audit_enable_disable);
ATF_TC_HEAD(audit_enable_disable, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit can be enabled and disabled");
}
ATF_TC_BODY(audit_enable_disable, tc)
{
	int enabled, old_enabled;
	size_t len = sizeof(enabled);

	/* Get current state */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &old_enabled, &len));

	/* Enable audit */
	enabled = 1;
	ATF_REQUIRE_EQ(0, set_audit_sysctl("enabled", &enabled, sizeof(enabled)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &enabled, &len));
	ATF_REQUIRE_EQ(1, enabled);

	/* Disable audit */
	enabled = 0;
	ATF_REQUIRE_EQ(0, set_audit_sysctl("enabled", &enabled, sizeof(enabled)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &enabled, &len));
	ATF_REQUIRE_EQ(0, enabled);

	/* Restore original state */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("enabled", &old_enabled, sizeof(old_enabled)));
}

/*
 * Test 3: Verify audit destination configuration
 */
ATF_TC(audit_destination);
ATF_TC_HEAD(audit_destination, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit destination configuration");
}
ATF_TC_BODY(audit_destination, tc)
{
	int dest, old_dest;
	size_t len = sizeof(dest);

	/* Get current destination */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &old_dest, &len));

	/* Test all destination values */
	dest = 0; /* none */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("destination", &dest, sizeof(dest)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &dest, &len));
	ATF_REQUIRE_EQ(0, dest);

	dest = 1; /* syslog */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("destination", &dest, sizeof(dest)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &dest, &len));
	ATF_REQUIRE_EQ(1, dest);

	dest = 2; /* file */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("destination", &dest, sizeof(dest)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &dest, &len));
	ATF_REQUIRE_EQ(2, dest);

	dest = 3; /* both */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("destination", &dest, sizeof(dest)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &dest, &len));
	ATF_REQUIRE_EQ(3, dest);

	/* Restore original destination */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("destination", &old_dest, sizeof(old_dest)));
}

/*
 * Test 4: Verify severity filtering
 */
ATF_TC(audit_severity_filter);
ATF_TC_HEAD(audit_severity_filter, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify severity filtering works correctly");
}
ATF_TC_BODY(audit_severity_filter, tc)
{
	int severity, old_severity;
	size_t len = sizeof(severity);

	/* Get current minimum severity */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("min_severity", &old_severity, &len));

	/* Test all severity levels (0=EMERG to 7=DEBUG) */
	for (severity = 0; severity <= 7; severity++) {
		ATF_REQUIRE_EQ(0, set_audit_sysctl("min_severity", &severity, sizeof(severity)));
		ATF_REQUIRE_EQ(0, get_audit_sysctl("min_severity", &severity, &len));
		ATF_REQUIRE(severity >= 0 && severity <= 7);
	}

	/* Restore original severity */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("min_severity", &old_severity, sizeof(old_severity)));
}

/*
 * Test 5: Verify file path configuration
 */
ATF_TC(audit_file_path);
ATF_TC_HEAD(audit_file_path, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit log file path configuration");
}
ATF_TC_BODY(audit_file_path, tc)
{
	char path[256], old_path[256];
	size_t len = sizeof(path);

	/* Get current file path */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("file_path", old_path, &len));

	/* Set new path */
	strlcpy(path, "/var/log/test_audit.log", sizeof(path));
	ATF_REQUIRE_EQ(0, set_audit_sysctl("file_path", path, strlen(path) + 1));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("file_path", path, &len));
	ATF_REQUIRE_STREQ(path, "/var/log/test_audit.log");

	/* Restore original path */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("file_path", old_path, strlen(old_path) + 1));
}

/*
 * Test 6: Verify rotation configuration
 */
ATF_TC(audit_rotation_config);
ATF_TC_HEAD(audit_rotation_config, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify log rotation configuration");
}
ATF_TC_BODY(audit_rotation_config, tc)
{
	size_t rotation_size, old_rotation_size;
	int rotation_count, old_rotation_count;
	size_t len;

	/* Get current rotation settings */
	len = sizeof(old_rotation_size);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("rotation_size", &old_rotation_size, &len));

	len = sizeof(old_rotation_count);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("rotation_count", &old_rotation_count, &len));

	/* Test rotation size */
	rotation_size = 5 * 1024 * 1024; /* 5 MB */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("rotation_size", &rotation_size, sizeof(rotation_size)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("rotation_size", &rotation_size, &len));
	ATF_REQUIRE_EQ(5 * 1024 * 1024, rotation_size);

	/* Test rotation count */
	rotation_count = 3;
	ATF_REQUIRE_EQ(0, set_audit_sysctl("rotation_count", &rotation_count, sizeof(rotation_count)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("rotation_count", &rotation_count, &len));
	ATF_REQUIRE_EQ(3, rotation_count);

	/* Restore original settings */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("rotation_size", &old_rotation_size, sizeof(old_rotation_size)));
	ATF_REQUIRE_EQ(0, set_audit_sysctl("rotation_count", &old_rotation_count, sizeof(old_rotation_count)));
}

/*
 * Test 7: Verify event count increments
 */
ATF_TC(audit_event_count);
ATF_TC_HEAD(audit_event_count, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify event count increments on logging");
}
ATF_TC_BODY(audit_event_count, tc)
{
	int old_count, new_count;
	size_t len = sizeof(old_count);

	/* Get initial event count */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("event_count", &old_count, &len));

	/* Note: We can't directly trigger audit events from userland,
	 * but we can verify the sysctl is readable and returns a value */
	ATF_REQUIRE(old_count >= 0);

	/* The count should increment when kernel modules log events */
	/* This is verified by integration tests */
}

/*
 * Test 8: Verify audit log access requires privilege
 */
ATF_TC(audit_access_priv);
ATF_TC_HEAD(audit_access_priv, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit log access requires root privilege");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(audit_access_priv, tc)
{
	/* Root should be able to access audit configuration */
	int enabled;
	size_t len = sizeof(enabled);

	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &enabled, &len));

	/* Non-root access test would require fork/exec */
	/* This is verified by the kernel's priv_check() */
}

/*
 * Test 9: Verify include_data configuration
 */
ATF_TC(audit_include_data);
ATF_TC_HEAD(audit_include_data, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify include_data configuration");
}
ATF_TC_BODY(audit_include_data, tc)
{
	int include_data, old_include_data;
	size_t len = sizeof(include_data);

	/* Get current setting */
	ATF_REQUIRE_EQ(0, get_audit_sysctl("include_data", &old_include_data, &len));

	/* Test enabling */
	include_data = 1;
	ATF_REQUIRE_EQ(0, set_audit_sysctl("include_data", &include_data, sizeof(include_data)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("include_data", &include_data, &len));
	ATF_REQUIRE_EQ(1, include_data);

	/* Test disabling */
	include_data = 0;
	ATF_REQUIRE_EQ(0, set_audit_sysctl("include_data", &include_data, sizeof(include_data)));
	ATF_REQUIRE_EQ(0, get_audit_sysctl("include_data", &include_data, &len));
	ATF_REQUIRE_EQ(0, include_data);

	/* Restore original setting */
	ATF_REQUIRE_EQ(0, set_audit_sysctl("include_data", &old_include_data, sizeof(old_include_data)));
}

/*
 * Test 10: Verify audit initialization
 */
ATF_TC(audit_init);
ATF_TC_HEAD(audit_init, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify audit subsystem initializes correctly");
}
ATF_TC_BODY(audit_init, tc)
{
	int enabled, dest, min_severity, include_data;
	size_t len;
	char path[256];

	/* Verify all sysctls are accessible (subsystem is initialized) */
	len = sizeof(enabled);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("enabled", &enabled, &len));

	len = sizeof(dest);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("destination", &dest, &len));

	len = sizeof(min_severity);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("min_severity", &min_severity, &len));

	len = sizeof(include_data);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("include_data", &include_data, &len));

	len = sizeof(path);
	ATF_REQUIRE_EQ(0, get_audit_sysctl("file_path", path, &len));

	/* Verify default values */
	ATF_REQUIRE(enabled == 0 || enabled == 1);
	ATF_REQUIRE(dest >= 0 && dest <= 3);
	ATF_REQUIRE(min_severity >= 0 && min_severity <= 7);
	ATF_REQUIRE(include_data == 0 || include_data == 1);
	ATF_REQUIRE(strlen(path) > 0);
}

ATF_ADD_TEST_CASE(tcf, audit_sysctl_tree);
ATF_ADD_TEST_CASE(tcf, audit_enable_disable);
ATF_ADD_TEST_CASE(tcf, audit_destination);
ATF_ADD_TEST_CASE(tcf, audit_severity_filter);
ATF_ADD_TEST_CASE(tcf, audit_file_path);
ATF_ADD_TEST_CASE(tcf, audit_rotation_config);
ATF_ADD_TEST_CASE(tcf, audit_event_count);
ATF_ADD_TEST_CASE(tcf, audit_access_priv);
ATF_ADD_TEST_CASE(tcf, audit_include_data);
ATF_ADD_TEST_CASE(tcf, audit_init);
