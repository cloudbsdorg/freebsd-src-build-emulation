/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 * Copyright (c) 2026 JetBrains s.r.o.
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

#ifndef _EMU_MMIO_H_
#define _EMU_MMIO_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * MMIO Validation Framework
 * 
 * Provides bounds-checked, validated MMIO access for device emulation.
 * All MMIO accesses must go through these functions to ensure:
 * - Offset is within device region bounds
 * - Access size is valid (1, 2, 4, 8 bytes)
 * - Access is properly aligned
 * - Device-specific handlers are called safely
 */

/* MMIO access size constants */
#define EMU_MMIO_SIZE_8     1
#define EMU_MMIO_SIZE_16    2
#define EMU_MMIO_SIZE_32    4
#define EMU_MMIO_SIZE_64    8

/* MMIO access direction */
#define EMU_MMIO_READ       0
#define EMU_MMIO_WRITE      1

/* MMIO error codes */
#define EMU_MMIO_OK             0
#define EMU_MMIO_ERR_BOUNDS     -1  /* Access outside device region */
#define EMU_MMIO_ERR_ALIGN      -2  /* Misaligned access */
#define EMU_MMIO_ERR_SIZE       -3  /* Invalid access size */
#define EMU_MMIO_ERR_READONLY   -4  /* Write to read-only region */
#define EMU_MMIO_ERR_NODEV      -5  /* No device registered at address */
#define EMU_MMIO_ERR_OVERFLOW   -6  /* Integer overflow in address calculation */
#define EMU_MMIO_ERR_HANDLER    -7  /* Device handler returned error */

/*
 * MMIO region descriptor
 * Each device registers one or more MMIO regions
 */
struct emu_mmio_region {
    uint64_t    mr_base;        /* Base address of region */
    uint64_t    mr_size;        /* Size of region in bytes */
    uint64_t    mr_end;         /* mr_base + mr_size (cached) */
    int         mr_flags;
#define EMU_MMIO_F_READ     0x01    /* Region supports reads */
#define EMU_MMIO_F_WRITE    0x02    /* Region supports writes */
#define EMU_MMIO_F_READONLY 0x04    /* Read-only region */
#define EMU_MMIO_F_WRITEONLY 0x08   /* Write-only region */
    
    /* Device-specific handlers */
    int (*mr_read)(void *arg, uint64_t offset, int size, uint64_t *value);
    int (*mr_write)(void *arg, uint64_t offset, int size, uint64_t value);
    void *mr_arg;             /* Argument passed to handlers */
    
    const char *mr_name;      /* Region name for debugging */
    
    STAILQ_ENTRY(emu_mmio_region) mr_link;
};

/*
 * MMIO bus context
 * Manages all registered MMIO regions
 */
struct emu_mmio_bus {
    STAILQ_HEAD(, emu_mmio_region) mb_regions;
    int         mb_region_count;
    struct mtx  mb_lock;
};

/* MMIO bus operations */
int     emu_mmio_init(struct emu_mmio_bus *bus);
void    emu_mmio_destroy(struct emu_mmio_bus *bus);
int     emu_mmio_register_region(struct emu_mmio_bus *bus,
            struct emu_mmio_region *region);
int     emu_mmio_unregister_region(struct emu_mmio_bus *bus,
            struct emu_mmio_region *region);

/* Validated MMIO access functions */
int     emu_mmio_read8(struct emu_mmio_bus *bus, uint64_t addr, uint8_t *val);
int     emu_mmio_read16(struct emu_mmio_bus *bus, uint64_t addr, uint16_t *val);
int     emu_mmio_read32(struct emu_mmio_bus *bus, uint64_t addr, uint32_t *val);
int     emu_mmio_read64(struct emu_mmio_bus *bus, uint64_t addr, uint64_t *val);
int     emu_mmio_write8(struct emu_mmio_bus *bus, uint64_t addr, uint8_t val);
int     emu_mmio_write16(struct emu_mmio_bus *bus, uint64_t addr, uint16_t val);
int     emu_mmio_write32(struct emu_mmio_bus *bus, uint64_t addr, uint32_t val);
int     emu_mmio_write64(struct emu_mmio_bus *bus, uint64_t addr, uint64_t val);

/* Helper functions */
int     emu_mmio_check_alignment(uint64_t addr, int size);
int     emu_mmio_check_bounds(uint64_t addr, int size, uint64_t base, uint64_t end);
const char *emu_mmio_strerror(int error);

#endif /* !_EMU_MMIO_H_ */
