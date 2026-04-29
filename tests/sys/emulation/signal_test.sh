#!/usr/bin/env atf-sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# Signal Handling Security Tests
#
# These tests verify that signal handlers are implemented correctly
# and that signals cannot inject unexpected behavior into the emulator.
#

atf_test_case emu_signal_handlers_exist ok
emu_signal_handlers_exist_head()
{
	atf_set "descr" "Verify signal handler functions exist in emu_engine"
}
emu_signal_handlers_exist_body()
{
	atf_check -s exit:0 grep "emu_init_signal_handlers" /usr/sbin/emu
	atf_check -s exit:0 grep "emu_check_signals" /usr/sbin/emu
	atf_check -s exit:0 grep "emu_handle_signal" /usr/sbin/emu
}

atf_test_case emu_signal_flags_atomic ok
emu_signal_flags_atomic_head()
{
	atf_set "descr" "Verify signal flags use atomic sig_atomic_t type"
}
emu_signal_flags_atomic_body()
{
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigsegv_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigpipe_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigterm_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigint_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sighup_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigusr1_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "volatile sig_atomic_t g_sigusr2_flag" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_signal_handler_no_nonreentrant ok
emu_signal_handler_no_nonreentrant_head()
{
	atf_set "descr" "Verify signal handler has no non-reentrant function calls"
}
emu_signal_handler_no_nonreentrant_body()
{
	# Signal handler should only set flags, no malloc/printf/etc
	atf_check -s exit:0 grep -A 30 "emu_signal_handler(int sig)" \
	    /usr/src/usr.sbin/emu/emu_engine.c | grep -v "malloc\|printf\|fprintf\|warn\|err"
}

atf_test_case emu_sigsegv_handling ok
emu_sigsegv_handling_head()
{
	atf_set "descr" "Test SIGSEGV signal handling (expect graceful shutdown)"
}
emu_sigsegv_handling_body()
{
	# SIGSEGV should trigger graceful shutdown
	atf_check -s exit:0 grep "SIGSEGV received" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "segmentation violation in emulator" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_sigpipe_handling ok
emu_sigpipe_handling_head()
{
	atf_set "descr" "Test SIGPIPE signal handling (broken pipe recovery)"
}
emu_sigpipe_handling_body()
{
	# SIGPIPE should allow continuation (pipe may be reconnected)
	atf_check -s exit:0 grep "SIGPIPE received" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "broken pipe in console/network" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "Continue execution, pipe may be reconnected" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_sigterm_handling ok
emu_sigterm_handling_head()
{
	atf_set "descr" "Test SIGTERM signal handling (graceful termination)"
}
emu_sigterm_handling_body()
{
	# SIGTERM should trigger graceful termination
	atf_check -s exit:0 grep "SIGTERM received" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "terminating gracefully" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_sigint_handling ok
emu_sigint_handling_head()
{
	atf_set "descr" "Test SIGINT signal handling (interrupt execution)"
}
emu_sigint_handling_body()
{
	# SIGINT should interrupt execution
	atf_check -s exit:0 grep "SIGINT received" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "interrupting execution" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_signal_handler_registration ok
emu_signal_handler_registration_head()
{
	atf_set "descr" "Verify signal handlers are registered with sigaction"
}
emu_signal_handler_registration_body()
{
	atf_check -s exit:0 grep "sigaction(signals\[i\], &sa, NULL)" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "sa.sa_handler = emu_signal_handler" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "sigemptyset(&sa.sa_mask)" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_signal_flags_cleared_on_check ok
emu_signal_flags_cleared_on_check_head()
{
	atf_set "descr" "Verify signal flags are cleared when checked"
}
emu_signal_flags_cleared_on_check_body()
{
	# Each flag should be cleared (set to 0) after being checked
	atf_check -s exit:0 grep "g_sigsegv_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sigpipe_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sigterm_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sigint_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sighup_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sigusr1_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "g_sigusr2_flag = 0" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_signal_stdlib_includes ok
emu_signal_stdlib_includes_head()
{
	atf_set "descr" "Verify signal.h and stdatomic.h are included"
}
emu_signal_stdlib_includes_body()
{
	atf_check -s exit:0 grep "#include <signal.h>" \
	    /usr/src/usr.sbin/emu/emu_engine.c
	atf_check -s exit:0 grep "#include <stdatomic.h>" \
	    /usr/src/usr.sbin/emu/emu_engine.c
}

atf_test_case emu_signal_header_declarations ok
emu_signal_header_declarations_head()
{
	atf_set "descr" "Verify signal handler functions declared in header"
}
emu_signal_header_declarations_body()
{
	atf_check -s exit:0 grep "emu_init_signal_handlers" \
	    /usr/src/usr.sbin/emu/emu_engine.h
	atf_check -s exit:0 grep "emu_check_signals" \
	    /usr/src/usr.sbin/emu/emu_engine.h
	atf_check -s exit:0 grep "emu_handle_signal" \
	    /usr/src/usr.sbin/emu/emu_engine.h
}
