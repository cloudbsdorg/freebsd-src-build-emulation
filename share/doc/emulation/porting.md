# Porting Guide: Adding New Architecture Support

This guide explains how to add support for a new CPU architecture to the FreeBSD emulation framework.

## Overview

Adding a new architecture involves:

1. Creating a new kernel module (`emu_<arch>.ko`)
2. Implementing the architecture-specific callbacks
3. Adding device emulations
4. Creating test cases
5. Updating documentation

## Step 1: Create Module Structure

Create a new directory for your architecture module:

```bash
mkdir -p sys/modules/emu_<arch>
mkdir -p sys/emulation/emu_<arch>
```

## Step 2: Create Module Makefile

Create `sys/modules/emu_<arch>/Makefile`:

```makefile
#
# Emulation Module for <ARCH> Architecture
#

KMOD=   emu_<arch>
SRCS=   emu_<arch>_mod.c

MODULE_DEPEND(emu_<arch>, emu_core, 1, 1, 1)

.include <bsd.kmod.mk>
```

## Step 3: Create Module Source

Create `sys/emulation/emu_<arch>/emu_<arch>_mod.c`:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <your@email.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/_types.h>

#include "emu.h"
#include "emu_module.h"

#define EMU_<ARCH>_VERSION 1

static int emu_<arch>_init(void);
static void emu_<arch>_fini(void);

static int emu_<arch>_cpu_reset(struct emu_instance *inst);
static int emu_<arch>_cpu_step(struct emu_instance *inst);
static int emu_<arch>_cpu_interrupt(struct emu_instance *inst, int irq);

static int emu_<arch>_memory_read(struct emu_instance *inst,
    uint64_t gpa, void *buf, size_t size);
static int emu_<arch>_memory_write(struct emu_instance *inst,
    uint64_t gpa, const void *buf, size_t size);

static int emu_<arch>_get_register(struct emu_instance *inst,
    int reg, uint64_t *value);
static int emu_<arch>_set_register(struct emu_instance *inst,
    int reg, uint64_t value);

static struct emu_arch_ops emu_<arch>_ops = {
    .init = emu_<arch>_init,
    .fini = emu_<arch>_fini,
    .cpu_reset = emu_<arch>_cpu_reset,
    .cpu_step = emu_<arch>_cpu_step,
    .cpu_interrupt = emu_<arch>_cpu_interrupt,
    .memory_read = emu_<arch>_memory_read,
    .memory_write = emu_<arch>_memory_write,
    .get_register = emu_<arch>_get_register,
    .set_register = emu_<arch>_set_register,
};

static int
emu_<arch>_init(void)
{
    /* Initialize architecture-specific state */
    return (0);
}

static void
emu_<arch>_fini(void)
{
    /* Clean up architecture-specific state */
}

static int
emu_<arch>_cpu_reset(struct emu_instance *inst)
{
    /* Reset CPU state to initial values */
    return (0);
}

static int
emu_<arch>_cpu_step(struct emu_instance *inst)
{
    /*
     * Fetch, decode, and execute one instruction.
     * Update program counter and registers.
     */
    return (0);
}

static int
emu_<arch>_cpu_interrupt(struct emu_instance *inst, int irq)
{
    /* Handle interrupt/exception */
    return (0);
}

static int
emu_<arch>_memory_read(struct emu_instance *inst,
    uint64_t gpa, void *buf, size_t size)
{
    /* Read memory from guest physical address */
    return (0);
}

static int
emu_<arch>_memory_write(struct emu_instance *inst,
    uint64_t gpa, const void *buf, size_t size)
{
    /* Write memory to guest physical address */
    return (0);
}

static int
emu_<arch>_get_register(struct emu_instance *inst,
    int reg, uint64_t *value)
{
    /* Get register value */
    return (0);
}

static int
emu_<arch>_set_register(struct emu_instance *inst,
    int reg, uint64_t value)
{
    /* Set register value */
    return (0);
}

static int
emu_<arch>_modevent(module_t mod, int type, void *data)
{
    int error = 0;

    switch (type) {
    case MOD_LOAD:
        /* Register with core framework */
        error = emu_arch_register("<arch>", EMU_<ARCH>_VERSION,
            &emu_<arch>_ops);
        if (error == 0) {
            printf("EMU <ARCH>: loaded\n");
        }
        break;

    case MOD_UNLOAD:
        /* Deregister from core framework */
        emu_arch_deregister("<arch>");
        printf("EMU <ARCH>: unloaded\n");
        break;

    default:
        error = EOPNOTSUPP;
        break;
    }

    return (error);
}

static moduledata_t emu_<arch>_mod = {
    "emu_<arch>",
    emu_<arch>_modevent,
    NULL
};

DECLARE_MODULE(emu_<arch>, emu_<arch>_mod, SI_SUB_EMULATION, 
    SI_ORDER_ANY);
MODULE_DEPEND(emu_<arch>, emu_core, 1, 1, 1);
MODULE_VERSION(emu_<arch>, EMU_<ARCH>_VERSION);
```

## Step 4: Implement CPU Emulation

Create CPU-specific source files:

- `emu_<arch>_decode.c` - Instruction decoding
- `emu_<arch>_exec.c` - Instruction execution
- `emu_<arch>_regs.c` - Register file implementation
- `emu_<arch>_mmu.c` - Memory management unit

### Instruction Decoding

```c
/* emu_<arch>_decode.c */

enum emu_<arch>_op {
    OP_NOP,
    OP_MOV,
    OP_ADD,
    OP_SUB,
    OP_LOAD,
    OP_STORE,
    OP_BRANCH,
    /* ... add more opcodes ... */
};

struct emu_<arch>_instruction {
    enum emu_<arch>_op op;
    uint32_t raw;
    /* decoded operands */
};

int
emu_<arch>_decode(struct emu_<arch>_cpu *cpu, uint32_t insn,
    struct emu_<arch>_instruction *decoded)
{
    /* Parse instruction encoding */
    decoded->raw = insn;
    
    switch (insn >> 26) {
    case 0x00:
        decoded->op = OP_NOP;
        break;
    case 0x01:
        decoded->op = OP_MOV;
        break;
    /* ... decode more instructions ... */
    default:
        return (EINVAL);
    }
    
    return (0);
}
```

### Instruction Execution

```c
/* emu_<arch>_exec.c */

int
emu_<arch>_exec(struct emu_<arch>_cpu *cpu,
    struct emu_<arch>_instruction *insn)
{
    switch (insn->op) {
    case OP_NOP:
        /* Do nothing */
        break;

    case OP_MOV:
        /* Copy register to register */
        break;

    case OP_ADD:
        /* Add two registers */
        break;

    case OP_LOAD:
        /* Load from memory */
        break;

    case OP_STORE:
        /* Store to memory */
        break;

    case OP_BRANCH:
        /* Modify PC */
        break;

    default:
        return (EINVAL);
    }

    return (0);
}
```

## Step 5: Add Device Emulations

### UART Device

```c
/* emu_<arch>_uart.c */

struct emu_<arch>_uart {
    struct mutex lock;
    uint8_t data;
    uint8_t control;
    bool interrupt_enabled;
};

static void
emu_<arch>_uart_write(struct emu_<arch>_uart *uart,
    uint64_t offset, uint8_t value)
{
    mtx_lock(&uart->lock);
    
    switch (offset) {
    case UART_DATA:
        uart->data = value;
        /* Signal host console */
        break;
        
    case UART_CONTROL:
        uart->control = value;
        uart->interrupt_enabled = (value & UART_CTRL_IRQ_EN);
        break;
    }
    
    mtx_unlock(&uart->lock);
}

static uint8_t
emu_<arch>_uart_read(struct emu_<arch>_uart *uart, uint64_t offset)
{
    uint8_t value;
    
    mtx_lock(&uart->lock);
    
    switch (offset) {
    case UART_DATA:
        value = uart->data;
        break;
        
    case UART_CONTROL:
        value = uart->control;
        break;
    }
    
    mtx_unlock(&uart->lock);
    return (value);
}
```

## Step 6: Add Tests

Create tests in `tests/sys/emulation/`:

```bash
tests/sys/emulation/Makefile
tests/sys/emulation/<arch>_test.c
```

```c
/* <arch>_test.c */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysent.h>

#include <atf-c.h>

/* Test basic instruction decoding */
ATF_TC(decode_nop);
ATF_TC_HEAD(decode_nop, tc)
{
    atf_tc_set_md_var(tc, "descr", "Test NOP instruction decoding");
}

ATF_TC_BODY(decode_nop, tc)
{
    /* Test NOP decoding */
}

/* Add more tests... */

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, decode_nop);
    /* Add more test cases */
    return (0);
}
```

## Step 7: Update Build System

### Update sys/modules/emu/Makefile

```makefile
MODULE_DEPEND(emu, emu_<arch>, 1, 1, 1)
```

### Update sys/conf/options

Add architecture to the build options file.

### Update Configuration Files

Update `usr.sbin/emu/emu.conf.5` and other config files to document the new architecture.

## Step 8: Update Documentation

- Add architecture to `emulation.4` man page
- Update `share/doc/emulation/README.md`
- Add architecture to `emu.8` man page

## Testing Checklist

1. **Unit Tests**
   - [ ] Instruction decoding
   - [ ] Register access
   - [ ] Memory access

2. **Integration Tests**
   - [ ] Instance creation
   - [ ] Instance start/stop
   - [ ] Module loading

3. **Device Tests**
   - [ ] UART read/write
   - [ ] Timer interrupts
   - [ ] Interrupt handling

4. **Stress Tests**
   - [ ] Memory exhaustion
   - [ ] Concurrent instances
   - [ ] CPU resource limits

## Example: Adding RISC-V Support

See existing implementations for reference:

- `sys/emulation/emu_riscv/` - RISC-V architecture module
- `sys/modules/emu_riscv/` - RISC-V module build

## Getting Help

- FreeBSD Emulation Framework Issues: https://github.com/freebsd/freebsd-src/issues
- FreeBSD Forums: https://forums.freebsd.org/
- #freebsd-emulation on OFTC IRC
