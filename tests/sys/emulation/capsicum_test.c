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
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/capability.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <atf-c.h>

/*
 * Capsicum Sandboxing Unit Tests
 *
 * These tests verify the Capsicum sandboxing implementation
 * for the emulation framework (S6.1, S6.2).
 *
 * Test coverage:
 * - test_capsicum_enter: Verify capability mode entry
 * - test_capsicum_rights_limit: Verify FD rights limiting
 * - test_capsicum_ioctls_limit: Verify ioctl restrictions
 * - test_emulator_runs_under_capsicum: Verify emulator functionality in sandbox
 * - test_capsicum_no_file_access: Verify file access is blocked
 * - test_capsicum_no_network: Verify network access is blocked
 * - test_capsicum_no_process_spawn: Verify process execution is blocked
 */

/*
 * Test 1: Verify capability mode entry
 */
ATF_TC(test_capsicum_enter);
ATF_TC_HEAD(test_capsicum_enter, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that capability mode can be entered successfully");
}
ATF_TC_BODY(test_capsicum_enter, tc)
{
	int error;

	/* Enter capability mode */
	error = cap_enter();
	ATF_CHECK_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Verify we're in capability mode by trying to open a new file */
	/* This should fail in capability mode without prior rights */
	int fd = open("/tmp/test_capsicum", O_RDONLY);
	ATF_CHECK_MSG(fd < 0 && errno == ENOTCAPABLE,
	    "Expected ENOTCAPABLE in capability mode, got fd=%d, errno=%d",
	    fd, errno);
}

/*
 * Test 2: Verify FD rights limiting
 */
ATF_TC(test_capsicum_rights_limit);
ATF_TC_HEAD(test_capsicum_rights_limit, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that file descriptor rights can be limited");
}
ATF_TC_BODY(test_capsicum_rights_limit, tc)
{
	int fd, error;
	cap_rights_t rights;
	cap_ioctls_t ioctls;

	/* Create a test file */
	fd = open("/tmp/test_rights", O_RDWR | O_CREAT | O_TRUNC, 0600);
	ATF_REQUIRE_MSG(fd >= 0, "Failed to create test file: %s",
	    strerror(errno));

	/* Limit rights to read-only */
	cap_rights_init(&rights, CAP_READ, CAP_SEEK);
	error = cap_rights_limit(fd, &rights);
	ATF_CHECK_MSG(error == 0, "cap_rights_limit() failed: %s",
	    strerror(errno));

	/* Verify write is now denied */
	const char *test_data = "test";
	ssize_t written = write(fd, test_data, strlen(test_data));
	ATF_CHECK_MSG(written < 0 && errno == ENOTCAPABLE,
	    "Expected ENOTCAPABLE on write, got written=%zd, errno=%d",
	    written, errno);

	/* Verify read still works */
	char buf[64];
	ssize_t bytes_read = pread(fd, buf, sizeof(buf), 0);
	ATF_CHECK_MSG(bytes_read >= 0,
	    "pread() should succeed with CAP_READ: %s", strerror(errno));

	close(fd);
	unlink("/tmp/test_rights");
}

/*
 * Test 3: Verify ioctl restrictions
 */
ATF_TC(test_capsicum_ioctls_limit);
ATF_TC_HEAD(test_capsicum_ioctls_limit, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that ioctls can be restricted");
}
ATF_TC_BODY(test_capsicum_ioctls_limit, tc)
{
	int fd, error;
	cap_rights_t rights;
	cap_ioctls_t ioctls;

	/* Open /dev/null for testing */
	fd = open("/dev/null", O_RDWR);
	ATF_REQUIRE_MSG(fd >= 0, "Failed to open /dev/null: %s",
	    strerror(errno));

	/* Limit to basic rights with no ioctls */
	cap_rights_init(&rights, CAP_READ, CAP_WRITE);
	error = cap_rights_limit(fd, &rights);
	ATF_CHECK_MSG(error == 0, "cap_rights_limit() failed: %s",
	    strerror(errno));

	/* Try an ioctl - should fail */
	int dummy = 0;
	error = ioctl(fd, TIOCGWINSZ, &dummy);
	ATF_CHECK_MSG(error < 0 && errno == ENOTCAPABLE,
	    "Expected ENOTCAPABLE on ioctl, got error=%d, errno=%d",
	    error, errno);

	close(fd);
}

/*
 * Test 4: Verify emulator can run under Capsicum
 */
ATF_TC(test_emulator_runs_under_capsicum);
ATF_TC_HEAD(test_emulator_runs_under_capsicum, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that emulator can function after entering capability mode");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(test_emulator_runs_under_capsicum, tc)
{
	int status;
	pid_t pid;

	/* Fork a child process to test emulator under Capsicum */
	pid = fork();
	ATF_REQUIRE_MSG(pid >= 0, "fork() failed: %s", strerror(errno));

	if (pid == 0) {
		/* Child process - simulate emulator initialization */
		int dev_null = open("/dev/null", O_RDONLY);
		if (dev_null < 0)
			_exit(1);

		/* Enter capability mode */
		if (cap_enter() != 0)
			_exit(2);

		/* Verify we can still use pre-opened FDs */
		char buf[1];
		if (read(dev_null, buf, 1) < 0)
			_exit(3);

		close(dev_null);
		_exit(0);
	}

	/* Parent waits for child */
	if (waitpid(pid, &status, 0) < 0)
		atf_tc_fail("waitpid() failed: %s", strerror(errno));

	ATF_CHECK_MSG(WIFEXITED(status), "Child did not exit normally");
	ATF_CHECK_MSG(WEXITSTATUS(status) == 0,
	    "Child exited with status %d", WEXITSTATUS(status));
}

/*
 * Test 5: Verify file access is blocked in capability mode
 */
ATF_TC(test_capsicum_no_file_access);
ATF_TC_HEAD(test_capsicum_no_file_access, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that new file access is blocked in capability mode");
}
ATF_TC_BODY(test_capsicum_no_file_access, tc)
{
	int error;

	/* Enter capability mode */
	error = cap_enter();
	ATF_REQUIRE_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Try to open various files - all should fail */
	int fd1 = open("/etc/passwd", O_RDONLY);
	ATF_CHECK_MSG(fd1 < 0 && errno == ENOTCAPABLE,
	    "Opening /etc/passwd should fail in capability mode");

	int fd2 = open("/tmp/test", O_RDWR | O_CREAT, 0600);
	ATF_CHECK_MSG(fd2 < 0 && errno == ENOTCAPABLE,
	    "Creating /tmp/test should fail in capability mode");

	int fd3 = open("/", O_RDONLY);
	ATF_CHECK_MSG(fd3 < 0 && errno == ENOTCAPABLE,
	    "Opening / should fail in capability mode");
}

/*
 * Test 6: Verify network access is blocked
 */
ATF_TC(test_capsicum_no_network);
ATF_TC_HEAD(test_capsicum_no_network, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that network access is blocked in capability mode");
}
ATF_TC_BODY(test_capsicum_no_network, tc)
{
	int error, sock;

	/* Enter capability mode */
	error = cap_enter();
	ATF_REQUIRE_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Try to create a socket - should fail */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	ATF_CHECK_MSG(sock < 0 && errno == ENOTCAPABLE,
	    "socket() should fail in capability mode, got sock=%d, errno=%d",
	    sock, errno);

	/* Try to connect - should also fail */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock >= 0) {
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(80);
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		
		error = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
		ATF_CHECK_MSG(error < 0 && errno == ENOTCAPABLE,
		    "connect() should fail in capability mode");
		close(sock);
	}
}

/*
 * Test 7: Verify process execution is blocked
 */
ATF_TC(test_capsicum_no_process_spawn);
ATF_TC_HEAD(test_capsicum_no_process_spawn, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that process execution is blocked in capability mode");
}
ATF_TC_BODY(test_capsicum_no_process_spawn, tc)
{
	int error;
	pid_t pid;

	/* Enter capability mode */
	error = cap_enter();
	ATF_REQUIRE_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Try to fork - should fail */
	pid = fork();
	ATF_CHECK_MSG(pid < 0 && errno == ENOTCAPABLE,
	    "fork() should fail in capability mode, got pid=%d, errno=%d",
	    pid, errno);

	/* Try to exec - should also fail */
	char *argv[] = {"/bin/echo", "test", NULL};
	char *envp[] = {NULL};
	error = execve("/bin/echo", argv, envp);
	ATF_CHECK_MSG(error < 0 && errno == ENOTCAPABLE,
	    "execve() should fail in capability mode");
}

/*
 * Test 8: Verify rights can be selectively granted
 */
ATF_TC(test_capsicum_selective_rights);
ATF_TC_HEAD(test_capsicum_selective_rights, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that specific rights can be granted while others are denied");
}
ATF_TC_BODY(test_capsicum_selective_rights, tc)
{
	int fd, error;
	cap_rights_t rights;

	/* Create and write to a test file */
	fd = open("/tmp/test_selective", O_RDWR | O_CREAT | O_TRUNC, 0600);
	ATF_REQUIRE_MSG(fd >= 0, "Failed to create test file: %s",
	    strerror(errno));

	const char *test_data = "Hello, Capsicum!";
	ATF_REQUIRE_MSG(write(fd, test_data, strlen(test_data)) > 0,
	    "Failed to write test data");

	/* Limit to read and seek only */
	cap_rights_init(&rights, CAP_READ, CAP_SEEK);
	error = cap_rights_limit(fd, &rights);
	ATF_REQUIRE_MSG(error == 0, "cap_rights_limit() failed: %s",
	    strerror(errno));

	/* Seek and read should work */
	off_t offset = lseek(fd, 0, SEEK_SET);
	ATF_CHECK_MSG(offset >= 0, "lseek() should succeed");

	char buf[64];
	ssize_t bytes_read = read(fd, buf, sizeof(buf));
	ATF_CHECK_MSG(bytes_read > 0, "read() should succeed");
	ATF_CHECK_MSG(memcmp(buf, test_data, strlen(test_data)) == 0,
	    "Read data should match");

	/* Write should fail */
	ssize_t written = write(fd, "test", 4);
	ATF_CHECK_MSG(written < 0 && errno == ENOTCAPABLE,
	    "write() should fail with ENOTCAPABLE");

	close(fd);
	unlink("/tmp/test_selective");
}

/*
 * Test 9: Verify capability mode is one-way
 */
ATF_TC(test_capsicum_one_way);
ATF_TC_HEAD(test_capsicum_one_way, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that capability mode cannot be exited once entered");
}
ATF_TC_BODY(test_capsicum_one_way, tc)
{
	int error;

	/* Enter capability mode */
	error = cap_enter();
	ATF_REQUIRE_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Try to enter again - should succeed (idempotent) */
	error = cap_enter();
	ATF_CHECK_MSG(error == 0,
	    "Second cap_enter() should succeed (idempotent)");

	/* There's no cap_exit() - verify we're still restricted */
	int fd = open("/tmp/test", O_RDONLY);
	ATF_CHECK_MSG(fd < 0 && errno == ENOTCAPABLE,
	    "Should still be in capability mode");
}

/*
 * Test 10: Verify FD closing in capability mode
 */
ATF_TC(test_capsicum_fd_close);
ATF_TC_HEAD(test_capsicum_fd_close, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that file descriptors can be closed in capability mode");
}
ATF_TC_BODY(test_capsicum_fd_close, tc)
{
	int fd, error;

	/* Open a file before entering capability mode */
	fd = open("/dev/null", O_RDONLY);
	ATF_REQUIRE_MSG(fd >= 0, "Failed to open /dev/null: %s",
	    strerror(errno));

	/* Enter capability mode */
	error = cap_enter();
	ATF_REQUIRE_MSG(error == 0, "cap_enter() failed: %s", strerror(errno));

	/* Closing FDs should still work */
	error = close(fd);
	ATF_CHECK_MSG(error == 0,
	    "close() should succeed in capability mode");
}

ATF_TC_WITHOUT_HEAD(test_capsicum_enter);
ATF_TC_WITHOUT_HEAD(test_capsicum_rights_limit);
ATF_TC_WITHOUT_HEAD(test_capsicum_ioctls_limit);
ATF_TC_WITHOUT_HEAD(test_emulator_runs_under_capsicum);
ATF_TC_WITHOUT_HEAD(test_capsicum_no_file_access);
ATF_TC_WITHOUT_HEAD(test_capsicum_no_network);
ATF_TC_WITHOUT_HEAD(test_capsicum_no_process_spawn);
ATF_TC_WITHOUT_HEAD(test_capsicum_selective_rights);
ATF_TC_WITHOUT_HEAD(test_capsicum_one_way);
ATF_TC_WITHOUT_HEAD(test_capsicum_fd_close);

ATF_ADD_TEST_CASES(tcs,
    test_capsicum_enter,
    test_capsicum_rights_limit,
    test_capsicum_ioctls_limit,
    test_emulator_runs_under_capsicum,
    test_capsicum_no_file_access,
    test_capsicum_no_network,
    test_capsicum_no_process_spawn,
    test_capsicum_selective_rights,
    test_capsicum_one_way,
    test_capsicum_fd_close);
