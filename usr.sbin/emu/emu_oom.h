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

#ifndef _EMU_OOM_H_
#define _EMU_OOM_H_

/*
 * OOM Killer Interaction (S16.1)
 *
 * Protect emulator processes from premature OOM killing.
 */

/*
 * emu_adjust_oom_score() - Adjust OOM killer score for emulator process
 *
 * Uses procctl(PROC_OOMADJ_CTL) to make the emulator process less likely
 * to be selected by the OOM killer.
 *
 * @return 0 on success, -1 on failure with errno set
 */
int emu_adjust_oom_score(void);

/*
 * emu_get_oom_score() - Get current OOM killer score for this process
 *
 * Retrieves the current OOM adjustment value for the calling process.
 *
 * @param score_out Pointer to store the OOM score value
 * @return 0 on success, -1 on failure with errno set
 */
int emu_get_oom_score(int *score_out);

/*
 * emu_reset_oom_score() - Reset OOM killer score to default
 *
 * Resets the OOM adjustment value to 0 (default).
 *
 * @return 0 on success, -1 on failure with errno set
 */
int emu_reset_oom_score(void);

#endif /* !_EMU_OOM_H_ */
