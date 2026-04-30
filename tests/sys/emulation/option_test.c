/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
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
 */

/*
 * Kernel Option Parsing Tests for Emulation Framework
 * Task 8.1: Verify option enable/disable via sysctl interface.
 *            Test all KERNEL_EMULATION_* combinations.
 *            No kernel code loaded - tests sysctl interface only.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/kld.h>
#include <sys/errno.h>

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test configuration structure */
struct option_test_config {
    const char *sysctl_name;
    int expected_default;
    int min_val;
    int max_val;
    const char *description;
};

/* Default test configurations for emulation framework options */
static struct option_test_config emulation_options[] = {
    {
        .sysctl_name = "kern.emulation.enabled",
        .expected_default = 0,
        .min_val = 0,
        .max_val = 1,
        .description = "Main emulation enable/disable"
    },
    {
        .sysctl_name = "kern.emulation.allow_nonroot",
        .expected_default = 0,
        .min_val = 0,
        .max_val = 1,
        .description = "Allow non-root users"
    },
    {
        .sysctl_name = "kern.emulation.memory_overcommit",
        .expected_default = 0,
        .min_val = 0,
        .max_val = 1,
        .description = "Allow memory overcommit"
    },
    {
        .sysctl_name = "kern.emulation.sandbox_capsicum",
        .expected_default = 1,
        .min_val = 0,
        .max_val = 1,
        .description = "Enable Capsicum sandboxing"
    },
    {
        .sysctl_name = "kern.emulation.nested_virt",
        .expected_default = 0,
        .min_val = 0,
        .max_val = 1,
        .description = "Enable nested virtualization"
    },
    {
        .sysctl_name = "kern.emulation.max_instances",
        .expected_default = 16,
        .min_val = 1,
        .max_val = 1024,
        .description = "Maximum concurrent instances"
    },
    {
        .sysctl_name = "kern.emulation.required_group",
        .expected_default = 979,
        .min_val = 0,
        .max_val = 65535,
        .description = "Required GID for non-root access"
    },
    {
        .sysctl_name = "kern.emulation.crash_capture",
        .expected_default = 1,
        .min_val = 0,
        .max_val = 1,
        .description = "Enable crash state capture"
    },
    {
        .sysctl_name = "kern.emulation.crash_contained",
        .expected_default = 1,
        .min_val = 0,
        .max_val = 1,
        .description = "Enable crash containment"
    },
    {
        .sysctl_name = "kern.emulation.crash_notify",
        .expected_default = 1,
        .min_val = 0,
        .max_val = 1,
        .description = "Enable crash notification"
    }
};

#define NUM_OPTIONS (sizeof(emulation_options) / sizeof(emulation_options[0]))

/*
 * Test: Emulation sysctl tree exists
 * Verifies that kern.emulation.* sysctl tree is accessible
 */
ATF_TC(sysctl_tree_exists);

/*
 * Test: Emulation enabled sysctl
 * Verifies kern.emulation.enabled exists and has correct default
 */
ATF_TC(emu_enabled_sysctl);

/*
 * Test: Emulation security options
 * Verifies allow_nonroot and sandbox_capsicum options
 */
ATF_TC(emu_security_options);

/*
 * Test: Emulation resource limits
 * Verifies max_instances and required_group options
 */
ATF_TC(emu_resource_limits);

/*
 * Test: Emulation crash options
 * Verifies crash_capture, crash_contained, crash_notify options
 */
ATF_TC(emu_crash_options);

/*
 * Test: Emulation option enable/disable
 * Tests that options can be toggled between valid values
 */
ATF_TC(emu_option_toggle);

/*
 * Test: Emulation invalid option values
 * Tests that invalid values are rejected
 */
ATF_TC(emu_invalid_values);

/*
 * Test: Module loading with default options
 * Verifies module loads with default (secure) settings
 */
ATF_TC(emu_module_default_options);

/*
 * Test: Per-architecture options exist
 * Verifies architecture-specific emulation options
 */
ATF_TC(emu_arch_options);

/*
 * Test: Memory policy options
 * Verifies memory overcommit and related settings
 */
ATF_TC(emu_memory_options);

/*
 * Test: Instance options
 * Verifies per-instance option sysctls
 */
ATF_TC(emu_instance_options);

/* Helper function to check if sysctl exists */
static int
sysctl_exists(const char *name)
{
    int mib[CTL_MAXNAME];
    char strbuf[256];
    size_t len = sizeof(strbuf);
    int error;

    /* Convert sysctl name to mib */
    error = sysctlgetmibname(name, strbuf, &len);
    if (error != 0) {
        return 0;
    }

    /* Try to read the value */
    int val;
    size_t vallen = sizeof(val);
    error = sysctlbyname(name, &val, &vallen, NULL, 0);
    
    return (error == 0);
}

/* Helper function to read sysctl integer value */
static int
sysctl_get_int(const char *name, int *val)
{
    size_t vallen = sizeof(*val);
    return (sysctlbyname(name, val, &vallen, NULL, 0) == 0);
}

/* Helper function to write sysctl integer value */
static int
sysctl_set_int(const char *name, int val)
{
    return (sysctlbyname(name, NULL, NULL, &val, sizeof(val)) == 0);
}

/* Helper function to check if emu module is loaded */
static int
emu_module_loaded(void)
{
    return (kldfind("emu_core") != -1 || kldfind("emu") != -1);
}

/*
 * Implementation: sysctl_tree_exists
 */
void
sysctl_tree_exists_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify kern.emulation.* sysctl tree exists");
}

void
sysctl_tree_exists_body(void)
{
    /* Check top-level kern.emulation sysctl exists */
    if (!sysctl_exists("kern.emulation")) {
        /* Module may not be loaded yet */
        atf_tc_skip("kern.emulation sysctl not available (module not loaded)");
    }

    /* Verify we can read enabled status */
    int enabled;
    if (!sysctl_get_int("kern.emulation.enabled", &enabled)) {
        atf_tc_fail("Cannot read kern.emulation.enabled");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_enabled_sysctl
 */
void
emu_enabled_sysctl_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify kern.emulation.enabled sysctl exists and has correct default");
}

void
emu_enabled_sysctl_body(void)
{
    /* Check if module is loaded */
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Verify sysctl exists */
    if (!sysctl_exists("kern.emulation.enabled")) {
        atf_tc_fail("kern.emulation.enabled sysctl does not exist");
    }

    /* Read default value - should be 0 (disabled) */
    int enabled;
    if (!sysctl_get_int("kern.emulation.enabled", &enabled)) {
        atf_tc_fail("Cannot read kern.emulation.enabled");
    }

    /* Verify default is 0 (disabled for security) */
    ATF_CHECK_EQ_MSG(enabled, 0,
        "kern.emulation.enabled should default to 0 (disabled)");

    /* Test enabling emulation */
    if (!sysctl_set_int("kern.emulation.enabled", 1)) {
        atf_tc_fail("Cannot enable emulation via sysctl");
    }

    /* Verify it was set */
    if (!sysctl_get_int("kern.emulation.enabled", &enabled)) {
        atf_tc_fail("Cannot read kern.emulation.enabled after enable");
    }

    ATF_CHECK_EQ_MSG(enabled, 1,
        "kern.emulation.enabled should be 1 after enabling");

    /* Restore default */
    sysctl_set_int("kern.emulation.enabled", 0);

    atf_tc_pass();
}

/*
 * Implementation: emu_security_options
 */
void
emu_security_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify allow_nonroot and sandbox_capsicum security options");
}

void
emu_security_options_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test allow_nonroot option */
    if (sysctl_exists("kern.emulation.allow_nonroot")) {
        int allow_nonroot;
        if (!sysctl_get_int("kern.emulation.allow_nonroot", &allow_nonroot)) {
            atf_tc_fail("Cannot read allow_nonroot");
        }
        
        /* Default should be 0 (secure) */
        ATF_CHECK_EQ_MSG(allow_nonroot, 0,
            "allow_nonroot should default to 0 (root-only)");

        /* Verify toggle works */
        sysctl_set_int("kern.emulation.allow_nonroot", 1);
        sysctl_get_int("kern.emulation.allow_nonroot", &allow_nonroot);
        ATF_CHECK_EQ_MSG(allow_nonroot, 1,
            "allow_nonroot should be 1 after setting");

        sysctl_set_int("kern.emulation.allow_nonroot", 0);
    } else {
        atf_tc_skip("allow_nonroot sysctl not available");
    }

    /* Test sandbox_capsicum option */
    if (sysctl_exists("kern.emulation.sandbox_capsicum")) {
        int sandbox;
        if (!sysctl_get_int("kern.emulation.sandbox_capsicum", &sandbox)) {
            atf_tc_fail("Cannot read sandbox_capsicum");
        }
        
        /* Default should be 1 (enabled for security) */
        ATF_CHECK_EQ_MSG(sandbox, 1,
            "sandbox_capsicum should default to 1 (enabled)");
    } else {
        atf_tc_skip("sandbox_capsicum sysctl not available");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_resource_limits
 */
void
emu_resource_limits_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify max_instances and required_group resource limit options");
}

void
emu_resource_limits_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test max_instances option */
    if (sysctl_exists("kern.emulation.max_instances")) {
        int max_inst;
        if (!sysctl_get_int("kern.emulation.max_instances", &max_inst)) {
            atf_tc_fail("Cannot read max_instances");
        }

        /* Verify reasonable default (1-1024) */
        ATF_CHECK_GE_MSG(max_inst, 1,
            "max_instances should be at least 1");
        ATF_CHECK_LE_MSG(max_inst, 1024,
            "max_instances should be at most 1024");

        /* Test setting a new value */
        sysctl_set_int("kern.emulation.max_instances", 32);
        sysctl_get_int("kern.emulation.max_instances", &max_inst);
        ATF_CHECK_EQ_MSG(max_inst, 32,
            "max_instances should be 32 after setting");

        /* Restore default */
        sysctl_set_int("kern.emulation.max_instances", 16);
    } else {
        atf_tc_skip("max_instances sysctl not available");
    }

    /* Test required_group option */
    if (sysctl_exists("kern.emulation.required_group")) {
        int req_group;
        if (!sysctl_get_int("kern.emulation.required_group", &req_group)) {
            atf_tc_fail("Cannot read required_group");
        }

        /* Default should be 979 (GID_EMU) */
        ATF_CHECK_EQ_MSG(req_group, 979,
            "required_group should default to 979 (GID_EMU)");
    } else {
        atf_tc_skip("required_group sysctl not available");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_crash_options
 */
void
emu_crash_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify crash_capture, crash_contained, crash_notify options");
}

void
emu_crash_options_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test crash_capture option */
    if (sysctl_exists("kern.emulation.crash_capture")) {
        int capture;
        if (!sysctl_get_int("kern.emulation.crash_capture", &capture)) {
            atf_tc_fail("Cannot read crash_capture");
        }
        ATF_CHECK_EQ_MSG(capture, 1,
            "crash_capture should default to 1 (enabled)");
    }

    /* Test crash_contained option */
    if (sysctl_exists("kern.emulation.crash_contained")) {
        int contained;
        if (!sysctl_get_int("kern.emulation.crash_contained", &contained)) {
            atf_tc_fail("Cannot read crash_contained");
        }
        ATF_CHECK_EQ_MSG(contained, 1,
            "crash_contained should default to 1 (enabled)");
    }

    /* Test crash_notify option */
    if (sysctl_exists("kern.emulation.crash_notify")) {
        int notify;
        if (!sysctl_get_int("kern.emulation.crash_notify", &notify)) {
            atf_tc_fail("Cannot read crash_notify");
        }
        ATF_CHECK_EQ_MSG(notify, 1,
            "crash_notify should default to 1 (enabled)");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_option_toggle
 */
void
emu_option_toggle_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that options can be toggled between valid values");
}

void
emu_option_toggle_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test toggle for kern.emulation.enabled */
    if (sysctl_exists("kern.emulation.enabled")) {
        /* Toggle to 1 */
        if (!sysctl_set_int("kern.emulation.enabled", 1)) {
            atf_tc_fail("Cannot set enabled to 1");
        }
        
        int val;
        sysctl_get_int("kern.emulation.enabled", &val);
        ATF_CHECK_EQ_MSG(val, 1, "Should be 1 after enabling");

        /* Toggle back to 0 */
        if (!sysctl_set_int("kern.emulation.enabled", 0)) {
            atf_tc_fail("Cannot set enabled to 0");
        }
        
        sysctl_get_int("kern.emulation.enabled", &val);
        ATF_CHECK_EQ_MSG(val, 0, "Should be 0 after disabling");
    }

    /* Test toggle for nested_virt */
    if (sysctl_exists("kern.emulation.nested_virt")) {
        int val;

        sysctl_set_int("kern.emulation.nested_virt", 1);
        sysctl_get_int("kern.emulation.nested_virt", &val);
        ATF_CHECK_EQ_MSG(val, 1, "nested_virt should be 1 after enabling");

        sysctl_set_int("kern.emulation.nested_virt", 0);
        sysctl_get_int("kern.emulation.nested_virt", &val);
        ATF_CHECK_EQ_MSG(val, 0, "nested_virt should be 0 after disabling");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_invalid_values
 */
void
emu_invalid_values_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Test that invalid option values are rejected");
}

void
emu_invalid_values_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test that boolean options reject values > 1 */
    if (sysctl_exists("kern.emulation.enabled")) {
        /* Save original value */
        int orig;
        sysctl_get_int("kern.emulation.enabled", &orig);

        /* Try to set invalid value */
        sysctl_set_int("kern.emulation.enabled", 999);
        
        /* Value should either be rejected or clamped */
        int val;
        sysctl_get_int("kern.emulation.enabled", &val);
        
        /* If it was set, it should be clamped to 0 or 1 */
        if (val != orig) {
            ATF_CHECK(val >= 0 && val <= 1);
        }

        /* Restore original */
        sysctl_set_int("kern.emulation.enabled", orig);
    }

    /* Test that max_instances rejects 0 */
    if (sysctl_exists("kern.emulation.max_instances")) {
        int orig;
        sysctl_get_int("kern.emulation.max_instances", &orig);

        /* Try to set 0 - should fail or be clamped */
        sysctl_set_int("kern.emulation.max_instances", 0);
        
        int val;
        sysctl_get_int("kern.emulation.max_instances", &val);
        
        /* If set, should be at least 1 */
        if (val == 0) {
            atf_tc_fail("max_instances should reject 0");
        }

        /* Restore */
        sysctl_set_int("kern.emulation.max_instances", orig);
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_module_default_options
 */
void
emu_module_default_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify module loads with secure default options");
}

void
emu_module_default_options_body(void)
{
    /* Check if module is loaded */
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Verify security defaults */
    
    /* 1. Emulation should be disabled by default */
    int enabled;
    if (sysctl_get_int("kern.emulation.enabled", &enabled)) {
        ATF_CHECK_EQ_MSG(enabled, 0,
            "Emulation should be disabled by default");
    }

    /* 2. Non-root should not be allowed by default */
    int allow_nonroot;
    if (sysctl_get_int("kern.emulation.allow_nonroot", &allow_nonroot)) {
        ATF_CHECK_EQ_MSG(allow_nonroot, 0,
            "Non-root access should be disabled by default");
    }

    /* 3. Capsicum sandboxing should be enabled by default */
    int sandbox;
    if (sysctl_get_int("kern.emulation.sandbox_capsicum", &sandbox)) {
        ATF_CHECK_EQ_MSG(sandbox, 1,
            "Capsicum sandboxing should be enabled by default");
    }

    /* 4. Crash capture should be enabled by default */
    int capture;
    if (sysctl_get_int("kern.emulation.crash_capture", &capture)) {
        ATF_CHECK_EQ_MSG(capture, 1,
            "Crash capture should be enabled by default");
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_arch_options
 */
void
emu_arch_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify architecture-specific emulation options");
}

void
emu_arch_options_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Check for architecture-related sysctls */
    const char *arch_sysctls[] = {
        "kern.emulation.caps",
        "kern.emulation.mode",
        "kern.emulation.host_arch",
        "kern.emulation.host_cpus"
    };

    int found_any = 0;
    for (size_t i = 0; i < sizeof(arch_sysctls) / sizeof(arch_sysctls[0]); i++) {
        if (sysctl_exists(arch_sysctls[i])) {
            found_any = 1;
            break;
        }
    }

    if (!found_any) {
        atf_tc_skip("Architecture sysctls not available");
    }

    /* Verify host_arch reflects actual architecture */
    char arch_buf[32];
    size_t arch_len = sizeof(arch_buf);
    if (sysctlbyname("kern.emulation.host_arch", arch_buf, &arch_len, NULL, 0) == 0) {
        /* Should be non-empty */
        ATF_CHECK(strlen(arch_buf) > 0);
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_memory_options
 */
void
emu_memory_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify memory overcommit and related settings");
}

void
emu_memory_options_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Test memory_overcommit option */
    if (sysctl_exists("kern.emulation.memory_overcommit")) {
        int overcommit;
        if (!sysctl_get_int("kern.emulation.memory_overcommit", &overcommit)) {
            atf_tc_fail("Cannot read memory_overcommit");
        }

        /* Default should be 0 (no overcommit for safety) */
        ATF_CHECK_EQ_MSG(overcommit, 0,
            "memory_overcommit should default to 0 (disabled)");

        /* Toggle */
        sysctl_set_int("kern.emulation.memory_overcommit", 1);
        sysctl_get_int("kern.emulation.memory_overcommit", &overcommit);
        ATF_CHECK_EQ_MSG(overcommit, 1,
            "memory_overcommit should be 1 after enabling");

        sysctl_set_int("kern.emulation.memory_overcommit", 0);
    } else {
        atf_tc_skip("memory_overcommit sysctl not available");
    }

    /* Test memory_balloon_min_pct if exists */
    if (sysctl_exists("kern.emulation.memory_balloon_min_pct")) {
        int min_pct;
        if (sysctl_get_int("kern.emulation.memory_balloon_min_pct", &min_pct)) {
            /* Should be in valid range (0-100) */
            ATF_CHECK_GE_MSG(min_pct, 0,
                "memory_balloon_min_pct should be >= 0");
            ATF_CHECK_LE_MSG(min_pct, 100,
                "memory_balloon_min_pct should be <= 100");
        }
    }

    atf_tc_pass();
}

/*
 * Implementation: emu_instance_options
 */
void
emu_instance_options_head(void)
{
    atf_tc_set_md_var(ATC, "descr",
        "Verify per-instance option sysctls");
}

void
emu_instance_options_body(void)
{
    if (!emu_module_loaded()) {
        atf_tc_skip("emu module not loaded");
    }

    /* Per-instance options should be under kern.emulation.instance.<name> */
    /* These would only exist after an instance is created */

    /* Check for instances list */
    if (!sysctl_exists("kern.emulation.instances")) {
        atf_tc_skip("instances sysctl not available");
    }

    /* Read number of instances */
    char instances_buf[1024];
    size_t len = sizeof(instances_buf);
    if (sysctlbyname("kern.emulation.instances", instances_buf, &len, NULL, 0) == 0) {
        /* Should be a comma-separated list (may be empty) */
        ATF_CHECK(len > 0);
    }

    /* Check for num_instances */
    if (sysctl_exists("kern.emulation.num_instances")) {
        int num;
        if (sysctl_get_int("kern.emulation.num_instances", &num)) {
            ATF_CHECK_GE_MSG(num, 0,
                "num_instances should be >= 0");
        }
    }

    atf_tc_pass();
}

/* Add test cases to test suite */
ATF_TP_ADD_TCS(tp)
{

    ATF_TP_ADD_TC(tp, sysctl_tree_exists);
    ATF_TP_ADD_TC(tp, emu_enabled_sysctl);
    ATF_TP_ADD_TC(tp, emu_security_options);
    ATF_TP_ADD_TC(tp, emu_resource_limits);
    ATF_TP_ADD_TC(tp, emu_crash_options);
    ATF_TP_ADD_TC(tp, emu_option_toggle);
    ATF_TP_ADD_TC(tp, emu_invalid_values);
    ATF_TP_ADD_TC(tp, emu_module_default_options);
    ATF_TP_ADD_TC(tp, emu_arch_options);
    ATF_TP_ADD_TC(tp, emu_memory_options);
    ATF_TP_ADD_TC(tp, emu_instance_options);

    return (atf_no_error());
}
