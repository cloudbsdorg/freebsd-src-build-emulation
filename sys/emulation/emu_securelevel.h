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

#ifndef _SYS_EMU_SECURELEVEL_H_
#define	_SYS_EMU_SECURELEVEL_H_

/*
 * Emulation Framework Securelevel Integration Header
 *
 * This header provides securelevel-aware operation restrictions
 * for the emulation framework.
 */

#ifdef _KERNEL

/*
 * Check if the current securelevel restricts an operation
 *
 * Parameters:
 *   td    - Thread requesting the operation
 *   level - Securelevel threshold (0-3)
 *
 * Returns:
 *   0 if the operation is allowed
 *   EPERM if the securelevel restricts the operation
 */
int	emu_securelevel_check(struct thread *td, int level);

/*
 * Check if a restricted operation is allowed at current securelevel
 *
 * Parameters:
 *   td  - Thread requesting the operation
 *   op  - Operation name (for auditing)
 *
 * Returns:
 *   0 if the operation is allowed
 *   EPERM if the securelevel restricts the operation
 */
int	emu_securelevel_restricted_op(struct thread *td, const char *op);

#endif /* _KERNEL */

#endif /* !_SYS_EMU_SECURELEVEL_H_ */
