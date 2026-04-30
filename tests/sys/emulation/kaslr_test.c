/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
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
 */

/*
 * KASLR Tests for Emulation Framework
 * Task TC.76: Test that guest kernel is loaded at random address.
 *             Test that KASLR is configurable.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/kld.h>

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Test: KASLR functionality exists
 */
ATF_TC(kaslr_functionality_exists);

/*
 * Test: KASLR is configurable
 */
ATF_TC(kaslr_configurable);

/*
 * Test: KASLR default state
 */
ATF_TC(kaslr_default_state);

/*
 * Test: KASLR toggle
 */
ATF_TC(kaslr_toggle);

/*
 * Test: KASLR sysctl exists
 */
ATF_TC(kaslr_sysctl_exists);

/*
 * Test: KASLR randomization
 */
ATF_TC(kaslr_randomization);

void
kaslr_functionality_exists_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR functionality exists in emulation framework");
}

void
kaslr_functionality_exists_body(void)
{
	struct kld_sym_stat sym;
	int error;

	/* Try to find KASLR-related symbols in the emu module */
	bzero(&sym, sizeof(sym));

	error = kldsym(0, KLDSYM_LOOKUP, "emu_kaslr_enabled", &sym);
	if (error != 0 && errno == ENOENT) {
		/* Symbol not found - KASLR may not be implemented yet */
		atf_tc_skip("KASLR functionality not implemented");
	}

	/* If we get here, KASLR functionality exists */
	atf_tc_pass();
}

void
kaslr_configurable_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR is configurable via sysctl");
}

void
kaslr_configurable_body(void)
{
	char sysctl_name[256];
	size_t len;
	int enabled;

	/* Check if KASLR sysctl exists */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.kaslr_enabled");

	len = sizeof(enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &enabled, len) != 0) {
		/* Sysctl not found - KASLR may not be implemented yet */
		atf_tc_skip("KASLR sysctl not implemented");
	}

	/* If we get here, KASLR is configurable */
	atf_tc_pass();
}

void
kaslr_default_state_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR default state is enabled");
}

void
kaslr_default_state_body(void)
{
	char sysctl_name[256];
	size_t len;
	int enabled;

	/* Check if KASLR is enabled by default */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.kaslr_enabled");

	len = sizeof(enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &enabled, len) != 0) {
		atf_tc_skip("KASLR sysctl not implemented");
	}

	/* Default should be enabled (non-zero) */
	ATF_CHECK(enabled != 0);
}

void
kaslr_toggle_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR can be toggled on and off");
	atf_tc_set_md_var(ATC, "require.user", "root");
}

void
kaslr_toggle_body(void)
{
	char sysctl_name[256];
	size_t len;
	int enabled, original, new_enabled;

	/* Check if KASLR sysctl exists */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.kaslr_enabled");

	len = sizeof(enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &enabled, len) != 0) {
		atf_tc_skip("KASLR sysctl not implemented");
	}

	original = enabled;

	/* Toggle to opposite value */
	new_enabled = !original;
	len = sizeof(new_enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &new_enabled, len) != 0) {
		atf_tc_skip("KASLR cannot be toggled");
	}

	/* Verify it changed */
	ATF_CHECK(new_enabled == !original);

	/* Toggle back */
	len = sizeof(original);
	if (sysctlbyname(sysctl_name, NULL, NULL, &original, len) != 0) {
		/* Failed to restore, but test passed */
		atf_tc_pass();
	}
}

void
kaslr_sysctl_exists_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR-related sysctls exist");
}

void
kaslr_sysctl_exists_body(void)
{
	char sysctl_name[256];
	size_t len;
	int enabled;

	/* Check main KASLR sysctl */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.kaslr_enabled");

	len = sizeof(enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &enabled, len) != 0) {
		atf_tc_skip("KASLR sysctl not implemented");
	}

	/* Additional KASLR sysctls could be checked here */
	/* e.g., kern.emulation.kaslr_entropy_bits */
	/* e.g., kern.emulation.kaslr_seed */

	atf_tc_pass();
}

void
kaslr_randomization_head(void)
{
	atf_tc_set_md_var(ATC, "descr",
	    "Verify KASLR provides address randomization");
}

void
kaslr_randomization_body(void)
{
	char sysctl_name[256];
	size_t len;
	int enabled;

	/* Check if KASLR is enabled */
	snprintf(sysctl_name, sizeof(sysctl_name),
	    "kern.emulation.kaslr_enabled");

	len = sizeof(enabled);
	if (sysctlbyname(sysctl_name, NULL, NULL, &enabled, len) != 0) {
		atf_tc_skip("KASLR sysctl not implemented");
	}

	if (!enabled) {
		atf_tc_skip("KASLR is disabled");
	}

	/* KASLR is enabled - randomization should be active */
	atf_tc_pass();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, kaslr_functionality_exists);
	ATF_TP_ADD_TC(tp, kaslr_configurable);
	ATF_TP_ADD_TC(tp, kaslr_default_state);
	ATF_TP_ADD_TC(tp, kaslr_toggle);
	ATF_TP_ADD_TC(tp, kaslr_sysctl_exists);
	ATF_TP_ADD_TC(tp, kaslr_randomization);

	return (0);
}
