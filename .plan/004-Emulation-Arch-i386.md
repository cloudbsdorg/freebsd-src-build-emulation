# i386 (x86-32) Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | IA-32 (x86-32), with x86-16 (real mode) and x86-64 (long mode via amd64 module) support |
| **Base specification** | Intel 64 and IA-32 Architectures SDM Volumes 1-3 |
| **FreeBSD support** | i386 (primary target) |
| **Emulation priority** | P1 |
| **bhyve support** | ❌ (bhyve does not support 32-bit guests) |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_i386.ko` |

### 1.1 CPU Levels / Feature Tiers

| CPU Level | Base ISA | Key Features | FreeBSD Target |
|-----------|----------|--------------|----------------|
| `i386` | IA-32 | CMPXCHG8B, SSE2, PAE, PSE36, FXSR | Baseline i386 |
| `i486` | i386 + | CMPXCHG, XADD, BSWAP, INVPLG | 486-class |
| `i586` | i486 + | MMX, conditional move, RDTSC | Pentium-class |
| `i686` | i586 + | SSE, P6 features, CMOV, CMPXCHG8B, SYSENTER/SYSEXIT | Pentium Pro+ |

**Default CPU level:** `i686` (widest FreeBSD i386 compatibility)

**Instance configuration field:** `cpu_level` (string, e.g., "i686", "i586")

**Note:** The i386 architecture shares the instruction decoder, MMU, and interrupt infrastructure with amd64 (see `003-Emulation-Arch-amd64.md`). This document covers only the differences from amd64.

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers (32-bit mode)

| Register | Width | Purpose |
|----------|-------|---------|
| EAX | 32-bit | Accumulator, return value, syscall number |
| EBX | 32-bit | Base register (callee-saved) |
| ECX | 32-bit | Counter, 4th syscall arg |
| EDX | 32-bit | Data, 3rd syscall arg |
| ESI | 32-bit | Source, 2nd syscall arg |
| EDI | 32-bit | Destination, 1st syscall arg |
| EBP | 32-bit | Base pointer (callee-saved, frame pointer) |
| ESP | 32-bit | Stack pointer |
| EIP | 32-bit | Instruction pointer |
| EFLAGS | 32-bit | Status flags |

**Segment registers:** CS, DS, ES, FS, GS, SS (each 16-bit selector + 32-bit base hidden)

**Control registers:** CR0, CR2, CR3, CR4

**Debug registers:** DR0-DR7

**x87 FPU registers:** ST0-ST7 (80-bit each)

**SSE registers:** XMM0-XMM7 (128-bit, SSE only — no AVX in 32-bit mode)

**MSRs (Model-Specific Registers):**
- `MSR_EFER` (0xC0000080) — Extended Feature Enable Register (only NXE, LME/LMA not valid in 32-bit)
- `MSR_MTRR*` — Memory type range registers
- `MSR_APIC_BASE` (0x1B) — Local APIC base address
- `MSR_TSC` (0x10) — Time-stamp counter
- `MSR_PAT` (0x277) — Page attribute table
- `MSR_SYSENTER_CS/EIP/ESP` (0x174-0x176) — SYSENTER/SYSEXIT targets

### 2.2 Differences from amd64

| Feature | i386 (32-bit) | amd64 (64-bit) |
|---------|---------------|----------------|
| **Register width** | 32-bit (EAX, EBX, etc.) | 64-bit (RAX, RBX, etc.) |
| **Extended registers** | None (no R8-R15) | R8-R15 available |
| **RIP-relative addressing** | Not available | Available |
| **REX prefix** | Not available (0x40-0x4F are INC/DEC) | Available (0x40-0x4F) |
| **SYSCALL/SYSRET** | Not available (use SYSENTER/SYSEXIT or INT 0x80) | Available |
| **PUSHA/POPA** | Available | Not available (invalid in 64-bit mode) |
| **AAA/AAD/AAM/AAS** | Available (BCD instructions) | Not available (invalid in 64-bit mode) |
| **DAA/DAS** | Available (BCD instructions) | Not available (invalid in 64-bit mode) |
| **BOUND** | Available | Not available (invalid in 64-bit mode) |
| **INTO** | Available | Not available (invalid in 64-bit mode) |
| **SEG_CS/DS/ES/SS** | Full 32-bit base | 64-bit base, but limited use |
| **FS/GS base** | Via MSR or segment descriptor | Via MSR (FSBASE, GSBASE) |
| **AVX/AVX-512** | Not available | Available (x86-64-v3/v4) |
| **XMM registers** | XMM0-XMM7 | XMM0-XMM15 |
| **Page table levels** | 2 (no PAE) or 3 (PAE) | 4 (PML4→PDPT→PD→PT) |
| **Virtual address space** | 4GB (32-bit) | 256TB (48-bit) |
| **Physical address** | 32-bit (or 36-bit with PAE) | Up to 52-bit |

### 2.3 Instruction Categories

The i386 instruction set is a subset of amd64. All instructions listed in `003-Emulation-Arch-amd64.md` Section 2.2 apply, with the following exceptions:

**Not available in i386 mode:**
- REX-prefixed instructions (MOV with 64-bit operands)
- SYSCALL, SYSRET
- MOVSXD (move with sign extension 32→64)
- 64-bit variants: IRETQ, PUSHFQ, POPFQ
- R8-R15 register encodings

**Available only in i386 mode (not in amd64 long mode):**
- PUSHA/POPA (push/pop all 32-bit registers)
- AAA, AAD, AAM, AAS (BCD arithmetic)
- DAA, DAS (BCD decimal adjust)
- BOUND (check array index against bounds)
- INTO (interrupt on overflow)
- INC/DEC with 0x40-0x4F opcodes (in amd64 these are REX prefixes)

---

## 3. MMU / Paging

### 3.1 Legacy 32-bit Paging (No PAE)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 4MB (large page with CR4.PSE) |
| **Page table levels** | PD (Level 2), PT (Level 1) |
| **Virtual address bits** | 32-bit |
| **Physical address bits** | 32-bit |
| **CR4.PSE** | Page Size Extension (4MB pages) |
| **CR4.PGE** | Global pages |

**Page table entry format (4KB page, 32-bit):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | P | Present |
| 1 | R/W | Read/Write |
| 2 | U/S | User/Supervisor |
| 3 | PWT | Page-level write-through |
| 4 | PCD | Page-level cache disable |
| 5 | A | Accessed |
| 6 | D | Dirty (only at leaf PTEs) |
| 7 | PAT | Page attribute table (only at leaf PTEs) |
| 8 | G | Global (only at leaf PTEs, CR4.PGE must be set) |
| 9-11 | AVL | Available for OS use |
| 12-31 | PhysAddr | Physical page frame address (bits 12-31) |

**Page table walk (2-level, no PAE):**
```
PDE = ReadPhys(CR3[31:12] << 12 + (VA[31:22] << 2))
Check PDE.P == 1, else #PF
If PDE.PS == 1: 4MB page → PhysAddr = PDE[31:22] << 22 | VA[21:0]

PTE = ReadPhys(PDE[31:12] << 12 + (VA[21:12] << 2))
Check PTE.P == 1, else #PF
PhysAddr = PTE[31:12] << 12 | VA[11:0]
```

### 3.2 PAE Paging (36-bit Physical)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 2MB (large page) |
| **Page table levels** | PDPT (Level 3), PD (Level 2), PT (Level 1) |
| **Virtual address bits** | 32-bit |
| **Physical address bits** | 36-bit (64GB max) |
| **CR4.PAE** | Physical Address Extension |

**Page table entry format (PAE, 64-bit entries):**

Same format as x86-64 PTEs (see `003-Emulation-Arch-amd64.md` Section 3.1), but with 32-bit virtual address input.

**Page table walk (3-level, PAE):**
```
PDPTE = CR3[31:5] << 5 + (VA[31:30] << 3)  (4 PDPTE registers in CR3)
Check PDPTE.P == 1, else #PF

PDE = ReadPhys(PDPTE[51:12] << 12 + (VA[29:21] << 3))
Check PDE.P == 1, else #PF
If PDE.PS == 1: 2MB page → PhysAddr = PDE[51:21] << 21 | VA[20:0]

PTE = ReadPhys(PDE[51:12] << 12 + (VA[20:12] << 3))
Check PTE.P == 1, else #PF
PhysAddr = PTE[51:12] << 12 | VA[11:0]
```

### 3.3 TLB Simulation

Same as amd64 (see `003-Emulation-Arch-amd64.md` Section 3.5), but without PCID support (CR4.PCIDE is not available in 32-bit mode).

---

## 4. Interrupt and Exception Model

### 4.1 IDT (32-bit format)

The i386 IDT uses 8-byte entries (vs. 16-byte in x86-64):

```
Offset 0-1:   Offset[15:0] (low 16 bits of handler address)
Offset 2-3:   Segment selector (CS)
Offset 4:     Type (bits 0-3): 0xE=interrupt gate, 0xF=trap gate, 0x5=task gate
              DPL (bits 5-6): Descriptor privilege level
              P (bit 7): Present
Offset 5-6:   Offset[31:16] (high 16 bits)
```

### 4.2 Exception Vectors

Same exception vectors as amd64 (see `003-Emulation-Arch-amd64.md` Section 4.1), with the following differences:

| Vector | Name | i386 Difference |
|--------|------|-----------------|
| 9 | Coprocessor Segment Overrun | Exists in i386 (legacy), reserved in x86-64 |
| 20 | #VE | Not available in i386 (virtualization exception) |

### 4.3 Task State Segment (32-bit)

The i386 TSS is 104 bytes (vs. 128 bytes in x86-64):

```
Offset 0-1:   Previous task link (TSS selector)
Offset 2-3:   Reserved
Offset 4-7:   ESP0
Offset 8-11:  SS0
Offset 12-15: ESP1
Offset 16-19: SS1
Offset 20-23: ESP2
Offset 24-27: SS2
Offset 28-31: CR3
Offset 32-35: EIP
Offset 36-39: EFLAGS
Offset 40-43: EAX
Offset 44-47: ECX
Offset 48-51: EDX
Offset 52-55: EBX
Offset 56-59: ESP
Offset 60-63: EBP
Offset 64-67: ESI
Offset 68-71: EDI
Offset 72-75: ES
Offset 76-79: CS
Offset 80-83: SS
Offset 84-87: DS
Offset 88-91: FS
Offset 92-95: GS
Offset 96-99: LDT segment selector
Offset 100:   I/O map base (1 byte, plus 1 byte reserved for T bit)
```

**Key difference from x86-64:** The i386 TSS includes full register save area for hardware task switching (via task gate). x86-64 does not support hardware task switching.

### 4.4 Interrupt Controllers

Same as amd64:
- **i8259 PIC** (legacy, at 0x20-0x21, 0xA0-0xA1)
- **LAPIC** (at 0xFEE00000)
- **I/O APIC** (at 0xFEC00000)

---

## 5. Boot Process

### 5.1 Real Mode Startup

Same as amd64 (see `003-Emulation-Arch-amd64.md` Section 5.1):
- Reset vector at 0xFFFFFFF0 (or 0xFFFF0 for 8086/80286)
- Real mode addressing: PhysAddr = (CS << 4) + IP
- IVT at physical address 0x0000-0x03FF

### 5.2 Protected Mode Transition

1. Load GDT with at least null, code, and data descriptors
2. Set CR0.PE = 1 (enable protected mode)
3. Far jump to flush prefetch queue
4. Load segment registers with protected mode selectors
5. Set up page tables (optional, for paging)
6. Set CR0.PG = 1 (enable paging, if desired)

### 5.3 FreeBSD Boot Sequence (i386)

1. **boot0** (MBR) — loaded from disk by BIOS
2. **boot2** (BTX loader) — loaded by boot0
3. **loader** (Lua interpreter) — loads kernel, modules
4. **kernel** — ELF binary loaded at physical address 0x100000 (1MB)
5. **Kernel initialization:**
   - Entry point: `start` (locore.s) — set up page tables, enable paging
   - `mi_startup()` — main initialization
   - `kmain()` — kernel main

### 5.4 Required Emulated Devices for Boot

Same as amd64 (see `003-Emulation-Arch-amd64.md` Section 5.4):
- UART (NS16550) at 0x3F8
- HPET at 0xFED00000
- i8254 PIT at 0x40-0x43
- i8259 PIC at 0x20-0x21, 0xA0-0xA1
- I/O APIC at 0xFEC00000
- LAPIC at 0xFEE00000
- AHCI/SATA or virtio-blk for storage
- RTC at 0x70-0x71
- ACPI tables

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| I386.1 | Implement i386 instruction decoder (shared with amd64) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | Reuse amd64 decoder with 32-bit operand size. Disable REX prefix decoding. Enable 0x40-0x4F as INC/DEC. |
| I386.2 | Implement PUSHA/POPA instructions | NOT STARTED | | I386.1 | `sys/emulation/i386/emu_cpu_i386.c` | Push/pop all 32-bit registers (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI) |
| I386.3 | Implement BCD arithmetic instructions | NOT STARTED | | I386.1 | `sys/emulation/i386/emu_cpu_i386.c` | AAA, AAD, AAM, AAS, DAA, DAS |
| I386.4 | Implement BOUND instruction | NOT STARTED | | I386.1 | `sys/emulation/i386/emu_cpu_i386.c` | Check array index against bounds, #BR on failure |
| I386.5 | Implement INTO instruction | NOT STARTED | | I386.1 | `sys/emulation/i386/emu_cpu_i386.c` | Interrupt 4 on overflow flag |
| I386.6 | Implement 2-level page table walk (no PAE) | NOT STARTED | | AMD64.14 | `sys/emulation/i386/emu_mmu_i386.c` | PD→PT walk. 4KB and 4MB pages. 32-bit physical. |
| I386.7 | Implement PAE 3-level page table walk | NOT STARTED | | AMD64.14 | `sys/emulation/i386/emu_mmu_i386.c` | PDPT→PD→PT walk. 4KB and 2MB pages. 36-bit physical. |
| I386.8 | Implement 32-bit IDT (8-byte entries) | NOT STARTED | | AMD64.16 | `sys/emulation/i386/emu_intr_i386.c` | 8-byte IDT entries, task gate support |
| I386.9 | Implement 32-bit TSS (104 bytes) | NOT STARTED | | I386.8 | `sys/emulation/i386/emu_intr_i386.c` | Full register save area, hardware task switching |
| I386.10 | Implement SYSENTER/SYSEXIT (i386 syscall mechanism) | NOT STARTED | | I386.1 | `sys/emulation/i386/emu_cpu_i386.c` | MSR_SYSENTER_CS/EIP/ESP. Fast system call for 32-bit. |
| I386.11 | Implement INT 0x80 (legacy syscall) | NOT STARTED | | I386.8 | `sys/emulation/i386/emu_cpu_i386.c` | Traditional FreeBSD i386 syscall mechanism |
| I386.12 | Implement protected mode initialization | NOT STARTED | | I386.1 | `usr.sbin/emu/emu_arch_i386.c` | GDT setup, CR0.PE=1, far jump, segment reload |
| I386.13 | Implement SeaBIOS firmware loading (32-bit) | NOT STARTED | | I386.12 | `usr.sbin/emu/emu_arch_i386.c` | Load SeaBIOS binary from blob cache via `emu_blob_resolve()`. Legacy BIOS boot for i386. See `010-Emulation-Blob-Management.md`. |
| I386.14 | Write i386 CPU emulation unit tests | NOT STARTED | | I386.1-I386.13 | `tests/sys/emulation/i386/` | Test 32-specific instructions. Test 2-level and PAE paging. Test hardware task switching. |
| I386.15 | Write i386 boot integration test | NOT STARTED | | I386.13 | `tests/usr.sbin/emu/i386_boot_test.sh` | Boot FreeBSD i386 kernel in emulator. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 3 (Architecture-Specific CPU Emulation). |
| `002-Emulation-Security-FS.md` | Security architecture. |
| `003-Emulation-Arch-amd64.md` | AMD64 architecture. Shares instruction decoder, MMU, and interrupt infrastructure. All amd64 tasks apply to i386 with 32-bit operand size. |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| Intel 64 and IA-32 SDM Vol 1-3 | https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html | Primary ISA reference |
| FreeBSD i386 kernel entry (locore.s) | `sys/i386/i386/locore.s` | Boot sequence, initial page tables |
| FreeBSD i386 pmap | `sys/i386/i386/pmap.c` | Page table management reference |
| FreeBSD i386 trap handling | `sys/i386/i386/trap.c` | Exception dispatch reference |
| SeaBIOS | https://www.seabios.org/ | Legacy BIOS firmware (BSD-licensed) |

### 7.3 Shared Infrastructure

Same shared infrastructure as amd64 (see `003-Emulation-Arch-amd64.md` Section 7.3). The i386 module reuses the amd64 instruction decoder with 32-bit operand size configuration.

---

## 8. Notes

- The i386 architecture is largely a subset of amd64. The recommended implementation approach is to reuse the amd64 instruction decoder with a "32-bit mode" flag that controls operand size, REX prefix handling, and available instructions.
- FreeBSD i386 support is in maintenance mode but still receives security updates. The emulator should support it for legacy testing.
- Hardware task switching (via task gate in IDT) is unique to i386 and not available in x86-64. This is rarely used by modern FreeBSD but must be emulated for correctness.
- The INT 0x80 syscall mechanism is the traditional FreeBSD i386 syscall path. SYSENTER/SYSEXIT is the fast path on modern CPUs.
