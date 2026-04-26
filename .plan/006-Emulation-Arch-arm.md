# ARM (32-bit) Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | ARMv7-A (ARM), with Thumb/Thumb-2 support |
| **Base specification** | ARM Architecture Reference Manual ARMv7-A (DDI0406) |
| **FreeBSD support** | arm (32-bit, ARMv7) |
| **Emulation priority** | P1 |
| **bhyve support** | ❌ |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_arm.ko` |

### 1.1 CPU Levels / Feature Tiers

| CPU Level | Base ISA | Key Features | FreeBSD Target |
|-----------|----------|--------------|----------------|
| `armv4` | ARMv4 | ARM ISA, SWP, BLX | Legacy |
| `armv5` | ARMv4 + | BLX (immediate), CLZ, BKPT, enhanced DSP | XScale, ARM9 |
| `armv6` | ARMv5 + | Thumb, SIMD, VFPv2, MMU with PIPT | ARM11 |
| `armv7-a` | ARMv6 + | Thumb-2, NEON, VFPv3, virtualization | FreeBSD arm (primary) |
| `armv7-r` | ARMv7-A subset | Real-time profile, MPU (no MMU) | Real-time |
| `armv7-m` | ARMv7-A subset | Microcontroller profile, no MMU | Embedded |

**Default CPU level:** `armv7-a` (FreeBSD arm target)

**Instance configuration field:** `cpu_level` (string, e.g., "armv7-a", "armv6")

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers

| Register | Width | Purpose |
|----------|-------|---------|
| R0-R3 | 32-bit | Argument registers (caller-saved), return value in R0 |
| R4-R10 | 32-bit | Callee-saved registers |
| R11 (FP) | 32-bit | Frame pointer (callee-saved) |
| R12 (IP) | 32-bit | Intra-procedure call scratch register |
| R13 (SP) | 32-bit | Stack pointer (banked: SVC, IRQ, FIQ, ABT, UND, SYS, MON) |
| R14 (LR) | 32-bit | Link register (banked per mode) |
| R15 (PC) | 32-bit | Program counter |
| CPSR | 32-bit | Current Program Status Register |
| SPSR_* | 32-bit | Saved Program Status Register (banked per mode) |

**CPSR fields:**
- N (bit 31): Negative condition flag
- Z (bit 30): Zero condition flag
- C (bit 29): Carry condition flag
- V (bit 28): Overflow condition flag
- Q (bit 27): Saturation flag (DSP extensions)
- IT (bits 15-10): If-Then block state (Thumb-2)
- GE (bits 19-16): Greater-than-or-Equal flags (SIMD)
- E (bit 9): Endianness (0=little, 1=big)
- A (bit 8): Imprecise abort mask
- I (bit 7): IRQ mask
- F (bit 6): FIQ mask
- T (bit 5): Thumb state (0=ARM, 1=Thumb)
- M[4:0] (bits 4-0): Mode (10000=User, 10001=FIQ, 10010=IRQ, 10011=Supervisor, 10111=Abort, 11011=Undefined, 11111=System, 10110=Monitor)

### 2.2 Instruction Categories

| Category | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **Data processing** | AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN | P0 | 16 core ALU operations |
| **Data processing (immediate)** | Same as above with #imm operand | P0 | Immediate forms |
| **Data processing (shifted register)** | Same as above with shifted register operand | P0 | LSL, LSR, ASR, ROR, RRX shifts |
| **Multiply** | MUL, MLA, MLS, UMULL, UMLAL, SMULL, SMLAL, SMULxy, SMLAxy, SMLALxy, SMULWy, SMLAWy | P0 | 32-bit and 64-bit multiply |
| **Load/store (single)** | LDR, STR, LDRB, STRB, LDRH, STRH, LDRSB, LDRSH | P0 | Single register load/store |
| **Load/store (multiple)** | LDM, STM (IA, IB, DA, DB) | P0 | Block load/store |
| **Load/store (exclusive)** | LDREX, STREX, LDREXB, STREXB, LDREXH, STREXH, LDREXD, STREXD | P0 | Exclusive access |
| **Branch** | B, BL, BX, BLX, BXJ | P0 | Branch and link |
| **Conditional branch** | BEQ, BNE, BCS/BHS, BCC/BLO, BMI, BPL, BVS, BVC, BHI, BLS, BGE, BGT, BLE, BAL | P0 | All 14 condition codes |
| **Coprocessor** | CDP, LDC, STC, MCR, MRC | P1 | System coprocessor access |
| **System register** | MRS, MSR | P0 | Read/write CPSR, SPSR |
| **Exception** | SVC (formerly SWI), BKPT | P0 | System calls, breakpoints |
| **Miscellaneous** | CLZ, BFC, BFI, SBFX, UBFX, SXTB, SXTH, UXTB, UXTH, REV, REV16, REVSH, RBIT | P0 | Bit manipulation |
| **SIMD (NEON)** | VADD, VSUB, VMUL, VMLA, VMLS, VABS, VNEG, VCEQ, VCGT, VCGE, VAND, VORR, VEOR, VBIC, VORN, VBSL, VLD1, VST1, VLD2, VST2, VLD3, VST3, VLD4, VST4, VMOV, VDUP, VSWP, VTRN, VZIP, VUZP, VREV, VEXT, VSHL, VSHR, VSRA, VRSHR, VRSRA, VSRI, VSLI | P1 | Advanced SIMD |
| **VFP** | FADD, FSUB, FMUL, FDIV, FSQRT, FCMP, FCMPE, FABS, FNEG, FCPY, FMOV, FUITO, FSITO, FTOUI, FTOSI, FCVT, FMSR, FMRS, FMDRR, FMRRD, FSTS, FLDS, FSTD, FLDD | P1 | Floating-point |
| **Thumb (16-bit)** | LSL, LSR, ASR, ADD, SUB, MOV, CMP, AND, EOR, LDR, STR, LDRB, STRB, LDRH, STRH, LDRSB, LDRSH, PUSH, POP, B, BX, BLX, SVC, IT | P0 | 16-bit Thumb instructions |
| **Thumb-2 (32-bit)** | Extended versions of ARM instructions in 32-bit Thumb encoding | P0 | Mixed 16/32-bit Thumb |

### 2.3 Instruction Encoding

**ARM mode:** Fixed 32-bit encoding:

| Bit Range | Field | Description |
|-----------|-------|-------------|
| 31-28 | Cond | Condition code (4 bits, 0xE=AL always) |
| 27-25 | Op | Primary opcode group |
| 24-21 | Func | Function/sub-opcode |
| 20 | S | Set condition codes |
| 19-16 | Rn | First source register |
| 15-12 | Rd | Destination register |
| 11-0 | Operand2 | Second operand (register + shift, or immediate) |

**Thumb mode:** Mixed 16-bit and 32-bit encoding:
- 16-bit instructions: 0x0000-0xFFFF
- 32-bit instructions: 0xE800-0xFFFF (first halfword indicates 32-bit)

**Implementation approach:** Decode using a table-driven approach with:
- ARM mode: 4-bit condition + 3-bit opcode group dispatch
- Thumb mode: First halfword determines 16-bit vs 32-bit, then table dispatch

---

## 3. MMU / Paging

### 3.1 ARMv7-A MMU

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (small page), 64KB (large page), 1MB (section), 16MB (supersection) |
| **Page table levels** | 1-level (section) or 2-level (page table walk) |
| **Virtual address bits** | 32-bit |
| **Physical address bits** | 32-bit (or 40-bit with LPAE) |
| **TTBCR** | Translation Table Base Control Register |
| **TTBR0** | Translation Table Base Register 0 (user space) |
| **TTBR1** | Translation Table Base Register 1 (kernel space) |
| **DACR** | Domain Access Control Register |

**First-level descriptor (section/page table):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | Type | 0=fault, 1=page table, 2=section (1MB), 3=supersection (16MB, with bit 18) |
| 1-17 | | Various fields depending on type |
| 18 | Supersection | 0=section, 1=supersection (if type=2) |
| 19-31 | Domain | Domain field (for access control) |

**Second-level descriptor (4KB small page):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | Type | 0=fault, 1=large page (64KB), 2=small page (4KB), 3=reserved |
| 1-31 | PhysAddr | Physical address (with APX, AP, C, B bits) |

### 3.2 LPAE (Large Physical Address Extension)

Same page table format as AArch64 (see `005-Emulation-Arch-arm64.md` Section 3.1), but with 32-bit virtual address input.

---

## 4. Interrupt and Exception Model

### 4.1 Processor Modes

| Mode | Abbrev | CPSR M[4:0] | Stack | Purpose |
|------|--------|-------------|-------|---------|
| User | USR | 10000 | SP_usr | Normal application code |
| FIQ | FIQ | 10001 | SP_fiq, LR_fiq | Fast interrupt |
| IRQ | IRQ | 10010 | SP_irq, LR_irq | Normal interrupt |
| Supervisor | SVC | 10011 | SP_svc, LR_svc | Supervisor call (SVC instruction) |
| Monitor | MON | 10110 | SP_mon, LR_mon | Secure monitor |
| Abort | ABT | 10111 | SP_abt, LR_abt | Data/prefetch abort |
| Undefined | UND | 11011 | SP_und, LR_und | Undefined instruction |
| System | SYS | 11111 | SP_usr | Privileged task (same SP as User) |

### 4.2 Exception Vectors

| Vector Address | Exception | Mode | Description |
|----------------|-----------|------|-------------|
| 0x00000000 | Reset | SVC | Power-on reset |
| 0x00000004 | Undefined Instruction | UND | Undefined opcode |
| 0x00000008 | SVC (SWI) | SVC | Supervisor call |
| 0x0000000C | Prefetch Abort | ABT | Instruction fetch fault |
| 0x00000010 | Data Abort | ABT | Data access fault |
| 0x00000014 | Reserved | — | Reserved (used for Hypervisor on ARMv7) |
| 0x00000018 | IRQ | IRQ | Normal interrupt |
| 0x0000001C | FIQ | FIQ | Fast interrupt |

**High vectors:** If SCTLR.V=1, vectors are at 0xFFFF0000 instead of 0x00000000.

### 4.3 Generic Interrupt Controller (GIC)

Same GIC architecture as arm64 (see `005-Emulation-Arch-arm64.md` Section 4.4), but typically GICv2 rather than GICv3 on 32-bit ARM platforms.

---

## 5. Boot Process

### 5.1 FreeBSD Boot Sequence (arm)

1. **Boot loader** (UBoot or loader) — loaded from flash/SD by SoC boot ROM
2. **loader** — loads kernel, modules, reads /boot/loader.conf
3. **kernel** — ELF binary loaded at appropriate DRAM address
4. **Kernel initialization:**
   - Entry point: `start` (locore.S) — set up page tables, enable MMU
   - `mi_startup()` — main initialization

### 5.2 Required Emulated Devices for Boot

| Device | Type | Address | Purpose |
|--------|------|---------|---------|
| **UART (PL011)** | Serial | 0x9000000 | Console output |
| **GIC** | Interrupt | 0x2F000000 | Interrupt controller |
| **Timer (SP804)** | Timer | 0x1C110000 | System timer |
| **virtio-blk** | Storage | MMIO | Disk access |
| **RTC (PL031)** | Timer | 0x1C170000 | Real-time clock |

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| ARM.1 | Implement ARM mode instruction decoder (32-bit) | NOT STARTED | | | `sys/emulation/arm/emu_cpu_arm.c` | Decode fixed 32-bit ARM instructions. Condition code + opcode group dispatch. |
| ARM.2 | Implement Thumb mode instruction decoder (16-bit) | NOT STARTED | | ARM.1 | `sys/emulation/arm/emu_cpu_arm.c` | Decode 16-bit Thumb instructions. LSL, LSR, ASR, ADD, SUB, MOV, CMP, AND, EOR, LDR, STR, PUSH, POP, B, BX, SVC, IT. |
| ARM.3 | Implement Thumb-2 instruction decoder (32-bit) | NOT STARTED | | ARM.2 | `sys/emulation/arm/emu_cpu_arm.c` | Decode 32-bit Thumb-2 instructions. First halfword 0xE800-0xFFFF. |
| ARM.4 | Implement data processing instructions (P0) | NOT STARTED | | ARM.1 | `sys/emulation/arm/emu_cpu_arm.c` | AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN |
| ARM.5 | Implement load/store instructions (P0) | NOT STARTED | | ARM.4 | `sys/emulation/arm/emu_cpu_arm.c` | LDR, STR, LDRB, STRB, LDRH, STRH, LDRSB, LDRSH, LDM, STM |
| ARM.6 | Implement branch instructions (P0) | NOT STARTED | | ARM.4 | `sys/emulation/arm/emu_cpu_arm.c` | B, BL, BX, BLX. All 14 condition codes. |
| ARM.7 | Implement multiply instructions (P0) | NOT STARTED | | ARM.4 | `sys/emulation/arm/emu_cpu_arm.c` | MUL, MLA, MLS, UMULL, UMLAL, SMULL, SMLAL |
| ARM.8 | Implement exclusive access instructions (P0) | NOT STARTED | | ARM.5 | `sys/emulation/arm/emu_cpu_arm.c` | LDREX, STREX, LDREXB, STREXB, LDREXH, STREXH, LDREXD, STREXD |
| ARM.9 | Implement system register access (P0) | NOT STARTED | | ARM.4 | `sys/emulation/arm/emu_cpu_arm.c` | MRS, MSR. CPSR, SPSR read/write. |
| ARM.10 | Implement exception generation (P0) | NOT STARTED | | ARM.6 | `sys/emulation/arm/emu_cpu_arm.c` | SVC (SWI), BKPT |
| ARM.11 | Implement ARMv7-A MMU (P0) | NOT STARTED | | | `sys/emulation/arm/emu_mmu_arm.c` | Section/page table walk. 4KB, 64KB, 1MB, 16MB pages. Domain access control. |
| ARM.12 | Implement LPAE page tables (P1) | NOT STARTED | | ARM.11 | `sys/emulation/arm/emu_mmu_arm.c` | 40-bit physical address. Same format as AArch64. |
| ARM.13 | Implement exception handling (P0) | NOT STARTED | | ARM.10 | `sys/emulation/arm/emu_intr_arm.c` | Vector table at 0x00000000 or 0xFFFF0000. Mode switching. Banked registers. |
| ARM.14 | Implement GICv2 interrupt controller (P0) | NOT STARTED | | ARM.13 | `sys/emulation/arm/emu_intr_arm.c` | Distributor and CPU interface. SGI, PPI, SPI. |
| ARM.15 | Implement PL011 UART (P0) | NOT STARTED | | | `usr.sbin/emu/emu_dev_uart.c` | Shared with arm64. ARM PrimeCell UART. |
| ARM.16 | Implement U-Boot firmware loading (P0) | NOT STARTED | | ARM.1 | `usr.sbin/emu/emu_arch_arm.c` | Load U-Boot binary from blob cache via `emu_blob_resolve()`. Generate DTB. See `010-Emulation-Blob-Management.md`. |
| ARM.17 | Implement device tree generation (P0) | NOT STARTED | | ARM.16 | `usr.sbin/emu/emu_arch_arm.c` | Generate FDT for ARMv7 platform. |
| ARM.18 | Implement NEON/VFP instructions (P1) | NOT STARTED | | ARM.4 | `sys/emulation/arm/emu_cpu_arm.c` | VADD, VMUL, VLD1, VST1, FADD, FMUL, etc. |
| ARM.19 | Write ARM CPU emulation unit tests | NOT STARTED | | ARM.1-ARM.18 | `tests/sys/emulation/arm/` | Test ARM and Thumb decoders. Test data processing. Test load/store. Test branch conditions. Test MMU. Test exception handling. Test GIC. |
| ARM.20 | Write ARM boot integration test | NOT STARTED | | ARM.16 | `tests/usr.sbin/emu/arm_boot_test.sh` | Boot FreeBSD arm kernel in emulator. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 3 (Architecture-Specific CPU Emulation). |
| `002-Emulation-Security-FS.md` | Security architecture. |
| `005-Emulation-Arch-arm64.md` | ARM64 architecture. Shares GIC, PL011 UART, and device tree infrastructure. |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| ARM Architecture Reference Manual ARMv7-A (DDI0406) | https://developer.arm.com/documentation/ddi0406/ | Primary ISA reference |
| FreeBSD arm kernel entry (locore.S) | `sys/arm/arm/locore.S` | Boot sequence |
| FreeBSD arm pmap | `sys/arm/arm/pmap.c` | Page table management |
| FreeBSD arm GIC driver | `sys/arm/arm/gic.c` | GIC driver reference |
| U-Boot | https://github.com/u-boot/u-boot | Boot firmware |

---

## 8. Notes

- ARM (32-bit) has two instruction sets: ARM (fixed 32-bit) and Thumb (mixed 16/32-bit). The emulator must support both and switch between them via the CPSR.T bit.
- The ARM condition code system (14 conditions applied to most instructions) is a unique feature not found in other architectures.
- Banked registers per processor mode add complexity to the register file implementation.
- FreeBSD arm support is primarily ARMv7-A. Older ARM versions (v4, v5, v6) are lower priority.
