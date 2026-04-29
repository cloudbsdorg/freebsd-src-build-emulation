/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 FreeBSD Foundation
 *
 * This software is developed by FreeBSD Foundation.
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

#ifndef _EMU_SCRUB_H_
#define _EMU_SCRUB_H_

/*
 * Memory scrubbing interface for FreeBSD Kernel Emulation Framework
 */

/* Scrubbing methods */
#define EMU_SCRUB_METHOD_ZERO		0
#define EMU_SCRUB_METHOD_RANDOM		1
#define EMU_SCRUB_METHOD_PATTERN	2

/*
 * Initialize memory scrubbing subsystem
 */
void emu_scrub_init(void);

/*
 * Destroy memory scrubbing subsystem
 */
void emu_scrub_destroy(void);

/*
 * Set/get scrubbing method
 */
int emu_scrub_set_method(int method);
int emu_scrub_get_method(void);

/*
 * Set scrubbing pattern (for pattern method)
 */
int emu_scrub_set_pattern(const uint8_t *pattern, int len);

/*
 * Enable/disable scrubbing
 */
int emu_scrub_set_enabled(int enabled);
int emu_scrub_is_enabled(void);

/*
 * Scrub memory region using configured method
 */
void emu_scrub_memory(void *addr, size_t len);

/*
 * Scrub memory with specific method (override global config)
 */
void emu_scrub_memory_method(void *addr, size_t len, int method);

#endif /* !_EMU_SCRUB_H_ */
