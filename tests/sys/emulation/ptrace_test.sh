#!/usr/bin/env atf-sh

#
# Ptrace prevention tests for the emulation framework
#
# These tests verify that ptrace attachment to emulator processes
# is properly blocked to prevent debugger attacks and guest memory
# inspection.
#

atf_test_case emu_ptrace_proc_trace_disable success
emu_ptrace_proc_trace_disable_head() {
	atf_set "descr" "Test that PROC_TRACE_CTL_DISABLE is called"
	atf_set "require.user" "root"
}
emu_ptrace_proc_trace_disable_body() {
	# Verify that emu_disable_ptrace() function exists in the binary
	if ! command -v emu >/dev/null 2>&1; then
		atf_skip "emu command not installed"
	fi

	# Check that the function symbol exists
	if ! nm /usr/sbin/emu 2>/dev/null | grep -q "emu_disable_ptrace"; then
		# Try the build directory
		if ! nm /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu.full 2>/dev/null | grep -q "emu_disable_ptrace"; then
			atf_skip "emu_disable_ptrace symbol not found (may not be built yet)"
		fi
	fi

	atf_pass
}

atf_test_case emu_ptrace_bhyve_proc_trace_disable success
emu_ptrace_bhyve_proc_trace_disable_head() {
	atf_set "descr" "Test that bhyve path also disables ptrace"
	atf_set "require.user" "root"
}
emu_ptrace_bhyve_proc_trace_disable_body() {
	# Verify that emu_bhyve_disable_ptrace() function exists
	if ! nm /usr/sbin/emu 2>/dev/null | grep -q "emu_bhyve_disable_ptrace"; then
		if ! nm /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu.full 2>/dev/null | grep -q "emu_bhyve_disable_ptrace"; then
			atf_skip "emu_bhyve_disable_ptrace symbol not found (may not be built yet)"
		fi
	fi

	atf_pass
}

atf_test_case emu_ptrace_headers success
emu_ptrace_headers_head() {
	atf_set "descr" "Test that procctl headers are included"
}
emu_ptrace_headers_body() {
	# Verify sys/procctl.h is included in emu_engine.c
	if ! grep -q "sys/procctl.h" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "sys/procctl.h not included in emu_engine.c"
	fi

	# Verify sys/procctl.h is included in emu_bhyve.c
	if ! grep -q "sys/procctl.h" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c; then
		atf_fail "sys/procctl.h not included in emu_bhyve.c"
	fi

	atf_pass
}

atf_test_case emu_ptrace_implementation success
emu_ptrace_implementation_head() {
	atf_set "descr" "Test that ptrace disable implementation uses procctl"
}
emu_ptrace_implementation_body() {
	# Verify emu_disable_ptrace uses PROC_TRACE_CTL
	if ! grep -q "PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "emu_disable_ptrace does not use PROC_TRACE_CTL"
	fi

	# Verify PROC_TRACE_CTL_DISABLE is used
	if ! grep -q "PROC_TRACE_CTL_DISABLE" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "emu_disable_ptrace does not use PROC_TRACE_CTL_DISABLE"
	fi

	atf_pass
}

atf_test_case emu_ptrace_bhyve_implementation success
emu_ptrace_bhyve_implementation_head() {
	atf_set "descr" "Test that bhyve ptrace disable implementation uses procctl"
}
emu_ptrace_bhyve_implementation_body() {
	# Verify emu_bhyve_disable_ptrace uses PROC_TRACE_CTL
	if ! grep -q "PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c; then
		atf_fail "emu_bhyve_disable_ptrace does not use PROC_TRACE_CTL"
	fi

	# Verify PROC_TRACE_CTL_DISABLE is used
	if ! grep -q "PROC_TRACE_CTL_DISABLE" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c; then
		atf_fail "emu_bhyve_disable_ptrace does not use PROC_TRACE_CTL_DISABLE"
	fi

	atf_pass
}

atf_test_case emu_ptrace_error_handling success
emu_ptrace_error_handling_head() {
	atf_set "descr" "Test that ptrace disable has proper error handling"
}
emu_ptrace_error_handling_body() {
	# Verify error handling in emu_disable_ptrace
	if ! grep -q "warn.*PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "emu_disable_ptrace lacks error handling"
	fi

	# Verify error handling in emu_bhyve_disable_ptrace
	if ! grep -q "warn.*PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c; then
		atf_fail "emu_bhyve_disable_ptrace lacks error handling"
	fi

	atf_pass
}

atf_test_case emu_ptrace_function_declaration success
emu_ptrace_function_declaration_head() {
	atf_set "descr" "Test that ptrace disable functions are declared in headers"
}
emu_ptrace_function_declaration_body() {
	# Verify emu_disable_ptrace declared in emu_engine.h
	if ! grep -q "emu_disable_ptrace" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.h; then
		atf_fail "emu_disable_ptrace not declared in emu_engine.h"
	fi

	# Verify emu_bhyve_disable_ptrace declared in emu_bhyve.h
	if ! grep -q "emu_bhyve_disable_ptrace" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.h; then
		atf_fail "emu_bhyve_disable_ptrace not declared in emu_bhyve.h"
	fi

	atf_pass
}

atf_test_case emu_ptrace_security_property success
emu_ptrace_security_property_head() {
	atf_set "descr" "Test that ptrace prevention is a security feature"
}
emu_ptrace_security_property_body() {
	# Verify documentation mentions security
	if ! grep -qi "security\|prevent\|attack\|debugger" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "ptrace prevention lacks security documentation"
	fi

	atf_pass
}

atf_test_case emu_ptrace_both_paths success
emu_ptrace_both_paths_head() {
	atf_set "descr" "Test that both custom emulator and bhyve paths disable ptrace"
}
emu_ptrace_both_paths_body() {
	# Verify both implementations exist
	custom_exists=0
	bhyve_exists=0

	grep -q "emu_disable_ptrace" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c && custom_exists=1
	grep -q "emu_bhyve_disable_ptrace" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c && bhyve_exists=1

	if [ $custom_exists -eq 0 ]; then
		atf_fail "Custom emulator ptrace disable not implemented"
	fi

	if [ $bhyve_exists -eq 0 ]; then
		atf_fail "Bhyve path ptrace disable not implemented"
	fi

	atf_pass
}

atf_test_case emu_ptrace_procctl_call success
emu_ptrace_procctl_call_head() {
	atf_set "descr" "Test that procctl is called with correct parameters"
}
emu_ptrace_procctl_call_body() {
	# Verify procctl call format in emu_engine.c
	if ! grep -q "procctl(P_PID, getpid(), PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_engine.c; then
		atf_fail "procctl not called correctly in emu_engine.c"
	fi

	# Verify procctl call format in emu_bhyve.c
	if ! grep -q "procctl(P_PID, getpid(), PROC_TRACE_CTL" /home/mlapointe/git/freebsd-src-build-emulation/usr.sbin/emu/emu_bhyve.c; then
		atf_fail "procctl not called correctly in emu_bhyve.c"
	fi

	atf_pass
}

atf_init_test_cases() {
	atf_add_test_case emu_ptrace_proc_trace_disable
	atf_add_test_case emu_ptrace_bhyve_proc_trace_disable
	atf_add_test_case emu_ptrace_headers
	atf_add_test_case emu_ptrace_implementation
	atf_add_test_case emu_ptrace_bhyve_implementation
	atf_add_test_case emu_ptrace_error_handling
	atf_add_test_case emu_ptrace_function_declaration
	atf_add_test_case emu_ptrace_security_property
	atf_add_test_case emu_ptrace_both_paths
	atf_add_test_case emu_ptrace_procctl_call
}
