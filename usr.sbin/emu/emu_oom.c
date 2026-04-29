/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
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

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/procctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "emu_memmgmt.h"
#include "emu.h"

/*
 * OOM Killer Interaction (S16.1)
 *
 * Protect emulator processes from premature OOM killing.
 * Emulator processes should be among the last to be killed
 * to protect guest workloads and prevent data loss.
 */

/*
 * emu_adjust_oom_score() - Adjust OOM killer score for emulator process
 *
 * Uses procctl(PROC_OOMADJ_CTL) to make the emulator process less likely
 * to be selected by the OOM killer. This protects guest workloads and
 * prevents data loss during memory pressure situations.
 *
 * The OOM adjustment value ranges from -1000 (never kill) to 1000 (always kill).
 * We set it to OOM_SCORE_ADJ_MIN (-1000) to make emulator processes
 * the least likely to be killed.
 *
 * @return 0 on success, -1 on failure with errno set
 */
int
emu_adjust_oom_score(void)
{
	struct procctl_io io;
	int oom_adj = PROC_OOMADJ_MIN;

	memset(&io, 0, sizeof(io));
	io.procctl_cmd = PROC_OOMADJ_CTL;
	io.iov_base = &oom_adj;
	io.iov_len = sizeof(oom_adj);

	if (procctl(P_PID, getpid(), PROC_OOMADJ_CTL, &oom_adj) != 0) {
		warn("procctl(PROC_OOMADJ_CTL) failed");
		return (-1);
	}

	/* Log the adjustment for debugging */
	warnx("OOM score adjusted to %d (minimum, least likely to be killed)",
	    oom_adj);

	return (0);
}

/*
 * emu_get_oom_score() - Get current OOM killer score for this process
 *
 * Retrieves the current OOM adjustment value for the calling process.
 * Useful for debugging and verification.
 *
 * @param score_out Pointer to store the OOM score value
 * @return 0 on success, -1 on failure with errno set
 */
int
emu_get_oom_score(int *score_out)
{
	struct procctl_io io;
	int oom_adj;

	if (score_out == NULL) {
		errno = EINVAL;
		return (-1);
	}

	memset(&io, 0, sizeof(io));
	io.procctl_cmd = PROC_OOMADJ_STATUS;
	io.iov_base = &oom_adj;
	io.iov_len = sizeof(oom_adj);

	if (procctl(P_PID, getpid(), PROC_OOMADJ_STATUS, &oom_adj) != 0) {
		warn("procctl(PROC_OOMADJ_STATUS) failed");
		return (-1);
	}

	*score_out = oom_adj;
	return (0);
}

/*
 * emu_reset_oom_score() - Reset OOM killer score to default
 *
 * Resets the OOM adjustment value to 0 (default). This should be
 * called during cleanup or if the emulator is shutting down gracefully.
 *
 * @return 0 on success, -1 on failure with errno set
 */
int
emu_reset_oom_score(void)
{
	int oom_adj = 0;

	if (procctl(P_PID, getpid(), PROC_OOMADJ_CTL, &oom_adj) != 0) {
		warn("procctl(PROC_OOMADJ_CTL) reset failed");
		return (-1);
	}

	warnx("OOM score reset to default (0)");
	return (0);
}
