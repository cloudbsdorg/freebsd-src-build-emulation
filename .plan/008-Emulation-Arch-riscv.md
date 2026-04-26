# RISC-V Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | RISC-V, 32-bit (RV32) and 64-bit (RV64) variants |
| **Base specification** | RISC-V Instruction Set Manual Volumes 1-2 (Unprivileged and Privileged) |
| **FreeBSD support** | riscv64 (primary target) |
| **Emulation priority** | P1 |
| **bhyve support** | ❌ |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_riscv.ko` |

### 1.1 CPU Levels / Feature Tiers

| CPU Level | Base ISA | Extensions | FreeBSD Target |
|-----------|----------|------------|----------------|
| `rv64imafd` | RV64I | M (multiply), A (atomics), F (float), D (double) | Baseline riscv64 |
| `rv64imafdc` | RV64I + | C (compressed instructions) | Common embedded |
| `rv64imafdcv` | RV64I + | V (vector extension) | Vector-capable |
| `rv32imafd` | RV32I | M, A, F, D | Baseline riscv32 |
| `rv32imac` | RV32I | M, A, C | Embedded riscv32 |

**Default CPU level:** `rv64imafd` (FreeBSD riscv64 target)

**Instance configuration field:** `cpu_level` (string, e.g., "rv64imafd", "rv64imafdc")

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers

| Register | ABI Name | Width | Purpose |
|----------|----------|-------|---------|
| X0 | zero | 32/64-bit | Hardwired zero (reads as 0, writes ignored) |
| X1 | ra | 32/64-bit | Return address |
| X2 | sp | 32/64-bit | Stack pointer |
| X3 | gp | 32/64-bit | Global pointer |
| X4 | tp | 32/64-bit | Thread pointer |
| X5 | t0 | 32/64-bit | Temporary/link register |
| X6-X7 | t1-t2 | 32/64-bit | Temporaries |
| X8 | s0/fp | 32/64-bit | Callee-saved / frame pointer |
| X9 | s1 | 32/64-bit | Callee-saved |
| X10-X11 | a0-a1 | 32/64-bit | Function arguments / return values |
| X12-X17 | a2-a7 | 32/64-bit | Function arguments |
| X18-X27 | s2-s11 | 32/64-bit | Callee-saved |
| X28-X31 | t3-t6 | 32/64-bit | Temporaries |
| PC | — | 32/64-bit | Program counter |

**Floating-point registers (if F/D extension):**
- F0-F31: 32-bit (single-precision, RV32F/RV64F)
- F0-F31: 64-bit (double-precision, RV64D — holds both SP and DP values)

**Vector registers (if V extension):**
- V0-V31: Variable width (VLEN bits, configurable 128-65536)

**CSRs (Control and Status Registers):**
- `mstatus` — Machine status (IE, WPRI, MPP, SPP, FS, XS, etc.)
- `misa` — Machine ISA (which extensions are enabled)
- `medeleg` — Machine exception delegation
- `mideleg` — Machine interrupt delegation
- `mie` — Machine interrupt enable
- `mtvec` — Machine trap vector (base address + mode)
- `mscratch` — Machine scratch register
- `mepc` — Machine exception PC
- `mcause` — Machine exception cause
- `mtval` — Machine trap value (fault address, instruction)
- `mip` — Machine interrupt pending
- `satp` — Supervisor address translation and protection (MMU enable + page table root)
- `sstatus` — Supervisor status (subset of mstatus)
- `stvec` — Supervisor trap vector
- `sscratch` — Supervisor scratch
- `sepc` — Supervisor exception PC
- `scause` — Supervisor exception cause
- `stval` — Supervisor trap value
- `sip` — Supervisor interrupt pending
- `sie` — Supervisor interrupt enable
- `time` — Wall-clock timer (read-only, in time CSR)
- `cycle` — Cycle counter
- `instret` — Instructions retired counter

### 2.2 Instruction Categories

| Category | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **Integer arithmetic (RV64I)** | LUI, AUIPC, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND | P0 | Core ALU operations |
| **Wide arithmetic (RV64I)** | ADDIW, SLLIW, SRLIW, SRAIW, ADDW, SUBW, SLLW, SRLW, SRAW | P0 | 32-bit operations with sign extension |
| **Load (RV64I)** | LB, LH, LW, LD, LBU, LHU, LWU | P0 | Load byte/half/word/doubleword |
| **Store (RV64I)** | SB, SH, SW, SD | P0 | Store byte/half/word/doubleword |
| **Fence** | FENCE, FENCE.I | P0 | Memory ordering, instruction fence |
| **Branch** | BEQ, BNE, BLT, BGE, BLTU, BGEU | P0 | Conditional branches |
| **Jump** | JAL, JALR | P0 | Jump and link |
| **System** | ECALL, EBREAK, CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI, MRET, SRET, WFI | P0 | System calls, CSR access, trap return |
| **Multiply (M ext)** | MUL, MULH, MULHU, MULHSU, DIV, DIVU, REM, REMU, MULW, DIVW, DIVUW, REMW, REMUW | P0 | Integer multiply/divide |
| **Atomics (A ext)** | LR.W, SC.W, LR.D, SC.D, AMOSWAP.W, AMOADD.W, AMOAND.W, AMOOR.W, AMOXOR.W, AMOMIN.W, AMOMAX.W, AMOMINU.W, AMOMAXU.W, AMOSWAP.D, AMOADD.D, AMOAND.D, AMOOR.D, AMOXOR.D, AMOMIN.D, AMOMAX.D, AMOMINU.D, AMOMAXU.D | P0 | Atomic memory operations |
| **Floating-point (F ext)** | FLW, FSW, FMADD.S, FMSUB.S, FNMSUB.S, FNMADD.S, FADD.S, FSUB.S, FMUL.S, FDIV.S, FSQRT.S, FSGNJ.S, FSGNJN.S, FSGNJX.S, FMIN.S, FMAX.S, FCVT.W.S, FCVT.WU.S, FCVT.S.W, FCVT.S.WU, FEQ.S, FLT.S, FLE.S, FCLASS.S, FCVT.L.S, FCVT.LU.S, FCVT.S.L, FCVT.S.LU | P1 | Single-precision FP |
| **Double-precision (D ext)** | FLD, FSD, FMADD.D, FMSUB.D, FNMSUB.D, FNMADD.D, FADD.D, FSUB.D, FMUL.D, FDIV.D, FSQRT.D, FSGNJ.D, FSGNJN.D, FSGNJX.D, FMIN.D, FMAX.D, FCVT.W.D, FCVT.WU.D, FCVT.D.W, FCVT.D.WU, FEQ.D, FLT.D, FLE.D, FCLASS.D, FCVT.L.D, FCVT.LU.D, FCVT.D.L, FCVT.D.LU, FCVT.S.D, FCVT.D.S | P1 | Double-precision FP |
| **Compressed (C ext)** | C.LW, C.LD, C.LWSP, C.LDSP, C.SW, C.SD, C.SWSP, C.SDSP, C.ADDI, C.ADDIW, C.ADDI16SP, C.LUI, C.SRLI, C.SRAI, C.ANDI, C.SUB, C.XOR, C.OR, C.AND, C.SUBW, C.ADDW, C.J, C.JR, C.JALR, C.BEQZ, C.BNEZ, C.LI, C.MV, C.ADD, C.EBREAK, C.NOP | P0 | 16-bit compressed instructions |
| **Vector (V ext)** | VADD, VSUB, VMUL, VDIV, VFADD, VFSUB, VFMUL, VFDIV, VFMADD, VFMSUB, VFNMADD, VFNMSUB, VLE, VSE, VLSE, VSSE, VMSEQ, VMSNE, VMSLT, VMSLE, VMSGT, VAND, VOR, VXOR, VREDSUM, VREDMAX, VREDMIN, VFMV, VFCLASS, VFCVT, VMV, VCPOP, VFIRST, VEXT, VSLIDEUP, VSLIDEDOWN, VID, VIOTA, VCOMPRESS | P2 | Vector extension |

### 2.3 Instruction Encoding

RISC-V uses fixed 32-bit instruction encoding (plus 16-bit compressed instructions):

**32-bit instruction format:**

| Format | Bits 31-25 | Bits 24-20 | Bits 19-15 | Bits 14-12 | Bits 11-7 | Bits 6-0 |
|--------|-----------|-----------|-----------|-----------|----------|---------|
| R-type | funct7 | rs2 | rs1 | funct3 | rd | opcode |
| I-type | imm[11:0] | | rs1 | funct3 | rd | opcode |
| S-type | imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode |
| B-type | imm[12,10:5] | rs2 | rs1 | funct3 | imm[4:1,11] | opcode |
| U-type | imm[31:12] | | | | rd | opcode |
| J-type | imm[20,10:1,11,19:12] | | | | rd | opcode |

**16-bit compressed instruction format (C extension):**

| Format | Bits 15-13 | Bits 12 | Bits 11-7 | Bits 6-2 | Bits 1-0 |
|--------|-----------|--------|-----------|---------|---------|
| Quadrant 0 | funct3 | Various | Various | Various | 00 |
| Quadrant 1 | funct3 | Various | Various | Various | 01 |
| Quadrant 2 | funct3 | Various | Various | Various | 10 |

**Implementation approach:** Decode using the 7-bit opcode as the primary dispatch, then funct3/funct7 for sub-decoding. For compressed instructions, use bits 1-0 as quadrant selector, then funct3 for sub-decoding.

---

## 3. MMU / Paging

### 3.1 RISC-V Sv39 Page Table (Default for RV64)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 2MB (megapage), 1GB (gigapage) |
| **Page table levels** | 3 (Sv39) |
| **Virtual address bits** | 39-bit (bits 38-0, bits 63-39 must equal bit 38) |
| **Physical address bits** | 56-bit (configurable via satp) |
| **satp.MODE** | 8=Sv39, 9=Sv48, 10=Sv57 |
| **satp.ASID** | Address Space ID (16 bits) |
| **satp.PPN** | Page table root (physical page number) |

**Page table entry format (64-bit):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | V | Valid |
| 1 | R | Readable |
| 2 | W | Writable |
| 3 | X | Executable |
| 4 | U | User (accessible from U-mode) |
| 5 | G | Global (not flushed on SFENCE.VMA ASID) |
| 6 | A | Accessed (set by hardware on access) |
| 7 | D | Dirty (set by hardware on write) |
| 8-9 | RSW | Reserved for supervisor software |
| 10-53 | PPN | Physical Page Number (44 bits for Sv39) |
| 54-63 | | Reserved |

**Page table walk (Sv39):**
```
// satp.MODE must be 8 (Sv39)
root_ppn = satp.PPN

Level 0: VPN[2] = VA[38:30], PTE = ReadPhys(root_ppn << 12 + VPN[2] * 8)
  Check PTE.V == 1, else page fault
  If PTE.R == 0 && PTE.W == 0 && PTE.X == 0: next level (pointer PTE)
  Else: leaf PTE → check permissions, check A/D bits

Level 1: VPN[1] = VA[29:21], PTE = ReadPhys(PTE.ppn << 12 + VPN[1] * 8)
  Check PTE.V == 1, else page fault
  If PTE.R == 0 && PTE.W == 0 && PTE.X == 0: next level (pointer PTE)
  Else: leaf PTE → 2MB megapage

Level 2: VPN[0] = VA[20:12], PTE = ReadPhys(PTE.ppn << 12 + VPN[0] * 8)
  Check PTE.V == 1, else page fault
  Leaf PTE → 4KB page

PhysAddr = PTE.ppn << 12 | VA[11:0]
```

### 3.2 Sv48 Page Table (Extended)

Same as Sv39 but with 4 levels and 48-bit virtual address:
- Level 0: VPN[3] = VA[47:39]
- Level 1: VPN[2] = VA[38:30]
- Level 2: VPN[1] = VA[29:21]
- Level 3: VPN[0] = VA[20:12]

### 3.3 TLB Simulation

| Feature | Implementation |
|---------|---------------|
| **Software TLB** | Hash table mapping (VA, ASID) → (PhysAddr, permissions) |
| **TLB flush** | SFENCE.VMA (with or without ASID, with or without address) |
| **ASID** | Address Space ID from satp.ASID |
| **TLB size** | Configurable (default: 32 entries, fully associative) |

---

## 4. Interrupt and Exception Model

### 4.1 Exception Causes (mcause/scause)

| Interrupt | Code | Exception Type | Description |
|-----------|------|----------------|-------------|
| 0 | 0 | Instruction address misaligned | PC not aligned to 2/4 bytes |
| 0 | 1 | Instruction access fault | MMU fault on instruction fetch |
| 0 | 2 | Illegal instruction | Unrecognized opcode |
| 0 | 3 | Breakpoint | EBREAK instruction |
| 0 | 4 | Load address misaligned | Unaligned load address |
| 0 | 5 | Load access fault | MMU fault on load |
| 0 | 6 | Store/AMO address misaligned | Unaligned store address |
| 0 | 7 | Store/AMO access fault | MMU fault on store |
| 0 | 8 | Environment call from U-mode | ECALL from user mode |
| 0 | 9 | Environment call from S-mode | ECALL from supervisor mode |
| 0 | 10 | Environment call from M-mode | Reserved (ECALL from M-mode) |
| 0 | 11 | Instruction page fault | Page fault on instruction fetch |
| 0 | 12 | Load page fault | Page fault on load |
| 0 | 13 | Reserved | |
| 0 | 14 | Store/AMO page fault | Page fault on store |
| 0 | 15 | Reserved | |
| 0 | 16-23 | Reserved | |
| 0 | 24 | Instruction guest-page fault | Hypervisor guest page fault |
| 0 | 25 | Load guest-page fault | Hypervisor guest page fault |
| 0 | 26 | Reserved | |
| 0 | 27 | Store/AMO guest-page fault | Hypervisor guest page fault |
| 1 | 1 | Supervisor software interrupt | Software IPI (S-mode) |
| 1 | 3 | Machine software interrupt | Software IPI (M-mode) |
| 1 | 5 | Supervisor timer interrupt | Timer interrupt (S-mode) |
| 1 | 7 | Machine timer interrupt | Timer interrupt (M-mode) |
| 1 | 9 | Supervisor external interrupt | External device interrupt (S-mode) |
| 1 | 11 | Machine external interrupt | External device interrupt (M-mode) |

### 4.2 Privilege Levels

| Level | Name | Purpose |
|-------|------|---------|
| 0 | U-mode | User applications |
| 1 | S-mode | Supervisor (OS kernel — FreeBSD runs here) |
| 2 | Reserved | |
| 3 | M-mode | Machine (firmware, boot ROM — OpenSBI runs here) |

### 4.3 Trap Handling

**Trap entry (M-mode or S-mode):**
1. `mcause`/`scause` = exception cause code
2. `mepc`/`sepc` = PC of trapping instruction
3. `mtval`/`stval` = fault address (for page faults) or instruction (for illegal instruction)
4. `mstatus.MPP`/`sstatus.SPP` = previous privilege level
5. `mstatus.MIE`/`sstatus.SIE` = previous interrupt enable
6. `mstatus.MIE`/`sstatus.SIE` = 0 (disable interrupts)
7. PC = `mtvec`/`stvec` (trap handler address)

**Trap return (MRET/SRET):**
1. PC = `mepc`/`sepc`
2. Privilege level = `mstatus.MPP`/`sstatus.SPP`
3. Interrupt enable = `mstatus.MIE`/`sstatus.SIE`

### 4.4 Interrupt Controller (CLINT/PLIC)

| Component | Address | Description |
|-----------|---------|-------------|
| **CLINT** | 0x02000000 | Core-Local Interrupt Controller (timer, software IPI) |
| **PLIC** | 0x0C000000 | Platform-Level Interrupt Controller (external devices) |

**CLINT registers:**
- `msip` (0x02000000 + hart*4) — Machine software interrupt pending (write 1 to IPI)
- `mtimecmp` (0x02004000 + hart*8) — Machine timer compare register
- `mtime` (0x0200BFF8) — Machine timer (free-running counter)

**PLIC registers:**
- Priority (0x0C000000 + irq*4) — Interrupt priority (0-7)
- Pending (0x0C001000 + bit/32) — Interrupt pending bits
- Enable (0x0C002000 + context*0x80 + bit/32) — Per-context enable bits
- Threshold (0x0C200000 + context*0x1000) — Priority threshold
- Claim/Complete (0x0C200004 + context*0x1000) — Claim and complete interrupt

---

## 5. Boot Process

### 5.1 FreeBSD Boot Sequence (riscv64)

1. **OpenSBI** (M-mode firmware) — loaded by boot ROM, provides SBI services
2. **loader** (S-mode) — loads kernel, modules, reads /boot/loader.conf
3. **kernel** (S-mode) — ELF binary loaded at appropriate DRAM address
4. **Kernel initialization:**
   - Entry point: `start` (locore.S) — set up page tables, enable MMU
   - `mi_startup()` — main initialization

### 5.2 Firmware Boot

| Firmware Type | Description | Emulation Required |
|---------------|-------------|-------------------|
| **OpenSBI** | M-mode firmware providing SBI (Supervisor Binary Interface) | OpenSBI binary integration |
| **U-Boot** | Boot loader for RISC-V | U-Boot binary integration |
| **Device tree** | FDT describing platform | Must generate DTB |

**Recommended approach:**
- Use **OpenSBI** as the M-mode firmware (BSD-licensed)
- Use **U-Boot** as the boot loader
- Generate a device tree blob (DTB) describing the emulated platform

### 5.3 Required Emulated Devices for Boot

| Device | Type | Address | Purpose |
|--------|------|---------|---------|
| **UART (NS16550)** | Serial | 0x10000000 | Console output |
| **CLINT** | Interrupt | 0x02000000 | Timer and software interrupts |
| **PLIC** | Interrupt | 0x0C000000 | External device interrupts |
| **virtio-blk** | Storage | MMIO | Disk access |
| **virtio-net** | Network | MMIO | Network access |

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| RISCV.1 | Implement RISC-V instruction decoder (32-bit) | NOT STARTED | | | `sys/emulation/riscv/emu_cpu_riscv.c` | 7-bit opcode dispatch. funct3/funct7 sub-decoding. |
| RISCV.2 | Implement compressed instruction decoder (16-bit, C ext) | NOT STARTED | | RISCV.1 | `sys/emulation/riscv/emu_cpu_riscv.c` | Quadrant-based dispatch. C.LW, C.LD, C.SW, C.SD, C.ADDI, C.J, C.BEQZ, etc. |
| RISCV.3 | Implement integer arithmetic instructions (P0) | NOT STARTED | | RISCV.1 | `sys/emulation/riscv/emu_cpu_riscv.c` | LUI, AUIPC, ADDI, SLTI, XORI, ORI, ANDI, SLLI, SRLI, SRAI, ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND |
| RISCV.4 | Implement wide arithmetic instructions (P0, RV64) | NOT STARTED | | RISCV.3 | `sys/emulation/riscv/emu_cpu_riscv.c` | ADDIW, SLLIW, SRLIW, SRAIW, ADDW, SUBW, SLLW, SRLW, SRAW |
| RISCV.5 | Implement load/store instructions (P0) | NOT STARTED | | RISCV.3 | `sys/emulation/riscv/emu_cpu_riscv.c` | LB, LH, LW, LD, LBU, LHU, LWU, SB, SH, SW, SD |
| RISCV.6 | Implement branch and jump instructions (P0) | NOT STARTED | | RISCV.3 | `sys/emulation/riscv/emu_cpu_riscv.c` | BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR |
| RISCV.7 | Implement system instructions (P0) | NOT STARTED | | RISCV.6 | `sys/emulation/riscv/emu_cpu_riscv.c` | ECALL, EBREAK, CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI, MRET, SRET, WFI |
| RISCV.8 | Implement multiply extension (M ext, P0) | NOT STARTED | | RISCV.3 | `sys/emulation/riscv/emu_cpu_riscv.c` | MUL, MULH, MULHU, MULHSU, DIV, DIVU, REM, REMU, MULW, DIVW, DIVUW, REMW, REMUW |
| RISCV.9 | Implement atomic extension (A ext, P0) | NOT STARTED | | RISCV.5 | `sys/emulation/riscv/emu_cpu_riscv.c` | LR.W, SC.W, LR.D, SC.D, AMOSWAP.W, AMOADD.W, AMOAND.W, AMOOR.W, AMOXOR.W, AMOMIN.W, AMOMAX.W, AMOMINU.W, AMOMAXU.W, and D variants |
| RISCV.10 | Implement Sv39 page table walk (P0) | NOT STARTED | | | `sys/emulation/riscv/emu_mmu_riscv.c` | 3-level page table. 4KB, 2MB, 1GB pages. satp.MODE=8. |
| RISCV.11 | Implement Sv48 page table walk (P1) | NOT STARTED | | RISCV.10 | `sys/emulation/riscv/emu_mmu_riscv.c` | 4-level page table. satp.MODE=9. |
| RISCV.12 | Implement TLB simulation (P0) | NOT STARTED | | RISCV.10 | `sys/emulation/riscv/emu_mmu_riscv.c` | Software TLB with ASID. SFENCE.VMA. |
| RISCV.13 | Implement trap handling (P0) | NOT STARTED | | RISCV.7 | `sys/emulation/riscv/emu_intr_riscv.c` | mcause/scause. mepc/sepc. mtval/stval. Trap entry/return. |
| RISCV.14 | Implement CLINT (P0) | NOT STARTED | | RISCV.13 | `sys/emulation/riscv/emu_intr_riscv.c` | MMIO at 0x02000000. msip, mtimecmp, mtime. Timer and software interrupts. |
| RISCV.15 | Implement PLIC (P0) | NOT STARTED | | RISCV.13 | `sys/emulation/riscv/emu_intr_riscv.c` | MMIO at 0x0C000000. Priority, pending, enable, threshold, claim/complete. |
| RISCV.16 | Implement floating-point extension (F/D ext, P1) | NOT STARTED | | RISCV.3 | `sys/emulation/riscv/emu_cpu_riscv.c` | FLW, FSW, FLD, FSD, FADD, FSUB, FMUL, FDIV, FSQRT, FCVT, FCMP, FCLASS |
| RISCV.17 | Implement vector extension (V ext, P2) | NOT STARTED | | RISCV.16 | `sys/emulation/riscv/emu_cpu_riscv.c` | VADD, VSUB, VMUL, VLE, VSE, VFADD, VFMADD, etc. |
| RISCV.18 | Implement OpenSBI firmware loading (P0) | NOT STARTED | | RISCV.1 | `usr.sbin/emu/emu_arch_riscv.c` | Load OpenSBI binary from blob cache via `emu_blob_resolve()`. M-mode reset vector. Provide SBI services. See `010-Emulation-Blob-Management.md`. |
| RISCV.19 | Implement U-Boot firmware loading (P0) | NOT STARTED | | RISCV.18 | `usr.sbin/emu/emu_arch_riscv.c` | Load U-Boot binary from blob cache via `emu_blob_resolve()`. Generate DTB. See `010-Emulation-Blob-Management.md`. |
| RISCV.20 | Implement device tree generation (P0) | NOT STARTED | | RISCV.19 | `usr.sbin/emu/emu_arch_riscv.c` | Generate FDT for RISC-V platform. CPU, CLINT, PLIC, UART, virtio. |
| RISCV.21 | Write RISC-V CPU emulation unit tests | NOT STARTED | | RISCV.1-RISCV.20 | `tests/sys/emulation/riscv/` | Test integer ops. Test compressed instructions. Test load/store. Test branches. Test atomics. Test MMU. Test traps. Test CLINT/PLIC. Test FPU. |
| RISCV.22 | Write RISC-V boot integration test | NOT STARTED | | RISCV.19 | `tests/usr.sbin/emu/riscv_boot_test.sh` | Boot FreeBSD riscv64 kernel in emulator. Verify console output. |
| RISCV.23 | Write RISC-V kernel module load test | NOT STARTED | | RISCV.22 | `tests/usr.sbin/emu/riscv_module_test.sh` | Load, verify, unload kernel module in emulated riscv64 environment. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 3 (Architecture-Specific CPU Emulation), Phase 5 (Custom Emulator Engine). |
| `002-Emulation-Security-FS.md` | Security architecture. |
| `010-Emulation-Blob-Management.md` | Blob management and CPU model database. Firmware loading (OpenSBI, U-Boot, DTB). |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| RISC-V Unprivileged Spec Vol 1 | https://riscv.org/technical/specifications/ | ISA reference |
| RISC-V Privileged Spec Vol 2 | https://riscv.org/technical/specifications/ | MMU, traps, CSRs |
| FreeBSD riscv64 kernel entry (locore.S) | `sys/riscv/riscv/locore.S` | Boot sequence |
| FreeBSD riscv64 pmap | `sys/riscv/riscv/pmap.c` | MMU reference |
| FreeBSD riscv64 trap handling | `sys/riscv/riscv/trap.c` | Exception dispatch reference |
| OpenSBI | https://github.com/riscv-software-src/opensbi | M-mode firmware (BSD-licensed) |
| QEMU RISC-V target | https://github.com/qemu/qemu/tree/master/target/riscv | Reference implementation |

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
| NS16550 UART | All architectures | `usr.sbin/emu/emu_dev_uart.c` |
| virtio-blk | All architectures | `usr.sbin/emu/emu_dev_storage.c` |
| virtio-net | All architectures | `usr.sbin/emu/emu_dev_net.c` |

---

## 8. Notes

- RISC-V has the cleanest ISA design of all supported architectures. Fixed 32-bit encoding (plus 16-bit compressed), regular format fields, and a small base instruction set.
- The M-mode/S-mode privilege separation means the emulator must support at least two privilege levels. OpenSBI handles M-mode, FreeBSD runs in S-mode.
- The RISC-V vector extension (V) is still evolving. Implement the ratified v1.0 specification.
- RISC-V does not have a dedicated hardware interrupt controller like x86's APIC or ARM's GIC. Instead, it uses CLINT (timer + software interrupts) and PLIC (external interrupts), which are simpler to emulate.
- FreeBSD riscv64 is a Tier 2 architecture but is actively developed.
