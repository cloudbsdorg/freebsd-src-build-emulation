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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <machine/stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "emu_mmio.h"

/*
 * MMIO Validation Framework Implementation
 * 
 * Provides bounds-checked, validated MMIO access for device emulation.
 * Security: All MMIO accesses are validated before reaching device handlers.
 */

MALLOC_DEFINE(M_EMU_MMIO, "emu_mmio", "MMIO region descriptors");

/*
 * Initialize MMIO bus
 */
int
emu_mmio_init(struct emu_mmio_bus *bus)
{
    if (bus == NULL)
        return (EINVAL);
    
    memset(bus, 0, sizeof(*bus));
    STAILQ_INIT(&bus->mb_regions);
    mtx_init(&bus->mb_lock, "emu_mmio_bus", NULL, MTX_DEF);
    bus->mb_region_count = 0;
    
    return (0);
}

/*
 * Destroy MMIO bus and free all regions
 */
void
emu_mmio_destroy(struct emu_mmio_bus *bus)
{
    struct emu_mmio_region *region, *temp;
    
    if (bus == NULL)
        return;
    
    mtx_lock(&bus->mb_lock);
    
    /* Free all registered regions */
    STAILQ_FOREACH_SAFE(region, &bus->mb_regions, mr_link, temp) {
        STAILQ_REMOVE(&bus->mb_regions, region, emu_mmio_region, mr_link);
        bus->mb_region_count--;
        free(region, M_EMU_MMIO);
    }
    
    mtx_unlock(&bus->mb_lock);
    mtx_destroy(&bus->mb_lock);
}

/*
 * Register an MMIO region
 */
int
emu_mmio_register_region(struct emu_mmio_bus *bus, struct emu_mmio_region *region)
{
    int error;
    
    if (bus == NULL || region == NULL)
        return (EINVAL);
    
    /* Validate region parameters */
    if (region->mr_size == 0)
        return (EINVAL);
    
    /* Check for overflow in end calculation */
    if (region->mr_base + region->mr_size < region->mr_base)
        return (EOVERFLOW);
    
    region->mr_end = region->mr_base + region->mr_size;
    
    /* Set default flags if none provided */
    if (region->mr_flags == 0)
        region->mr_flags = EMU_MMIO_F_READ | EMU_MMIO_F_WRITE;
    
    /* Require handlers for the allowed operations */
    if ((region->mr_flags & EMU_MMIO_F_READ) && region->mr_read == NULL)
        return (EINVAL);
    if ((region->mr_flags & EMU_MMIO_F_WRITE) && region->mr_write == NULL)
        return (EINVAL);
    
    mtx_lock(&bus->mb_lock);
    
    /* Check for overlapping regions */
    struct emu_mmio_region *existing;
    STAILQ_FOREACH(existing, &bus->mb_regions, mr_link) {
        if (region->mr_base < existing->mr_end && 
            region->mr_end > existing->mr_base) {
            mtx_unlock(&bus->mb_lock);
            return (EEXIST);
        }
    }
    
    /* Add to list */
    STAILQ_INSERT_TAIL(&bus->mb_regions, region, mr_link);
    bus->mb_region_count++;
    
    mtx_unlock(&bus->mb_lock);
    
    return (0);
}

/*
 * Unregister an MMIO region
 */
int
emu_mmio_unregister_region(struct emu_mmio_bus *bus, struct emu_mmio_region *region)
{
    if (bus == NULL || region == NULL)
        return (EINVAL);
    
    mtx_lock(&bus->mb_lock);
    
    STAILQ_REMOVE(&bus->mb_regions, region, emu_mmio_region, mr_link);
    bus->mb_region_count--;
    
    mtx_unlock(&bus->mb_lock);
    
    return (0);
}

/*
 * Find region containing address
 * Must be called with bus lock held
 */
static struct emu_mmio_region *
emu_mmio_find_region(struct emu_mmio_bus *bus, uint64_t addr)
{
    struct emu_mmio_region *region;
    
    STAILQ_FOREACH(region, &bus->mb_regions, mr_link) {
        if (addr >= region->mr_base && addr < region->mr_end)
            return (region);
    }
    
    return (NULL);
}

/*
 * Check alignment for access size
 */
int
emu_mmio_check_alignment(uint64_t addr, int size)
{
    switch (size) {
    case 1:
        return (0);  /* Any alignment for byte access */
    case 2:
        return ((addr & 1) ? EMU_MMIO_ERR_ALIGN : 0);
    case 4:
        return ((addr & 3) ? EMU_MMIO_ERR_ALIGN : 0);
    case 8:
        return ((addr & 7) ? EMU_MMIO_ERR_ALIGN : 0);
    default:
        return (EMU_MMIO_ERR_SIZE);
    }
}

/*
 * Check bounds for access
 */
int
emu_mmio_check_bounds(uint64_t addr, int size, uint64_t base, uint64_t end)
{
    uint64_t access_end;
    
    /* Check for overflow */
    if (addr + size < addr)
        return (EMU_MMIO_ERR_OVERFLOW);
    
    access_end = addr + size;
    
    /* Check if access is within region */
    if (addr < base || access_end > end)
        return (EMU_MMIO_ERR_BOUNDS);
    
    return (0);
}

/*
 * Read MMIO region
 */
static int
emu_mmio_region_read(struct emu_mmio_region *region, uint64_t offset, 
    int size, uint64_t *value)
{
    int error;
    
    if (region == NULL || value == NULL)
        return (EINVAL);
    
    /* Check if region supports reads */
    if (!(region->mr_flags & EMU_MMIO_F_READ))
        return (EMU_MMIO_ERR_READONLY);
    
    /* Call device handler */
    error = region->mr_read(region->mr_arg, offset, size, value);
    if (error != 0)
        return (EMU_MMIO_ERR_HANDLER);
    
    return (0);
}

/*
 * Write MMIO region
 */
static int
emu_mmio_region_write(struct emu_mmio_region *region, uint64_t offset,
    int size, uint64_t value)
{
    int error;
    
    if (region == NULL)
        return (EINVAL);
    
    /* Check if region supports writes */
    if (!(region->mr_flags & EMU_MMIO_F_WRITE))
        return (EMU_MMIO_ERR_READONLY);
    
    /* Call device handler */
    error = region->mr_write(region->mr_arg, offset, size, value);
    if (error != 0)
        return (EMU_MMIO_ERR_HANDLER);
    
    return (0);
}

/*
 * Generic MMIO read
 */
static int
emu_mmio_read(struct emu_mmio_bus *bus, uint64_t addr, int size, uint64_t *value)
{
    struct emu_mmio_region *region;
    uint64_t offset;
    int error;
    
    if (bus == NULL || value == NULL)
        return (EINVAL);
    
    /* Validate access size */
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return (EMU_MMIO_ERR_SIZE);
    
    /* Check alignment */
    error = emu_mmio_check_alignment(addr, size);
    if (error != 0)
        return (error);
    
    mtx_lock(&bus->mb_lock);
    
    /* Find region */
    region = emu_mmio_find_region(bus, addr);
    if (region == NULL) {
        mtx_unlock(&bus->mb_lock);
        return (EMU_MMIO_ERR_NODEV);
    }
    
    /* Check bounds within region */
    error = emu_mmio_check_bounds(addr, size, region->mr_base, region->mr_end);
    if (error != 0) {
        mtx_unlock(&bus->mb_lock);
        return (error);
    }
    
    /* Calculate offset within region */
    offset = addr - region->mr_base;
    
    /* Read from region */
    error = emu_mmio_region_read(region, offset, size, value);
    
    mtx_unlock(&bus->mb_lock);
    
    return (error);
}

/*
 * Generic MMIO write
 */
static int
emu_mmio_write(struct emu_mmio_bus *bus, uint64_t addr, int size, uint64_t value)
{
    struct emu_mmio_region *region;
    uint64_t offset;
    int error;
    
    if (bus == NULL)
        return (EINVAL);
    
    /* Validate access size */
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return (EMU_MMIO_ERR_SIZE);
    
    /* Check alignment */
    error = emu_mmio_check_alignment(addr, size);
    if (error != 0)
        return (error);
    
    mtx_lock(&bus->mb_lock);
    
    /* Find region */
    region = emu_mmio_find_region(bus, addr);
    if (region == NULL) {
        mtx_unlock(&bus->mb_lock);
        return (EMU_MMIO_ERR_NODEV);
    }
    
    /* Check bounds within region */
    error = emu_mmio_check_bounds(addr, size, region->mr_base, region->mr_end);
    if (error != 0) {
        mtx_unlock(&bus->mb_lock);
        return (error);
    }
    
    /* Calculate offset within region */
    offset = addr - region->mr_base;
    
    /* Write to region */
    error = emu_mmio_region_write(region, offset, size, value);
    
    mtx_unlock(&bus->mb_lock);
    
    return (error);
}

/*
 * 8-bit MMIO read
 */
int
emu_mmio_read8(struct emu_mmio_bus *bus, uint64_t addr, uint8_t *val)
{
    uint64_t value;
    int error;
    
    if (val == NULL)
        return (EINVAL);
    
    error = emu_mmio_read(bus, addr, 1, &value);
    if (error == 0)
        *val = (uint8_t)value;
    
    return (error);
}

/*
 * 16-bit MMIO read
 */
int
emu_mmio_read16(struct emu_mmio_bus *bus, uint64_t addr, uint16_t *val)
{
    uint64_t value;
    int error;
    
    if (val == NULL)
        return (EINVAL);
    
    error = emu_mmio_read(bus, addr, 2, &value);
    if (error == 0)
        *val = (uint16_t)value;
    
    return (error);
}

/*
 * 32-bit MMIO read
 */
int
emu_mmio_read32(struct emu_mmio_bus *bus, uint64_t addr, uint32_t *val)
{
    uint64_t value;
    int error;
    
    if (val == NULL)
        return (EINVAL);
    
    error = emu_mmio_read(bus, addr, 4, &value);
    if (error == 0)
        *val = (uint32_t)value;
    
    return (error);
}

/*
 * 64-bit MMIO read
 */
int
emu_mmio_read64(struct emu_mmio_bus *bus, uint64_t addr, uint64_t *val)
{
    uint64_t value;
    int error;
    
    if (val == NULL)
        return (EINVAL);
    
    error = emu_mmio_read(bus, addr, 8, &value);
    if (error == 0)
        *val = value;
    
    return (error);
}

/*
 * 8-bit MMIO write
 */
int
emu_mmio_write8(struct emu_mmio_bus *bus, uint64_t addr, uint8_t val)
{
    return (emu_mmio_write(bus, addr, 1, (uint64_t)val));
}

/*
 * 16-bit MMIO write
 */
int
emu_mmio_write16(struct emu_mmio_bus *bus, uint64_t addr, uint16_t val)
{
    return (emu_mmio_write(bus, addr, 2, (uint64_t)val));
}

/*
 * 32-bit MMIO write
 */
int
emu_mmio_write32(struct emu_mmio_bus *bus, uint64_t addr, uint32_t val)
{
    return (emu_mmio_write(bus, addr, 4, (uint64_t)val));
}

/*
 * 64-bit MMIO write
 */
int
emu_mmio_write64(struct emu_mmio_bus *bus, uint64_t addr, uint64_t val)
{
    return (emu_mmio_write(bus, addr, 8, val));
}

/*
 * Convert MMIO error code to string
 */
const char *
emu_mmio_strerror(int error)
{
    switch (error) {
    case EMU_MMIO_OK:
        return ("Success");
    case EMU_MMIO_ERR_BOUNDS:
        return ("Access outside device region");
    case EMU_MMIO_ERR_ALIGN:
        return ("Misaligned access");
    case EMU_MMIO_ERR_SIZE:
        return ("Invalid access size");
    case EMU_MMIO_ERR_READONLY:
        return ("Write to read-only region");
    case EMU_MMIO_ERR_NODEV:
        return ("No device registered at address");
    case EMU_MMIO_ERR_OVERFLOW:
        return ("Integer overflow in address calculation");
    case EMU_MMIO_ERR_HANDLER:
        return ("Device handler returned error");
    default:
        return ("Unknown MMIO error");
    }
}
