# PowerPC Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | PowerISA (PowerPC), 32-bit and 64-bit variants |
| **Base specification** | PowerISA 2.xx (PowerPC), PowerISA 3.xx (POWER) |
| **FreeBSD support** | powerpc (32-bit), powerpc64 (64-bit BE), powerpc64le (64-bit LE) |
| **Emulation priority** | P2 |
| **bhyve support** | ❌ |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_powerpc.ko` |

### 1.1 CPU Levels / Feature Tiers

| CPU Level | Base ISA | Key Features | FreeBSD Target |
|-----------|----------|--------------|----------------|
| `ppc-g1` | PowerISA 1.xx | 32-bit, FPU, no AltiVec | 601/603/604 |
| `ppc-g2` | PowerISA 2.01 | 32-bit, AltiVec, Book-E | 74xx (G4) |
| `ppc-g3` | PowerISA 2.02 | 32/64-bit, AltiVec, hypervisor | 970 (G5) |
| `ppc-g4` | PowerISA 2.04 | 32/64-bit, AltiVec, VMX128 | e600 |
| `ppc-pwr8` | PowerISA 2.07B | 64-bit, VSX, little-endian support, crypto | POWER8 |
| `ppc-pwr9` | PowerISA 3.0B | 64-bit, VSX-3, radix MMU, strong crypto | POWER9 |
| `ppc-pwr10` | PowerISA 3.1 | 64-bit, VSX-4, MMA, prefix instructions | POWER10 |

**Default CPU level:** `ppc-pwr8` (widest FreeBSD powerpc64 support)

**Instance configuration field:** `cpu_level` (string, e.g., "ppc-pwr8", "ppc-g4")

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers

| Register | Width | Purpose |
|----------|-------|---------|
| GPR0-GPR31 | 32/64-bit | General-purpose registers (GPR0=0 on some ops, GPR1=stack pointer) |
| LR | 32/64-bit | Link Register (return address) |
| CTR | 32/64-bit | Count Register (loop counter, branch target) |
| XER | 32-bit | Fixed-Point Exception Register (carry, overflow, summary overflow, compare bytes) |
| CR | 32-bit | Condition Register (8 fields, 4 bits each: LT, GT, EQ, SO) |
| MSR | 32/64-bit | Machine State Register |
| SRR0/SRR1 | 32/64-bit | Save/Restore Register 0/1 (exception return) |
| SPRG0-SPRG7 | 32/64-bit | Special-Purpose Registers (OS use) |
| DAR | 32/64-bit | Data Address Register (fault address) |
| DSISR | 32-bit | Data Storage Interrupt Status Register |
| DEC | 32/64-bit | Decrementer (timer) |
| TB | 64-bit | Time Base (free-running counter) |
| PVR | 32-bit | Processor Version Register |
| SDR1 | 32-bit | Page table base (32-bit hash MMU) |

**Floating-point registers:** FPR0-FPR31 (64-bit each)

**AltiVec/VMX registers:** VR0-VR31 (128-bit each), VSCR, VRSAVE

**VSX registers:** VSR0-VSR63 (128-bit each, overlapping FPR and VR)

### 2.2 Instruction Categories

| Category | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **Integer arithmetic** | ADD, ADDC, ADDE, ADDME, ADDZE, SUBF, SUBFC, SUBFE, SUBFME, SUBFZE, NEG, MULHW, MULHWU, MULLW, MULLD, DIVW, DIVWU, DIVD, DIVDU | P0 | Fixed-point arithmetic |
| **Integer compare** | CMP, CMPI, CMPL, CMPLI, CMPB | P0 | Compare and set CR |
| **Logical** | AND, ANDC, ANDIS, NAND, OR, ORC, ORIS, NOR, XOR, XORIS, EQV | P0 | Bitwise operations |
| **Shift/rotate** | SLW, SLD, SRW, SRD, SRAW, SRAD, SRAW, SRAD, RLWINM, RLWIMI, RLWNM, RLDICL, RLDICR, RLDIMI, RLDCL, RLDCR | P0 | Shift and rotate with mask |
| **Load/store** | LBZ, LBZU, LHZ, LHZU, LHA, LHAU, LWZ, LWZU, LD, LDU, LWA, STB, STBU, STH, STHU, STW, STWU, STD, STDU | P0 | Single register load/store |
| **Load/store multiple** | LMW, STMW | P0 | Block load/store |
| **Load/store string** | LSWI, LSWX, STSWI, STSWX | P1 | String load/store |
| **Branch** | B, BA, BL, BLA, BC, BCA, BCL, BCLA, BCLR, BCLRL, BCCTR, BCCTRL | P0 | Branch with CR and counter |
| **Condition register** | CRAND, CRNAND, CROR, CRNOR, CRXOR, CREQV, CRANDC, CRORC, MCRF, MCRXRX | P0 | CR logical operations |
| **System** | SC (syscall), RFI, RFID, HRFID, MTMSR, MFMSR, MTMSRD, MTSR, MFSR, MTSRIN, MFSRIN, MTSPR, MFSPR, MTDEC, MFDEC, MTCR, MFCR | P0 | System call, SPR access |
| **TLB** | TLBIE, TLBSYNC, TLBIVAX, SLBIA, SLBIE, SLBMFEEV, SLBMTEEV | P0 | TLB management |
| **Floating-point** | FADD, FADDS, FSUB, FSUBS, FMUL, FMULS, FDIV, FDIVS, FSQRT, FSQRTS, FCMPU, FCMPO, FCTIW, FCTIWZ, FCTID, FCTIDZ, FRSP, FMR, FNEG, FABS, FNABS, FSEL, FMADD, FMADDS, FMSUB, FMSUBS, FNMADD, FNMADDS, FNMSUB, FNMSUBS | P1 | FPU arithmetic |
| **AltiVec/VMX** | VADDUWM, VSUBUWM, VMUL, VMSUM, VPERM, VSEL, VSLW, VSRW, VRLW, VMAX, VMIN, VCMPEQ, VCMPGT, VCMPGE, LVX, STVX, LVSL, LVSR, LVXL, STVXL | P1 | SIMD (G4+) |
| **VSX** | XSADDDP, XSSUBDP, XSMULDP, XSDIVDP, XVADDDP, XVSUBDP, XVMULDP, XVDIVDP, XSCMPUDP, XSCMPODP, XVCMPEQDP, XVCMPGTDP, XVCMPGEDP, LXVD2X, STXVD2X, XXPERMDI, XXSLDWI, XXSEL, XXMRG, XXSPLTW | P2 | Vector-Scalar Extension (POWER8+) |
| **Decimal floating-point** | DADD, DSUB, DMUL, DDIV, DCMPU, DCMPO, DCTFIX, DCTFIXQ, DBCD, DCCCI, DMC, DQUA | P2 | DFP (POWER6+) |
| **Crypto** | VCIPHER, VCIPHERLAST, VSBOX, VNCIPHER, VNCIPHERLAST, VSHA256, VSHA512, VPMSUM, VSHASIGM | P2 | AES/SHA (POWER8+) |

### 2.3 Instruction Encoding

PowerPC uses fixed 32-bit instruction encoding:

| Bit Range | Field | Description |
|-----------|-------|-------------|
| 31-26 | Opcode | Primary opcode (6 bits, 0-63) |
| 25-21 | RT/RS | Target/source register |
| 20-16 | RA | First source register |
| 15-11 | RB | Second source register |
| 10-1 | XO | Extended opcode (10 bits) |
| 0 | Rc | Record bit (set CR0) |

**Implementation approach:** Decode using a 64-entry primary opcode table, with extended opcode tables for opcodes 4, 19, 31, 59, 60, 61, 62, 63.

---

## 3. MMU / Paging

### 3.1 Hash MMU (PowerPC 32-bit and 64-bit)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (base), 64KB, 256KB, 1MB, 16MB (large pages) |
| **Page table** | Hashed page table (hardware-controlled) |
| **SDR1** | Page table base address and size (32-bit) |
| **HTAB** | Hash Table (64-bit, controlled by HTABORG/HTABSIZE) |
| **SLB** | Segment Lookaside Buffer (64 entries, 64-bit) |
| **Segment size** | 256MB segments (32-bit), 1TB segments (64-bit) |

### 3.2 Radix MMU (POWER9+)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB, 64KB, 2MB, 1GB |
| **Page table levels** | 4 or 5 levels (similar to AArch64) |
| **Virtual address bits** | 48-bit or 52-bit |
| **PATE** | Process Table Entry (radix tree root) |
| **PRTB** | Process Table (partitioned by PID) |

---

## 4. Interrupt and Exception Model

### 4.1 Interrupt Vectors (PowerPC 64-bit)

| Vector | Interrupt | Description |
|--------|-----------|-------------|
| 0x0100 | Critical Input | Critical external interrupt |
| 0x0200 | Machine Check | Machine check error |
| 0x0300 | Data Storage | Data access fault (DSI) |
| 0x0400 | Instruction Storage | Instruction fetch fault (ISI) |
| 0x0500 | External | External interrupt (IRQ) |
| 0x0600 | Alignment | Alignment exception |
| 0x0700 | Program | Program exception (illegal instruction, privilege) |
| 0x0800 | Floating-Point Unavailable | FPU disabled |
| 0x0900 | Decrementer | Decrementer timer |
| 0x0A00 | I/O Controller | I/O controller interrupt |
| 0x0B00 | Reserved | |
| 0x0C00 | System Call | SC instruction |
| 0x0D00 | Trace | Single-step trace |
| 0x0E00 | AltiVec Unavailable | AltiVec disabled |
| 0x0F00 | Unused | |
| 0x1000 | Performance Monitor | Performance monitor |
| 0x1200 | System Error | System error interrupt |
| 0x1300 | SMI | System management interrupt |
| 0x1400 | Critical Doorbell | Critical doorbell (IPI) |
| 0x1500 | Unused | |
| 0x1600 | Hypervisor | Hypervisor call |
| 0x1700 | Unused | |
| 0x1800 | Unused | |
| 0x1900 | Unused | |
| 0x1A00 | Unused | |
| 0x1B00 | Unused | |
| 0x1C00 | Unused | |
| 0x1D00 | Unused | |
| 0x1E00 | Unused | |
| 0x1F00 | Unused | |

---

## 5. Boot Process

### 5.1 FreeBSD Boot Sequence (powerpc)

1. **Firmware** (OpenFirmware/UBoot) — loaded from flash/disk
2. **loader** — loads kernel, modules
3. **kernel** — ELF binary loaded at appropriate address
4. **Kernel initialization:**
   - Entry point: `start` (locore.S)
   - `mi_startup()` — main initialization

### 5.2 Required Emulated Devices for Boot

| Device | Type | Address | Purpose |
|--------|------|---------|---------|
| **UART (NS16550)** | Serial | 0x1C090000 | Console output |
| **OpenPIC** | Interrupt | 0x40000 | Interrupt controller |
| **Decrementer** | Timer | SPR | Scheduling |
| **virtio-blk** | Storage | MMIO | Disk access |

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| PPC.1 | Implement PowerPC instruction decoder (32-bit fixed) | NOT STARTED | | | `sys/emulation/powerpc/emu_cpu_ppc.c` | 64-entry primary opcode table. Extended opcode tables for opcodes 4, 19, 31, 59, 60, 61, 62, 63. |
| PPC.2 | Implement integer arithmetic instructions (P0) | NOT STARTED | | PPC.1 | `sys/emulation/powerpc/emu_cpu_ppc.c` | ADD, ADDC, SUBF, MULLW, MULLD, DIVW, DIVD, NEG, etc. |
| PPC.3 | Implement logical and shift/rotate instructions (P0) | NOT STARTED | | PPC.2 | `sys/emulation/powerpc/emu_cpu_ppc.c` | AND, OR, XOR, NOR, SLW, SRW, SRAW, RLWINM, RLDICL, etc. |
| PPC.4 | Implement load/store instructions (P0) | NOT STARTED | | PPC.2 | `sys/emulation/powerpc/emu_cpu_ppc.c` | LBZ, LHZ, LWZ, LD, STB, STH, STW, STD, LMW, STMW |
| PPC.5 | Implement branch instructions (P0) | NOT STARTED | | PPC.3 | `sys/emulation/powerpc/emu_cpu_ppc.c` | B, BA, BL, BC, BCLR, BCCTR. CR field conditions. |
| PPC.6 | Implement condition register operations (P0) | NOT STARTED | | PPC.3 | `sys/emulation/powerpc/emu_cpu_ppc.c` | CRAND, CROR, CRXOR, MCRF, MCRXRX |
| PPC.7 | Implement system instructions (P0) | NOT STARTED | | PPC.5 | `sys/emulation/powerpc/emu_cpu_ppc.c` | SC, RFI, RFID, MTMSR, MFMSR, MTSPR, MFSPR |
| PPC.8 | Implement hash MMU (P0) | NOT STARTED | | | `sys/emulation/powerpc/emu_mmu_ppc.c` | SDR1/HTAB. SLB management. Page table walk. |
| PPC.9 | Implement radix MMU (P1) | NOT STARTED | | PPC.8 | `sys/emulation/powerpc/emu_mmu_ppc.c` | Radix tree walk (POWER9+). PATE/PRTB. |
| PPC.10 | Implement exception handling (P0) | NOT STARTED | | PPC.7 | `sys/emulation/powerpc/emu_intr_ppc.c` | Vector table. SRR0/SRR1. MSR save/restore. |
| PPC.11 | Implement OpenPIC interrupt controller (P0) | NOT STARTED | | PPC.10 | `sys/emulation/powerpc/emu_intr_ppc.c` | Interrupt routing. Timer. IPI. |
| PPC.12 | Implement decrementer timer (P0) | NOT STARTED | | PPC.10 | `sys/emulation/powerpc/emu_intr_ppc.c` | DEC SPR. Auto-decrement. Timer interrupt. |
| PPC.13 | Implement floating-point instructions (P1) | NOT STARTED | | PPC.2 | `sys/emulation/powerpc/emu_cpu_ppc.c` | FADD, FSUB, FMUL, FDIV, FSQRT, FCMPU, FCTIWZ, FRSP, FMADD |
| PPC.14 | Implement AltiVec/VMX instructions (P1) | NOT STARTED | | PPC.13 | `sys/emulation/powerpc/emu_cpu_ppc.c` | VADDUWM, VSUBUWM, VMUL, VPERM, VSEL, LVX, STVX |
| PPC.15 | Implement VSX instructions (P2) | NOT STARTED | | PPC.14 | `sys/emulation/powerpc/emu_cpu_ppc.c` | XSADDDP, XVADDDP, LXVD2X, STXVD2X (POWER8+) |
| PPC.16 | Implement U-Boot firmware loading (P0) | NOT STARTED | | PPC.1 | `usr.sbin/emu/emu_arch_ppc.c` | Load U-Boot binary. Generate device tree. |
| PPC.17 | Write PowerPC CPU emulation unit tests | NOT STARTED | | PPC.1-PPC.16 | `tests/sys/emulation/powerpc/` | Test integer ops. Test load/store. Test branches. Test MMU. Test exceptions. Test FPU. |
| PPC.18 | Write PowerPC boot integration test | NOT STARTED | | PPC.16 | `tests/usr.sbin/emu/ppc_boot_test.sh` | Boot FreeBSD powerpc kernel in emulator. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 3 (Architecture-Specific CPU Emulation). |
| `002-Emulation-Security-FS.md` | Security architecture. |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| PowerISA 2.07B (POWER8) | https://openpowerfoundation.org/specifications/ | Primary ISA reference |
| PowerISA 3.0B (POWER9) | https://openpowerfoundation.org/specifications/ | ISA extensions |
| FreeBSD powerpc kernel entry | `sys/powerpc/powerpc/locore.S` | Boot sequence |
| FreeBSD powerpc pmap | `sys/powerpc/powerpc/pmap.c` | MMU reference |
| FreeBSD powerpc OpenPIC driver | `sys/powerpc/powerpc/openpic.c` | Interrupt controller reference |
| QEMU PowerPC target | https://github.com/qemu/qemu/tree/master/target/ppc | Reference implementation |

---

## 8. Notes

- PowerPC has a clean, fixed 32-bit instruction encoding that is simpler to decode than x86.
- The PowerISA has multiple revisions (2.xx for PowerPC, 3.xx for POWER). Focus on PowerISA 2.07B (POWER8) for FreeBSD powerpc64 support.
- The hash MMU is unique to PowerPC and differs significantly from x86 and ARM page table walks.
- FreeBSD powerpc64le (little-endian) support was added for POWER8+. The emulator must support both big-endian and little-endian modes.
