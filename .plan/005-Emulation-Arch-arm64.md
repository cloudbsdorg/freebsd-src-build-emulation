# ARM64 (AArch64) Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | AArch64 (ARMv8-A and later), with AArch32 (ARMv7 compatibility mode) support |
| **Base specification** | ARM Architecture Reference Manual ARMv8-A (DDI0487) |
| **FreeBSD support** | arm64 (primary target) |
| **Emulation priority** | P0 (tied with amd64 for cross-architecture testing) |
| **bhyve support** | ❌ (bhyve does not support arm64 guests) |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_aarch64.ko` |

### 1.1 CPU Levels / Feature Tiers

| CPU Level | Base ISA | Key Features | FreeBSD Target |
|-----------|----------|--------------|----------------|
| `armv8.0-a` | ARMv8.0-A | A64 ISA, 4KB/16KB/64KB pages, 48-bit VA, GICv3, virtualization extensions | Baseline arm64 |
| `armv8.1-a` | ARMv8.0-A + | LSE (atomic instructions), PAN, VHE | Modern arm64 |
| `armv8.2-a` | ARMv8.1-A + | RAS, SVE (scalar), statistical profiling | Late 2010s SoCs |
| `armv8.3-a` | ARMv8.2-A + | Pointer authentication, JS conversion, complex numbers | 2018+ SoCs |
| `armv8.4-a` | ARMv8.3-A + | SVE2, MPAM, DIT, IDST | 2020+ SoCs |
| `armv8.5-a` | ARMv8.4-A + | MTE (memory tagging), BTI (branch target), ETE/TRBE | 2021+ SoCs |
| `armv8.6-a` | ARMv8.5-A + | I8MM, BF16, AMUv1, enhanced virtualization | 2022+ SoCs |
| `armv9.0-a` | ARMv9.0-A | SVE2 base, Realm Management Extension (RME), CCA | 2023+ SoCs |
| `armv9.1-a` | ARMv9.0-A + | Enhanced SVE2, more crypto extensions | Future |
| `armv9.2-a` | ARMv9.1-A + | Further ISA extensions | Future |

**Default CPU level:** `armv8.0-a` (widest compatibility for FreeBSD arm64)

**Instance configuration field:** `cpu_level` (string, e.g., "armv8.0-a", "armv8.2-a")

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers

| Register | Width | Purpose |
|----------|-------|---------|
| X0-X7 | 64-bit | Argument registers (caller-saved), return value in X0 |
| X8 | 64-bit | Indirect result register (struct return address) |
| X9-X15 | 64-bit | Temporary registers (caller-saved) |
| X16-X17 | 64-bit | Intra-procedure call temporaries (IP0, IP1) |
| X18 | 64-bit | Platform register (TLS base on FreeBSD) |
| X19-X28 | 64-bit | Callee-saved registers |
| X29 (FP) | 64-bit | Frame pointer (callee-saved) |
| X30 (LR) | 64-bit | Link register (return address) |
| SP | 64-bit | Stack pointer (banked: SP_EL0, SP_ELx) |
| PC | 64-bit | Program counter |
| XZR | 64-bit | Zero register (reads as 0, writes discarded) |

**PSTATE (Process State):**
- N, Z, C, V — Condition flags
- SS — Software step (single-step)
- IL — Illegal execution state
- D, A, I, F — Debug, SError, IRQ, FIQ mask bits
- PAN — Privileged Access Never
- UAO — User Access Override
- DIT — Data Independent Timing
- TCO — Tag Check Override
- Z — SVE Zeroing
- M[3:0] — Execution state (EL0t, EL1t, EL1h, EL2t, EL2h, EL3t, EL3h)

**System registers (key MSRs):**
- `SCTLR_EL1` — System Control Register (MMU enable, alignment, caches, etc.)
- `TCR_EL1` — Translation Control Register (page size, VA size, ASID)
- `TTBR0_EL1` / `TTBR1_EL1` — Translation Table Base Registers
- `MAIR_EL1` — Memory Attribute Indirection Register
- `ESR_EL1` — Exception Syndrome Register
- `FAR_EL1` — Fault Address Register
- `ELR_EL1` — Exception Link Register (return address)
- `SPSR_EL1` — Saved Process State Register
- `VBAR_EL1` — Vector Base Address Register
- `CNTPCT_EL0` — Counter-timer Physical Count
- `CNTFRQ_EL0` — Counter-timer Frequency
- `TPIDR_EL0` / `TPIDRRO_EL0` / `TPIDR_EL1` — Thread ID registers
- `CONTEXTIDR_EL1` — Context ID Register (ASID)
- `PAR_EL1` — Physical Address Register (AT instruction result)

### 2.2 Instruction Categories

| Category | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **Data processing (immediate)** | ADD, ADDS, SUB, SUBS, CMP, CMN, MOV (ORR immediate), MVN, AND, ANDS, TST, ORR, EOR, ADC, SBC, SBCS, NEG, NEGS | P0 | Arithmetic with immediate operands |
| **Data processing (register)** | ADD, ADDS, SUB, SUBS, CMP, CMN, NEG, NEGS, ADC, SBC, SBCS, AND, ANDS, TST, BIC, BICS, ORR, ORN, EOR, EON, LSL, LSR, ASR, ROR | P0 | Register-to-register operations |
| **Data processing (wide immediate)** | MOVN, MOVZ, MOVK | P0 | Move wide immediate with shift |
| **Conditional operations** | CSEL, CSINC, CSINV, CSNEG, CINC, CINV, CNEG, CSET, CSETM | P0 | Conditional select and increment |
| **Multiply/divide** | MUL, SMULH, UMULH, SDIV, UDIV, MADD, MSUB, SMADDL, UMADDL, SMSUBL, UMSUBL, SMULL, UMULL | P0 | Integer multiply and divide |
| **Logical shift** | LSL, LSR, ASR, ROR (via SBFM/UBFM/EXTR) | P0 | Shift and rotate |
| **Bitfield operations** | BFM, SBFM, UBFM, BFI, BFXIL, SBFX, UBFX, SXTW, SXTB, SXTH, UXTW, UXTB, UXTH | P0 | Bitfield insert, extract, sign extend |
| **Load/store (single)** | LDR, STR, LDRB, STRB, LDRH, STRH, LDRSB, LDRSH, LDRSW, LDUR, STUR, LDURB, STURB, LDURH, STURH, LDURSB, LDURSH, LDURSW | P0 | Single register load/store |
| **Load/store (pair)** | LDP, STP, LDNP, STNP | P0 | Register pair load/store |
| **Load/store (exclusive)** | LDXR, STXR, LDXRB, STXRB, LDXRH, STXRH, LDXP, STXP | P0 | Exclusive access for atomics |
| **Load acquire / store release** | LDAR, STLR, LDARB, STLRB, LDARH, STLRH, LDAXR, STLXR, LDAXP, STLXP | P0 | Acquire/release semantics |
| **LSE atomics (ARMv8.1+)** | CAS, CASB, CASH, CASP, SWP, SWPB, SWPH, LDADD, LDADDB, LDADDH, LDCLR, LDCLRB, LDCLRH, LDEOR, LDEORB, LDEORH, LDSET, LDSETB, LDSETH | P1 | Large System Extensions (atomic ops) |
| **Branch** | B, BL, BR, BLR, RET, B.cond, CBZ, CBNZ, TBZ, TBNZ | P0 | All branch types |
| **Exception generation** | SVC, HVC, SMC, BRK, HLT | P0 | System calls, hypercalls, breakpoints |
| **System register access** | MRS, MSR | P0 | Read/write system registers |
| **System instructions** | NOP, YIELD, WFE, WFI, SEV, SEVL, ISB, DSB, DMB, CLREX | P0 | Synchronization and hints |
| **SIMD (scalar)** | FMOV, FABS, FNEG, FSQRT, FCVT, FRINTP, FRINTM, FRINTN, FRINTZ, FRINTA, FRINTX, FRINTI, FADD, FSUB, FMUL, FDIV, FMAX, FMIN, FMAXNM, FMINNM, FCMP, FCMPE | P1 | Scalar floating-point |
| **SIMD (vector)** | MOVI, MVNI, FMOV, DUP, SMOV, UMOV, INS, LD1, LD2, LD3, LD4, ST1, ST2, ST3, ST4, ADD, SUB, MUL, MLA, MLS, FADD, FSUB, FMUL, FDIV, FMAX, FMIN, CMEQ, CMGT, CMGE, CMHI, CMHS, CMTST, AND, BIC, ORR, EOR, BS, BSL, BIT, BIF, SSHL, USHL, SHRN, SHRN2, SSHLL, USHLL, XTN, XTN2, ZIP1, ZIP2, UZP1, UZP2, TRN1, TRN2, EXT, REV16, REV32, REV64 | P1 | Advanced SIMD (NEON) |
| **SVE (ARMv8.2+)** | All SVE predicated and unpredicated instructions | P2 | Scalable Vector Extension |
| **SVE2 (ARMv8.4+)** | All SVE2 instructions | P2 | Scalable Vector Extension 2 |
| **Pointer auth (ARMv8.3+)** | PACIA, PACIB, PACDA, PACDB, AUTIA, AUTIB, AUTDA, AUTDB, XPACI, XPACD, PACIZA, PACIZB | P2 | Pointer authentication |
| **Memory tagging (ARMv8.5+)** | STG, STZG, STGP, LDG, LDGM, STGM, STZGM, GMI, SUBP, SUBPS, IRG, ADDG, SUBG | P2 | Memory Tagging Extension |

### 2.3 Instruction Encoding

AArch64 uses fixed 32-bit instruction encoding (4 bytes per instruction):

| Bit Range | Field | Description |
|-----------|-------|-------------|
| 31-24 | Op0 | Primary opcode group |
| 23-22 | Op1 | Secondary opcode group |
| 21-10 | Op2 | Tertiary opcode / register fields |
| 9-5 | Rn | First source register |
| 4-0 | Rd/Rt | Destination / second source register |

**Instruction groups (Op0[31:24]):**
- `0x00-0x03` — SVE, Advanced SIMD, floating-point
- `0x04-0x07` — SVE, Advanced SIMD, floating-point
- `0x08-0x0B` — Load/store (LDAPR/STLR, LDP/STP, etc.)
- `0x0C-0x0F` — Load/store (LDR/STR, LDRB/STRB, etc.)
- `0x10-0x13` — Data processing (immediate)
- `0x14-0x17` — Branches, exception generation, system instructions
- `0x18-0x1B` — Load/store (register offset, exclusive, etc.)
- `0x1C-0x1F` — Data processing (floating-point, SIMD)
- `0x20-0x23` — SVE, Advanced SIMD, floating-point
- `0x24-0x27` — SVE, Advanced SIMD, floating-point
- `0x28-0x2B` — Load/store (SIMD, SVE)
- `0x2C-0x2F` — Load/store (SIMD, SVE)
- `0x30-0x33` — Data processing (register)
- `0x34-0x37` — Data processing (floating-point, SIMD)
- `0x38-0x3B` — Data processing (register)
- `0x3C-0x3F` — Data processing (floating-point, SIMD)

**Implementation approach:** Decode using a table-driven approach with the primary opcode group (bits 31:24) as the first-level dispatch, then secondary fields for sub-decoding.

---

## 3. MMU / Paging

### 3.1 AArch64 VMSAv8-64 Translation

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 16KB, 64KB |
| **Page table levels** | 3 levels (4KB pages, 40-bit VA) or 4 levels (4KB pages, 48-bit VA) |
| **Virtual address bits** | 40-bit (TCR_EL1.T0SZ=24) or 48-bit (TCR_EL1.T0SZ=16) |
| **Physical address bits** | Up to 48-bit (52-bit with ARMv8.2-LPA) |
| **Translation registers** | TTBR0_EL1 (user space), TTBR1_EL1 (kernel space) |
| **TCR fields** | T0SZ/T1SZ (VA size), TG0/TG1 (granule size), IPS (physical address size), AS (ASID size), A1 (ASID for TTBRx), EPD0/EPD1 (disable table walk) |
| **MAIR** | Memory Attribute Indirection Register (8 attribute slots) |

**Page table entry format (4KB granule, 48-bit VA):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | V | Valid |
| 1 | TYPE | Table (1) or Page (0) at level 0/1/2; Page (1) or reserved (0) at level 3 |
| 2-4 | ATTR_INDX | Index into MAIR (0-7) |
| 5-6 | NS | Non-secure (for EL2/EL3) |
| 7 | AP[1] | Access permission bit 1 |
| 8 | AP[0] | Access permission bit 0 (00=EL0/EL1 R/W, 01=EL1 R/W, 10=EL0/EL1 R, 11=EL1 R) |
| 9 | SH | Shareability (00=non-shareable, 10=outer, 11=inner) |
| 10 | AF | Access Flag (must be 1 to access, otherwise Access Flag fault) |
| 11 | nG | Not Global (tag with ASID) |
| 12-15 | | Reserved (SBZ) |
| 16-47 | PhysAddr | Output address (bits 16-47 for 4KB granule) |
| 48 | nT | Non-translation (for 2-stage) |
| 49-50 | PXN | Privileged Execute Never |
| 51 | XN | Execute Never |
| 52-62 | | Reserved |
| 63 | PBHA | Page-based hardware attributes (optional) |

**Page table walk (4KB granule, 48-bit VA):**
```
// Stage 1 translation
TTBR = (VA[63] == 0) ? TTBR0_EL1 : TTBR1_EL1
TCR.TG0/TG1 determines granule size
TCR.T0SZ/T1SZ determines VA size

Level 0: L0Index = VA[47:39], L0Desc = ReadPhys(TTBR + L0Index * 8)
  If L0Desc.TYPE == 1: Table descriptor → next level
  If L0Desc.TYPE == 0: Block (1GB) → PhysAddr = L0Desc[47:30] << 30 | VA[29:0]

Level 1: L1Index = VA[38:30], L1Desc = ReadPhys(L0Desc[47:12] << 12 + L1Index * 8)
  If L1Desc.TYPE == 1: Table descriptor → next level
  If L1Desc.TYPE == 0: Block (2MB) → PhysAddr = L1Desc[47:21] << 21 | VA[20:0]

Level 2: L2Index = VA[29:21], L2Desc = ReadPhys(L1Desc[47:12] << 12 + L2Index * 8)
  If L2Desc.TYPE == 1: Table descriptor → next level
  If L2Desc.TYPE == 0: Block (4KB granule, 2MB) → PhysAddr = L2Desc[47:21] << 21 | VA[20:0]

Level 3: L3Index = VA[20:12], L3Desc = ReadPhys(L2Desc[47:12] << 12 + L3Index * 8)
  Page → PhysAddr = L3Desc[47:12] << 12 | VA[11:0]
```

### 3.2 Stage 2 Translation (Virtualization)

For nested virtualization or VMM-like scenarios:
- Controlled by VTTBR_EL2 and VTCR_EL2
- Stage 2 translates intermediate physical addresses (IPA) to physical addresses (PA)
- Same page table format as Stage 1, but with different permission bits
- **Priority:** P2 (not required for initial FreeBSD boot)

### 3.3 TLB Simulation

| Feature | Implementation |
|---------|---------------|
| **Software TLB** | Hash table mapping (VA, ASID) → (PhysAddr, permissions) |
| **TLB flush** | TLBI operations: ALLE1, ALLE2, ALLE3, VMALLE1, ASIDE1, VAAE1, VALE1, IPAS2E1 |
| **ASID** | Address Space ID (8 or 16 bits, controlled by TCR.AS) |
| **VMID** | Virtual Machine ID (for Stage 2) |
| **TLB size** | Configurable (default: 64 entries, 4-way associative) |

---

## 4. Interrupt and Exception Model

### 4.1 Exception Levels

| Level | Name | Purpose |
|-------|------|---------|
| EL0 | User | Application code |
| EL1 | Kernel | OS kernel (FreeBSD runs at EL1) |
| EL2 | Hypervisor | Virtualization (not used by FreeBSD on bare metal) |
| EL3 | Secure Monitor | Secure world (not used by FreeBSD) |

### 4.2 Exception Vector Table

Each exception level has its own vector table, pointed to by `VBAR_ELx`. The table has 16 entries (16 bytes each, 4 instructions per entry):

| Offset | Exception Type | Description |
|--------|---------------|-------------|
| 0x000 | ELx_SP_EL0_SYNC | Synchronous, same EL, SP_EL0 |
| 0x080 | ELx_SP_EL0_IRQ | IRQ, same EL, SP_EL0 |
| 0x100 | ELx_SP_EL0_FIQ | FIQ, same EL, SP_EL0 |
| 0x180 | ELx_SP_EL0_SERROR | SError, same EL, SP_EL0 |
| 0x200 | ELx_SP_ELx_SYNC | Synchronous, same EL, SP_ELx |
| 0x280 | ELx_SP_ELx_IRQ | IRQ, same EL, SP_ELx |
| 0x300 | ELx_SP_ELx_FIQ | FIQ, same EL, SP_ELx |
| 0x380 | ELx_SP_ELx_SERROR | SError, same EL, SP_ELx |
| 0x400 | ELx_EL0_SYNC | Synchronous, lower EL (AArch64) |
| 0x480 | ELx_EL0_IRQ | IRQ, lower EL (AArch64) |
| 0x500 | ELx_EL0_FIQ | FIQ, lower EL (AArch64) |
| 0x580 | ELx_EL0_SERROR | SError, lower EL (AArch64) |
| 0x600 | ELx_EL0_32_SYNC | Synchronous, lower EL (AArch32) |
| 0x680 | ELx_EL0_32_IRQ | IRQ, lower EL (AArch32) |
| 0x700 | ELx_EL0_32_FIQ | FIQ, lower EL (AArch32) |
| 0x780 | ELx_EL0_32_SERROR | SError, lower EL (AArch32) |

### 4.3 Exception Syndrome (ESR_ELx)

| Exception Class (EC) | Description |
|----------------------|-------------|
| 0x00-0x13 | Various SIMD/SVE/SVE2 traps |
| 0x14 | SVC (system call) from AArch64 |
| 0x15 | SVC from AArch32 |
| 0x16-0x17 | HVC/SMC |
| 0x18 | MSR/MRS (trapped system register) |
| 0x19 | SVE access trap |
| 0x1A | ERET/ERETAA/ERETAB trap |
| 0x20 | Instruction Abort (MMU fault on instruction fetch) |
| 0x21 | Instruction Abort (same EL) |
| 0x22 | PC alignment fault |
| 0x24 | Data Abort (MMU fault on data access) |
| 0x25 | Data Abort (same EL) |
| 0x26 | SP alignment fault |
| 0x28 | Trapped floating-point/SIMD access |
| 0x2C | Trapped SVE access |
| 0x2E | Trapped TME access |
| 0x30 | Breakpoint (BRK instruction) |
| 0x31 | Breakpoint (hardware) |
| 0x32 | Software step |
| 0x33 | Watchpoint |
| 0x34 | Breakpoint (AArch32) |
| 0x38 | Pointer authentication failure |
| 0x3C | SEI (SError Interrupt) |
| 0x3F | SError |

**Data Abort syndrome (ISS):**
- ISV (bit 24): Instruction Syndrome Valid
- SAS (bits 23-22): Size (00=byte, 01=halfword, 10=word, 11=doubleword)
- SSE (bit 21): Sign Extend
- SRT (bits 20-16): Register transfer
- SF (bit 15): 64-bit register transfer
- AR (bit 14): Acquire/Release
- VNCR (bit 13): Not checked
- SET (bits 12-11): Tag access
- FnV (bit 10): Fault not valid
- EA (bit 9): External abort type
- CM (bit 8): Cache maintenance
- WnR (bit 6): Write not Read
- DFSC (bits 5-0): Data Fault Status Code

### 4.4 Generic Interrupt Controller (GICv3)

| Component | Address | Description |
|-----------|---------|-------------|
| **Distributor** | 0x2F000000 | Interrupt configuration, routing, enables |
| **Redistributor** | Per-CPU (0x2F100000 + n*0x20000) | Per-CPU interrupt management, SGI/PPI |
| **CPU Interface** | 0x2F200000 | Interrupt acknowledge, priority, EOI |
| **ITS** | 0x2F400000 | Interrupt Translation Service (MSI) |

**Interrupt types:**
- **SGI** (Software Generated Interrupt): IDs 0-15, IPI between CPUs
- **PPI** (Private Peripheral Interrupt): IDs 16-31, per-CPU timers
- **SPI** (Shared Peripheral Interrupt): IDs 32-1019, device interrupts
- **LPI** (Locality-specific Peripheral Interrupt): IDs 8192+, MSI

**Key GICv3 registers:**
- `GICD_CTLR` — Distributor Control
- `GICD_ISENABLERn` — Interrupt Set-Enable
- `GICD_ICENABLERn` — Interrupt Clear-Enable
- `GICD_ISPENDRn` — Interrupt Set-Pending
- `GICD_ICPENDRn` — Interrupt Clear-Pending
- `GICD_ISACTIVERn` — Interrupt Set-Active
- `GICD_ICACTIVERn` — Interrupt Clear-Active
- `GICD_IPRIORITYRn` — Interrupt Priority
- `GICD_ITARGETSRn` — Interrupt Target (GICv2 compat)
- `GICD_ICFGRn` — Interrupt Configuration (edge/level)
- `GICD_IROUTERn` — Interrupt Routing (GICv3, SPI)
- `GICR_CTLR` — Redistributor Control
- `GICR_ISENABLER0` — Redistributor SGI/PPI Set-Enable
- `GICR_ICENABLER0` — Redistributor SGI/PPI Clear-Enable
- `GICR_ISPENDR0` — Redistributor SGI/PPI Set-Pending
- `GICR_ICPENDR0` — Redistributor SGI/PPI Clear-Pending
- `GICR_ISACTIVER0` — Redistributor SGI/PPI Set-Active
- `GICR_ICACTIVER0` — Redistributor SGI/PPI Clear-Active
- `GICR_IPRIORITYRn` — Redistributor SGI/PPI Priority
- `GICR_ICFGR0/1` — Redistributor SGI/PPI Configuration
- `GICC_CTLR` — CPU Interface Control
- `GICC_PMR` — Priority Mask
- `GICC_BPR` — Binary Point
- `GICC_IAR` — Interrupt Acknowledge
- `GICC_EOIR` — End of Interrupt
- `GICC_RPR` — Running Priority

### 4.5 Timer (Generic Timer)

| Register | Description |
|----------|-------------|
| `CNTPCT_EL0` | Physical Count Register (free-running counter) |
| `CNTFRQ_EL0` | Counter Frequency Register |
| `CNTP_TVAL_EL0` | Physical Timer Value (countdown) |
| `CNTP_CTL_EL0` | Physical Timer Control (enable, mask, status) |
| `CNTP_CVAL_EL0` | Physical Timer Compare Value |
| `CNTPS_CVAL_EL1` | Physical Timer Secure Compare Value |
| `CNTVCT_EL0` | Virtual Count Register |
| `CNTV_TVAL_EL0` | Virtual Timer Value |
| `CNTV_CTL_EL0` | Virtual Timer Control |
| `CNTV_CVAL_EL0` | Virtual Timer Compare Value |

---

## 5. Boot Process

### 5.1 FreeBSD Boot Sequence (arm64)

1. **Boot firmware** (UEFI/UBoot) — loaded from flash/disk by SoC boot ROM
2. **loader.efi** (UEFI application) — loads kernel, modules, reads /boot/loader.conf
3. **kernel** — ELF binary loaded by UEFI at a DRAM address
4. **Kernel initialization:**
   - Entry point: `start` (locore.S) — set up page tables, enable MMU
   - `mi_startup()` — main initialization
   - `kmain()` — kernel main

### 5.2 Firmware Boot

| Firmware Type | Description | Emulation Required |
|---------------|-------------|-------------------|
| **UEFI** | EDK2/TianoCore for ARM64 | OVMF AArch64 build or U-Boot |
| **U-Boot** | Common on embedded ARM64 | U-Boot binary integration |
| **Device tree** | FDT (Flattened Device Tree) | Must generate DTB for emulated platform |

**Recommended approach:**
- Use **U-Boot** as the boot firmware (BSD-licensed, well-tested on ARM64)
- Generate a device tree blob (DTB) describing the emulated platform
- Load the FreeBSD arm64 kernel as an ELF binary via U-Boot's booti command

### 5.3 Required Emulated Devices for Boot

| Device | Type | Address | Purpose |
|--------|------|---------|---------|
| **UART (PL011)** | Serial | 0x9000000 | Console output |
| **GICv3** | Interrupt | 0x2F000000 (Dist), 0x2F100000 (Redist), 0x2F200000 (CPU IF) | Interrupt controller |
| **Generic Timer** | Timer | System registers | Scheduling |
| **virtio-blk** | Storage | MMIO at 0x3F000000 | Disk access |
| **virtio-net** | Network | MMIO at 0x3F100000 | Network access |
| **RTC (ARM PL031)** | Timer | 0x1C170000 | Real-time clock |
| **System reset** | Misc | 0x3F200000 | Power management |

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| ARM64.1 | Implement AArch64 instruction decoder | NOT STARTED | | | `sys/emulation/arm64/emu_cpu_arm64.c` | Decode fixed 32-bit instructions. Primary opcode group dispatch. |
| ARM64.2 | Implement data processing (immediate) instructions (P0) | NOT STARTED | | ARM64.1 | `sys/emulation/arm64/emu_cpu_arm64.c` | ADD, ADDS, SUB, SUBS, CMP, CMN, MOV, AND, ORR, EOR, etc. with immediates |
| ARM64.3 | Implement data processing (register) instructions (P0) | NOT STARTED | | ARM64.2 | `sys/emulation/arm64/emu_cpu_arm64.c` | ADD, SUB, AND, ORR, EOR, BIC, LSL, LSR, ASR, etc. register-to-register |
| ARM64.4 | Implement load/store instructions (P0) | NOT STARTED | | ARM64.2 | `sys/emulation/arm64/emu_cpu_arm64.c` | LDR, STR, LDP, STP, LDRB, STRB, LDRH, STRH, LDRSW, LDUR, STUR |
| ARM64.5 | Implement branch instructions (P0) | NOT STARTED | | ARM64.3 | `sys/emulation/arm64/emu_cpu_arm64.c` | B, BL, BR, BLR, RET, B.cond, CBZ, CBNZ, TBZ, TBNZ |
| ARM64.6 | Implement conditional operations (P0) | NOT STARTED | | ARM64.3 | `sys/emulation/arm64/emu_cpu_arm64.c` | CSEL, CSINC, CSINV, CSNEG, CSET, CSETM |
| ARM64.7 | Implement multiply/divide instructions (P0) | NOT STARTED | | ARM64.3 | `sys/emulation/arm64/emu_cpu_arm64.c` | MUL, SDIV, UDIV, MADD, MSUB, SMULL, UMULL |
| ARM64.8 | Implement bitfield operations (P0) | NOT STARTED | | ARM64.3 | `sys/emulation/arm64/emu_cpu_arm64.c` | BFM, SBFM, UBFM, BFI, BFXIL, SBFX, UBFX, SXTW, SXTB, etc. |
| ARM64.9 | Implement exception generation (P0) | NOT STARTED | | ARM64.5 | `sys/emulation/arm64/emu_cpu_arm64.c` | SVC, BRK, HLT |
| ARM64.10 | Implement system register access (P0) | NOT STARTED | | ARM64.1 | `sys/emulation/arm64/emu_cpu_arm64.c` | MRS, MSR. Emulate SCTLR, TCR, TTBR0/1, MAIR, ESR, FAR, ELR, SPSR, VBAR, TPIDR, CNTFRQ, CNTPCT. |
| ARM64.11 | Implement exclusive access instructions (P0) | NOT STARTED | | ARM64.4 | `sys/emulation/arm64/emu_cpu_arm64.c` | LDXR, STXR, LDXP, STXP, LDAXR, STLXR, CLREX |
| ARM64.12 | Implement acquire/release instructions (P0) | NOT STARTED | | ARM64.4 | `sys/emulation/arm64/emu_cpu_arm64.c` | LDAR, STLR, LDARB, STLRB, LDARH, STLRH |
| ARM64.13 | Implement LSE atomics (P1) | NOT STARTED | | ARM64.11 | `sys/emulation/arm64/emu_cpu_arm64.c` | CAS, SWP, LDADD, LDCLR, LDEOR, LDSET (ARMv8.1+) |
| ARM64.14 | Implement AArch64 page table walk (P0) | NOT STARTED | | | `sys/emulation/arm64/emu_mmu_arm64.c` | 4KB/16KB/64KB pages. 3 or 4 levels. TTBR0/TTBR1. TCR configuration. |
| ARM64.15 | Implement TLB simulation (P0) | NOT STARTED | | ARM64.14 | `sys/emulation/arm64/emu_mmu_arm64.c` | Software TLB with ASID. TLBI operations. |
| ARM64.16 | Implement exception vector table (P0) | NOT STARTED | | ARM64.9 | `sys/emulation/arm64/emu_intr_arm64.c` | VBAR_EL1, 16-entry vector table, exception entry/return |
| ARM64.17 | Implement synchronous exception dispatch (P0) | NOT STARTED | | ARM64.16 | `sys/emulation/arm64/emu_intr_arm64.c` | ESR_EL1 decoding, EC dispatch, fault handling |
| ARM64.18 | Implement GICv3 distributor (P0) | NOT STARTED | | ARM64.16 | `sys/emulation/arm64/emu_intr_arm64.c` | MMIO at 0x2F000000. Interrupt configuration, routing, enables. |
| ARM64.19 | Implement GICv3 redistributor (P0) | NOT STARTED | | ARM64.18 | `sys/emulation/arm64/emu_intr_arm64.c` | Per-CPU MMIO. SGI/PPI management. |
| ARM64.20 | Implement GICv3 CPU interface (P0) | NOT STARTED | | ARM64.19 | `sys/emulation/arm64/emu_intr_arm64.c` | IAR, EOI, PMR, BPR. Interrupt acknowledge and completion. |
| ARM64.21 | Implement generic timer (P0) | NOT STARTED | | ARM64.16 | `sys/emulation/arm64/emu_intr_arm64.c` | CNTPCT, CNTFRQ, CNTP_TVAL, CNTP_CTL, CNTP_CVAL. Timer interrupt to GIC. |
| ARM64.22 | Implement PL011 UART (P0) | NOT STARTED | | | `usr.sbin/emu/emu_dev_uart.c` | ARM PrimeCell UART at 0x9000000. Console output. |
| ARM64.23 | Implement U-Boot firmware loading (P0) | NOT STARTED | | ARM64.1 | `usr.sbin/emu/emu_arch_arm64.c` | Load U-Boot binary from blob cache via `emu_blob_resolve()`. Generate DTB for emulated platform. See `010-Emulation-Blob-Management.md`. |
| ARM64.24 | Implement device tree generation (P0) | NOT STARTED | | ARM64.23 | `usr.sbin/emu/emu_arch_arm64.c` | Generate FDT blob describing CPU, GIC, UART, timer, virtio devices. |
| ARM64.25 | Implement scalar floating-point (P1) | NOT STARTED | | ARM64.3 | `sys/emulation/arm64/emu_cpu_arm64.c` | FADD, FSUB, FMUL, FDIV, FSQRT, FCVT, FCMP, FMOV |
| ARM64.26 | Implement Advanced SIMD (NEON) (P1) | NOT STARTED | | ARM64.25 | `sys/emulation/arm64/emu_cpu_arm64.c` | LD1/ST1, ADD, MUL, FADD, CMEQ, AND, ORR, DUP, MOVI, ZIP, etc. |
| ARM64.27 | Implement AArch32 compatibility mode (P2) | NOT STARTED | | ARM64.1 | `sys/emulation/arm64/emu_cpu_arm64.c` | 32-bit ARM and Thumb instruction decoding. Exception level switching. |
| ARM64.28 | Implement SVE instructions (P2) | NOT STARTED | | ARM64.26 | `sys/emulation/arm64/emu_cpu_arm64.c` | Scalable Vector Extension (ARMv8.2+) |
| ARM64.29 | Implement pointer authentication (P2) | NOT STARTED | | ARM64.10 | `sys/emulation/arm64/emu_cpu_arm64.c` | PACIA, PACIB, AUTIA, AUTIB, XPACI (ARMv8.3+) |
| ARM64.30 | Write arm64 CPU emulation unit tests | NOT STARTED | | ARM64.1-ARM64.29 | `tests/sys/emulation/arm64/` | Test each instruction category. Test page table walk. Test exception dispatch. Test GIC. |
| ARM64.31 | Write arm64 boot integration test | NOT STARTED | | ARM64.23 | `tests/usr.sbin/emu/arm64_boot_test.sh` | Boot FreeBSD arm64 kernel in emulator. Verify console output. |
| ARM64.32 | Write arm64 kernel module load test | NOT STARTED | | ARM64.31 | `tests/usr.sbin/emu/arm64_module_test.sh` | Load, verify, unload kernel module in emulated arm64 environment. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 3 (Architecture-Specific CPU Emulation), Phase 5 (Custom Emulator Engine). |
| `002-Emulation-Security-FS.md` | Security architecture. |
| `006-Emulation-Arch-arm.md` | ARM 32-bit architecture. Shares GIC, timer, and UART infrastructure. |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| ARM Architecture Reference Manual ARMv8-A (DDI0487) | https://developer.arm.com/documentation/ddi0487/ | Primary ISA reference |
| ARM GICv3 Architecture Specification (GGI009) | https://developer.arm.com/documentation/ihi0069/ | Interrupt controller reference |
| FreeBSD arm64 kernel entry (locore.S) | `sys/arm64/arm64/locore.S` | Boot sequence, initial page tables |
| FreeBSD arm64 pmap | `sys/arm64/arm64/pmap.c` | Page table management reference |
| FreeBSD arm64 trap handling | `sys/arm64/arm64/trap.c` | Exception dispatch reference |
| FreeBSD arm64 GIC driver | `sys/arm64/arm64/gicv3.c` | GICv3 driver reference |
| U-Boot | https://github.com/u-boot/u-boot | Boot firmware (BSD-licensed) |
| QEMU ARM64 target | https://github.com/qemu/qemu/tree/master/target/arm | Reference instruction decoder |

### 7.3 Shared Infrastructure

| Component | Shared With | Location |
|-----------|-------------|----------|
| Instruction decoder framework | All architectures | `sys/emulation/emu_cpu.c` |
| Memory region management | All architectures | `sys/emulation/emu_mem.c` |
| Device model framework | All architectures | `sys/emulation/emu_device.c` |
| Interrupt controller framework | All architectures | `sys/emulation/emu_intr.c` |
| Console capture | All architectures | `sys/emulation/emu_console.c` |
| Crash detection | All architectures | `sys/emulation/emu_crash.c` |
| GDB stub | All architectures | `usr.sbin/emu/emu_gdb.c` |
| Snapshot/restore | All architectures | `usr.sbin/emu/emu_snapshot.c` |
| PL011 UART | arm (32-bit) | `usr.sbin/emu/emu_dev_uart.c` |
| virtio-blk | All architectures | `usr.sbin/emu/emu_dev_storage.c` |
| virtio-net | All architectures | `usr.sbin/emu/emu_dev_net.c` |

---

## 8. Notes

- AArch64 uses fixed 32-bit instruction encoding, making the decoder significantly simpler than x86-64's variable-length encoding.
- The GICv3 is the most complex component to emulate. Start with a simplified GICv2-compatible mode if GICv3 proves too complex initially.
- FreeBSD arm64 requires a device tree blob (DTB) to describe the platform. The emulator must generate a valid DTB at boot time.
- U-Boot is the recommended boot firmware. It supports loading ELF kernels directly via the `booti` command. U-Boot is **not** included in the FreeBSD source tree or release; it is downloaded at runtime via `emu blob fetch uboot-arm64`. See `010-Emulation-Blob-Management.md` for the complete blob management system.
- The AArch32 compatibility mode (running 32-bit ARM code at EL0) is not required for FreeBSD arm64 kernel testing but may be needed for userspace compatibility testing.
