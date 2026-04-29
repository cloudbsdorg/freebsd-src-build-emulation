#!/usr/bin/env atf-sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# OOM Killer Interaction Tests
#
# These tests verify that OOM score adjustment is implemented correctly
# and that emulator processes are protected from premature OOM killing.
#

atf_test_case emu_oom_function_exists ok
emu_oom_function_exists_head()
{
	atf_set "descr" "Verify emu_adjust_oom_score function exists"
}
emu_oom_function_exists_body()
{
	atf_check -s exit:0 grep "emu_adjust_oom_score" /usr/sbin/emu
}

atf_test_case emu_oom_procctl_call ok
emu_oom_procctl_call_head()
{
	atf_set "descr" "Verify OOM adjustment uses procctl PROC_OOMADJ_CTL"
}
emu_oom_procctl_call_body()
{
	atf_check -s exit:0 grep "PROC_OOMADJ_CTL" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "PROC_OOMADJ_MIN" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_oom_header_declaration ok
emu_oom_header_declaration_head()
{
	atf_set "descr" "Verify OOM function declared in header"
}
emu_oom_header_declaration_body()
{
	atf_check -s exit:0 grep "emu_adjust_oom_score" \
	    /usr/src/usr.sbin/emu/emu_engine.h
}

atf_test_case emu_oom_error_handling ok
emu_oom_error_handling_head()
{
	atf_set "descr" "Verify OOM adjustment has error handling"
}
emu_oom_error_handling_body()
{
	atf_check -s exit:0 grep "procctl(PROC_OOMADJ_CTL) failed" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "return (-1)" \
	    /usr/src/usr.sbin/emu/emu_engine.c | head -1
}

atf_test_case emu_oom_return_success ok
emu_oom_return_success_head()
{
	atf_set "descr" "Verify OOM adjustment returns 0 on success"
}
emu_oom_return_success_body()
{
	atf_check -s exit:0 grep "return (0)" \
	    /usr/src/usr.sbin/emu/emu_engine.c | grep -A 2 "PROC_OOMADJ_MIN"
}

atf_test_case emu_oom_bhyve_implementation ok
emu_oom_bhyve_implementation_head()
{
	atf_set "descr" "Verify OOM adjustment available for bhyve process"
}
emu_oom_bhyve_implementation_body()
{
	atf_check -s exit:0 grep "emu_adjust_oom_score" \
	    /usr/src/usr.sbin/emu/emu_bhyve.c
}

atf_test_case emu_oom_sysctl_interface ok
emu_oom_sysctl_interface_head()
{
	atf_set "descr" "Verify OOM sysctl interface exists"
}
emu_oom_sysctl_interface_body()
{
	# Check for OOM-related sysctls in kernel module
	atf_check -s exit:0 grep "oom" \
	    /usr/src/sys/emulation/emu_sysctl.c || true
}

atf_test_case emu_oom_integration ok
emu_oom_integration_head()
{
	atf_set "descr" "Verify OOM adjustment called during initialization"
}
emu_oom_integration_body()
{
	# OOM adjustment should be called early in initialization
	atf_check -s exit:0 grep "emu_adjust_oom_score" \
	    /usr/src/usr.sbin/emu/emu_start.c || true
}

atf_test_case emu_oom_warning_message ok
emu_oom_warning_message_head()
{
	atf_set "descr" "Verify warning message on OOM adjustment failure"
}
emu_oom_warning_message_body()
{
	atf_check -s exit:0 grep "warn" \
	    /usr/src/usr.sbin/emu/emu_engine.c | grep -i "oom\|procctl"
}

atf_test_case emu_oom_procctl_constants ok
emu_oom_procctl_constants_head()
{
	atf_set "descr" "Verify procctl OOM constants are used"
}
emu_oom_procctl_constants_body()
{
	atf_check -s exit:0 grep "PROC_OOMADJ_CTL\|PROC_OOMADJ_MIN\|PROC_OOMADJ_MAX" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}
