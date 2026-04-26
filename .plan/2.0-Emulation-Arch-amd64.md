# AMD64 (x86-64) Architecture Emulation — Implementation Plan

## 1. Architecture Overview

| Property | Value |
|----------|-------|
| **ISA** | x86-64 (AMD64), with x86-32 (IA-32e compatibility mode) and x86-16 (real mode) support |
| **Base specification** | AMD64 Architecture Programmer's Manual Volumes 1-3, Intel 64 and IA-32 Architectures SDM Volumes 1-3 |
| **FreeBSD support** | amd64 (primary target), i386 (32-bit compatibility) |
| **Emulation priority** | P0 (highest — primary development target) |
| **bhyve support** | ✅ Native (hardware VMM) |
| **Custom emulator** | ✅ Planned |
| **Kernel module** | `emu_amd64.ko` |

### 1.1 CPU Levels / Feature Tiers

The instance configuration must specify which CPU level to emulate. This controls which CPUID features are exposed and which instruction set extensions are available.

| CPU Level | Base ISA | Key Features | FreeBSD Target |
|-----------|----------|--------------|----------------|
| `x86-64` | x86-64 (AMD64) | CMPXCHG8B, CMPXCHG16B, LAHF/SAHF, POPCNT, SSE, SSE2, NX bit | Baseline amd64 |
| `x86-64-v2` | x86-64 + | SSE3, SSE4.1, SSE4.2, SSSE3, POPCNT, CX16 | Modern amd64 |
| `x86-64-v3` | x86-64-v2 + | AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, XSAVE | Haswell-era+ |
| `x86-64-v4` | x86-64-v3 + | AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL | Skylake-era+ |
| `i386` | IA-32 (32-bit) | CMPXCHG8B, SSE2, PAE, PSE36 | i386 (32-bit) |
| `i486` | i386 + | CMPXCHG, XADD, BSWAP | Legacy 32-bit |
| `i586` | i486 + | MMX, conditional move | Pentium-class |
| `i686` | i586 + | SSE, P6 features, CMOV | Pentium Pro+ |

**Default CPU level:** `x86-64` (baseline, widest compatibility)

**Instance configuration field:** `cpu_level` (string, e.g., "x86-64-v3", "i686")

---

## 2. Instruction Set Architecture

### 2.1 General-Purpose Registers (x86-64 mode)

| Register | Width | Purpose |
|----------|-------|---------|
| RAX | 64-bit | Accumulator, return value, syscall number |
| RBX | 64-bit | Base register (callee-saved) |
| RCX | 64-bit | Counter, 4th syscall arg |
| RDX | 64-bit | Data, 3rd syscall arg |
| RSI | 64-bit | Source, 2nd syscall arg |
| RDI | 64-bit | Destination, 1st syscall arg |
| RBP | 64-bit | Base pointer (callee-saved, frame pointer) |
| RSP | 64-bit | Stack pointer |
| R8-R15 | 64-bit | Extended registers (R8-R15) |
| RIP | 64-bit | Instruction pointer |
| RFLAGS | 64-bit | Status flags (low 32 bits used, upper 32 reserved) |

**Segment registers:** CS, DS, ES, FS, GS, SS (each 16-bit selector + 64-bit base hidden)

**Control registers:** CR0, CR2, CR3, CR4, CR8

**Debug registers:** DR0-DR7

**x87 FPU registers:** ST0-ST7 (80-bit each), FPU control/status/tag words

**SSE/AVX registers:** XMM0-XMM15 (128-bit), YMM0-YMM15 (256-bit), ZMM0-ZMM15 (512-bit, AVX-512)

**MSRs (Model-Specific Registers):** Key MSRs for emulation:
- `MSR_EFER` (0xC0000080) — Extended Feature Enable Register (SCE, LME, LMA, NXE, SVME, FFXSR)
- `MSR_STAR` (0xC0000081) — SYSCALL target (CS/SS, EIP)
- `MSR_LSTAR` (0xC0000082) — Long mode SYSCALL target RIP
- `MSR_CSTAR` (0xC0000083) — Compatibility mode SYSCALL target RIP
- `MSR_SF_MASK` (0xC0000084) — SYSCALL flag mask
- `MSR_FSBASE` (0xC0000100) — FS segment base
- `MSR_GSBASE` (0xC0000101) — GS segment base
- `MSR_KGSBASE` (0xC0000102) — Kernel GS base (swapgs)
- `MSR_MTRR*` — Memory type range registers
- `MSR_APIC_BASE` (0x1B) — Local APIC base address
- `MSR_TSC` (0x10) — Time-stamp counter
- `MSR_PAT` (0x277) — Page attribute table
- `MSR_SYSENTER_CS/EIP/ESP` (0x174-0x176) — SYSENTER/SYSEXIT targets

### 2.2 Instruction Categories

The emulator must implement the following instruction categories. Priority levels: **P0** (boot FreeBSD), **P1** (run kernel modules), **P2** (full application support).

| Category | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **Data movement** | MOV, MOVZX, MOVSX, MOVSXD, CMOVcc, XCHG, BSWAP, PUSH, POP, PUSHA/POPA (32-bit only), MOVD/MOVQ (MMX/SSE) | P0 | Foundation for all execution |
| **Arithmetic** | ADD, SUB, ADC, SBB, INC, DEC, NEG, CMP, MUL, IMUL, DIV, IDIV | P0 | Integer arithmetic |
| **Logical** | AND, OR, XOR, NOT, TEST, ANDN (BMI1) | P0 | Bitwise operations |
| **Shift/Rotate** | SHL/SAL, SHR, SAR, ROL, ROR, RCL, RCR, SHLD, SHRD | P0 | Bit manipulation |
| **Bit manipulation** | BT, BTS, BTR, BTC, BSF, BSR, POPCNT, LZCNT, TZCNT, BEXTR (BMI1/2) | P1 | Bit test and search |
| **Control transfer** | Jcc (conditional), JMP (unconditional), CALL, RET, RETF, IRET, IRETD, IRETQ | P0 | Branching and calls |
| **String operations** | MOVS, CMPS, SCAS, LODS, STOS, INS, OUTS (with REP prefix) | P0 | Memory string ops |
| **Flag control** | STC, CLC, CMC, STD, CLD, SAHF, LAHF, PUSHF/POPF, PUSHFD/POPFD, PUSHFQ/POPFQ | P0 | Flag manipulation |
| **Segment registers** | MOV (to/from seg reg), PUSH/POP seg, LDS, LES, LFS, LGS, LSS, ARPL | P1 | Segment management |
| **System registers** | MOV CRn, MOV DRn, LMSW, SMSW | P0 | Control/debug register access |
| **System instructions** | SYSCALL, SYSRET, SYSENTER, SYSEXIT, INT n, INTO, INT3, BOUND, UD2, HLT, WAIT, NOP | P0 | System calls and interrupts |
| **Protected mode** | LGDT, SGDT, LIDT, SIDT, LLDT, SLDT, LTR, STR, VERR, VERW, LAR, LSL | P0 | Descriptor table management |
| **Paging** | INVLPG, INVPCID, INVEPT, INVVPID | P0 | TLB management |
| **FPU** | FADD, FSUB, FMUL, FDIV, FCOM, FST, FLD, FILD, FISTP, FINIT, FCLEX, FLDCW, FSTCW, FSTSW, FXAM, FTST, FUCOM, FCHS, FABS, FSQRT, FPREM, FRNDINT, FSCALE, FXTRACT, FBLD, FBSTP, FXCH, FCMOVcc, FCOMPP, FIADD, FISUB, FIMUL, FIDIV, FICOM, FICOMP | P1 | x87 FPU for legacy code |
| **MMX** | PADD, PSUB, PMUL, PAND, POR, PXOR, PCMP, PACK, PUNPCK, MOVD, MOVQ, EMMS | P1 | Multimedia extensions |
| **SSE/SSE2** | MOVAPS, MOVUPS, MOVSS, MOVSD, ADDPS, ADDSS, ADDSD, SUBPS, SUBSS, SUBSD, MULPS, MULSS, MULSD, DIVPS, DIVSS, DIVSD, SQRTPS, SQRTSS, SQRTSD, CMPPS, CMPSS, CMPSD, ANDPS, ANDNPS, ORPS, XORPS, SHUFPS, UNPCKHPS, UNPCKLPS, CVTSI2SS, CVTSI2SD, CVTTSS2SI, CVTTSD2SI, CVTSS2SD, CVTSD2SS, PADD, PSUB, PMUL, PAND, POR, PXOR, PCMP, PACK, PUNPCK, MOVDQA, MOVDQU, MOVQ, PSHUFD, PSHUFHW, PSHUFLW, PSLL, PSRL, PSRA | P1 | SIMD for kernel and userspace |
| **SSE3/SSSE3** | ADDSUBPS, ADDSUBPD, MOVSHDUP, MOVSLDUP, MOVDDUP, HADDPS, HSUBPS, HADDPD, HSUBPD, PHADD, PHSUB, PMADDUBSW, PMULHRSW, PSHUFB, PSIGN, PALIGNR, PABSB, PABSW, PABSD | P2 | Supplemental SIMD |
| **SSE4.1/4.2** | PMULDQ, PMULLD, PHMINPOSUW, PCMPEQQ, PACKUSDW, PMOVSX, PMOVZX, PTEST, ROUNDPS, ROUNDSS, ROUNDSD, INSERTPS, PINSRB, PINSRD, PINSRQ, PEXTRB, PEXTRW, PEXTRD, PEXTRQ, BLENDPS, BLENDPD, BLENDVPS, BLENDVPD, DPPS, DPPD, MPSADBW, PCMPISTRI, PCMPISTRM, PCMPESTRI, PCMPESTRM, CRC32, POPCNT | P2 | String/text processing, CRC |
| **AES** | AESENC, AESENCLAST, AESDEC, AESDECLAST, AESKEYGENASSIST, AESIMC | P2 | AES acceleration |
| **CLMUL** | PCLMULQDQ, PCLMULLQLQDQ, PCLMULHQLQDQ, PCLMULQHQDQ | P2 | Carry-less multiplication |
| **BMI1/BMI2** | ANDN, BEXTR, BLSI, BLSMSK, BLSR, TZCNT, MULX, RORX, SARX, SHLX, SHRX, PDEP, PEXT | P2 | Bit manipulation (x86-64-v3+) |
| **AVX/AVX2** | VEX-prefixed versions of SSE/SSE2/SSE3/SSSE3/SSE4.x, VBROADCAST, VINSERT, VEXTRACT, VPERM, VGATHER, VFMADD, VFMSUB, VFNMADD, VFNMSUB | P2 | Advanced SIMD (x86-64-v3+) |
| **AVX-512** | EVEX-prefixed versions, masked operations, scatter/gather, conflict detection | P2 | Extended SIMD (x86-64-v4+) |
| **Virtualization** | VMXON, VMXOFF, VMLAUNCH, VMRESUME, VMREAD, VMWRITE, VMCALL, VMCLEAR, VMPTRLD, VMPTRST, INVEPT, INVVPID, VMFUNC | P2 | Nested virtualization support |

### 2.3 Instruction Encoding

The x86-64 instruction encoding is variable-length (1-15 bytes) and complex:

| Byte Position | Field | Description |
|---------------|-------|-------------|
| 0 | Legacy prefixes | Optional: 0x66 (operand size), 0x67 (address size), 0xF0 (LOCK), 0xF2/F3 (REPNE/REP), segment overrides (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65) |
| 0-3 | REX/VEX/EVEX prefix | REX (0x40-0x4F): W/R/X/B bits. VEX (0xC4/0xC5): 2-3 byte. EVEX (0x62): 4-byte for AVX-512 |
| 1-2 | Opcode | 1-2 byte primary opcode. 0x0F prefix for 2-byte opcodes. 0x0F 0x3A/0x38 for 3-byte opcodes |
| 2-3 | ModRM | Mod (2 bits): register/indirect. Reg (3 bits): register/opcode ext. R/M (3 bits): operand |
| 3-4 | SIB | Scale (2 bits), Index (3 bits), Base (3 bits) — for indexed addressing |
| 1-4 | Displacement | 1, 2, or 4 byte signed displacement (8/32-bit in 64-bit mode) |
| 1-4 | Immediate | 1, 2, or 4 byte immediate value (8/32-bit in 64-bit mode) |

**Implementation approach:** Decode using a table-driven approach with opcode maps for:
- Primary opcode table (0x00-0xFF)
- Two-byte opcode table (0x0F 0x00-0xFF)
- Three-byte opcode tables (0x0F 0x38/0x3A 0x00-0xFF)
- VEX-mapped opcode tables
- EVEX-mapped opcode tables

---

## 3. MMU / Paging

### 3.1 x86-64 4-Level Paging (Default)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 2MB (large page), 1GB (huge page) |
| **Page table levels** | PML4 (Level 4), PDPT (Level 3), PD (Level 2), PT (Level 1) |
| **Virtual address bits** | 48-bit (canonical: bits 63-48 must be sign-extended from bit 47) |
| **Physical address bits** | Up to 52 bits (PAE required for >32-bit physical) |
| **Control register** | CR3 = PML4 base address (page-aligned) |
| **CR4.PAE** | Must be 1 for x86-64 long mode |
| **CR4.PSE** | Page Size Extensions (2MB pages) |
| **CR4.PGE** | Global pages (PGE flag in PTEs) |
| **CR4.SMEP** | Supervisor Mode Execution Prevention |
| **CR4.SMAP** | Supervisor Mode Access Prevention |
| **EFER.LME** | Long Mode Enable |
| **EFER.LMA** | Long Mode Active (set by hardware when LME=1 and CR0.PG=1) |
| **EFER.NXE** | No-Execute page protection |

**Page table entry format (4KB page):**

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0 | P | Present |
| 1 | R/W | Read/Write (0=read-only, 1=read-write) |
| 2 | U/S | User/Supervisor (0=supervisor, 1=user) |
| 3 | PWT | Page-level write-through |
| 4 | PCD | Page-level cache disable |
| 5 | A | Accessed |
| 6 | D | Dirty (only at leaf PTEs) |
| 7 | PAT | Page attribute table (only at leaf PTEs) |
| 8 | G | Global (only at leaf PTEs, CR4.PGE must be set) |
| 9-11 | AVL | Available for OS use |
| 12-51 | PhysAddr | Physical page frame address (bits 12-51) |
| 52-62 | AVL | Available for OS use |
| 63 | NX | No-Execute (if EFER.NXE=1) |

**Page table walk algorithm:**
```
PML4E = ReadPhys(CR3 + (VA[47:39] << 3))
Check PML4E.P == 1, else #PF
Check PML4E.R/W, U/S permissions

PDPTE = ReadPhys(PML4E[51:12] << 12 + (VA[38:30] << 3))
Check PDPTE.P == 1, else #PF
If PDPTE.PS == 1: 1GB page → PhysAddr = PDPTE[51:30] << 30 | VA[29:0]

PDE = ReadPhys(PDPTE[51:12] << 12 + (VA[29:21] << 3))
Check PDE.P == 1, else #PF
If PDE.PS == 1: 2MB page → PhysAddr = PDE[51:21] << 21 | VA[20:0]

PTE = ReadPhys(PDE[51:12] << 12 + (VA[20:12] << 3))
Check PTE.P == 1, else #PF
PhysAddr = PTE[51:12] << 12 | VA[11:0]
```

### 3.2 x86-64 5-Level Paging (Future)

For systems with >48-bit virtual address space (Intel 5-level paging):
- Adds PML5 table level
- Virtual address bits: 57-bit canonical
- CR4.LA57 enables 5-level paging
- **Priority:** P2 (not required for initial FreeBSD boot)

### 3.3 IA-32e Compatibility Mode (32-bit in 64-bit)

When CS.L=0 and CS.D=1 in long mode, the processor runs 32-bit compatibility mode:
- 32-bit virtual addresses (4GB max)
- Same 4-level page tables as x86-64 (but with 32-bit virtual address input)
- 32-bit general-purpose registers (EAX, EBX, etc.)
- 16-bit and 32-bit operand sizes

### 3.4 Legacy 32-bit Paging (i386 mode)

| Component | Description |
|-----------|-------------|
| **Page sizes** | 4KB (standard), 4MB (large page with CR4.PSE) |
| **Page table levels** | PD (Level 2), PT (Level 1) — or PDPT/PD/PT with PAE |
| **Virtual address bits** | 32-bit |
| **Physical address bits** | 32-bit (or 36-bit with PAE) |
| **CR4.PAE** | Physical Address Extension (36-bit physical) |
| **CR4.PSE** | Page Size Extension (4MB pages) |

**Without PAE (2-level paging):**
```
PDE = ReadPhys(CR3[31:12] << 12 + (VA[31:22] << 2))
Check PDE.P == 1, else #PF
If PDE.PS == 1: 4MB page → PhysAddr = PDE[31:22] << 22 | VA[21:0]

PTE = ReadPhys(PDE[31:12] << 12 + (VA[21:12] << 2))
Check PTE.P == 1, else #PF
PhysAddr = PTE[31:12] << 12 | VA[11:0]
```

**With PAE (3-level paging):**
```
PDPTE = CR3[31:5] << 5 + (VA[31:30] << 3)  (4 PDPTE registers)
Check PDPTE.P == 1, else #PF

PDE = ReadPhys(PDPTE[51:12] << 12 + (VA[29:21] << 3))
Check PDE.P == 1, else #PF
If PDE.PS == 1: 2MB page → PhysAddr = PDE[51:21] << 21 | VA[20:0]

PTE = ReadPhys(PDE[51:12] << 12 + (VA[20:12] << 3))
Check PTE.P == 1, else #PF
PhysAddr = PTE[51:12] << 12 | VA[11:0]
```

### 3.5 TLB Simulation

| Feature | Implementation |
|---------|---------------|
| **Software TLB** | Hash table mapping (VA, PCID) → (PhysAddr, permissions) |
| **TLB flush on CR3 write** | Invalidate all entries (or all non-global entries) |
| **INVLPG** | Invalidate single page entry |
| **INVPCID** | Invalidate by PCID (individual, all, all-non-global) |
| **Global pages** | CR4.PGE + PTE.G bit — entries survive CR3 write |
| **PCID** | Process Context ID (CR4.PCIDE) — tag TLB entries |
| **TLB size** | Configurable (default: 64 entries, 4-way associative) |

---

## 4. Interrupt and Exception Model

### 4.1 Interrupt Descriptor Table (IDT)

| Vector | Name | Type | Description |
|--------|------|------|-------------|
| 0 | #DE | Fault | Divide Error |
| 1 | #DB | Fault/Trap | Debug Exception |
| 2 | #NMI | Interrupt | Non-Maskable Interrupt |
| 3 | #BP | Trap | Breakpoint (INT3) |
| 4 | #OF | Trap | Overflow (INTO) |
| 5 | #BR | Fault | BOUND Range Exceeded |
| 6 | #UD | Fault | Undefined Opcode |
| 7 | #NM | Fault | Device Not Available (FPU) |
| 8 | #DF | Abort | Double Fault |
| 9 | | Fault | Coprocessor Segment Overrun (legacy) |
| 10 | #TS | Fault | Invalid TSS |
| 11 | #NP | Fault | Segment Not Present |
| 12 | #SS | Fault | Stack Segment Fault |
| 13 | #GP | Fault | General Protection Fault |
| 14 | #PF | Fault | Page Fault |
| 15 | | | Reserved |
| 16 | #MF | Fault | x87 FPU Floating-Point Error |
| 17 | #AC | Fault | Alignment Check |
| 18 | #MC | Abort | Machine Check |
| 19 | #XM | Fault | SIMD Floating-Point Exception |
| 20 | #VE | Fault | Virtualization Exception |
| 21-31 | | | Reserved |
| 32-255 | | Interrupt/Trap | User-defined (hardware interrupts, system calls) |

**IDT entry format (16 bytes, x86-64):**
```
Offset 0-1:   Offset[15:0] (low 16 bits of handler address)
Offset 2-3:   Segment selector (CS)
Offset 4:     IST (Interrupt Stack Table offset, bits 0-2)
Offset 5:     Type (bits 0-3): 0xE=interrupt gate, 0xF=trap gate, 0x5=task gate
              DPL (bits 5-6): Descriptor privilege level
              P (bit 7): Present
Offset 6-7:   Offset[31:16] (middle 16 bits)
Offset 8-11:  Offset[63:32] (high 32 bits)
Offset 12-15: Reserved (must be 0)
```

### 4.2 Exception Error Codes

| Exception | Error Code | Description |
|-----------|------------|-------------|
| #DF (8) | Always 0 | Double fault (always pushes 0) |
| #TS (10) | Segment selector + flags | Invalid TSS selector |
| #NP (11) | Segment selector + flags | Segment not present selector |
| #SS (12) | Segment selector or 0 | Stack segment selector or 0 for stack faults |
| #GP (13) | Segment selector or 0 | General protection selector or 0 |
| #PF (14) | Page fault error code | P (bit 0): 0=non-present, 1=protection violation. W (bit 1): 0=read, 1=write. U/S (bit 2): 0=supervisor, 1=user. RSVD (bit 3): reserved bit violation. I/D (bit 4): instruction fetch |
| #AC (17) | Always 0 | Alignment check |
| #VE (20) | VM-entry EPT violation info | Virtualization exception |

### 4.3 Task State Segment (TSS)

The x86-64 TSS is used for:
- **RSP0, RSP1, RSP2** — Stack pointers for privilege levels 0, 1, 2
- **IST1-IST7** — Interrupt Stack Table entries (alternative stacks for specific interrupts)
- **I/O map base** — I/O permission bitmap offset

**TSS format (x86-64, 128 bytes):**
```
Offset 0-3:   Reserved
Offset 4-7:   RSP0 (low 32 bits)
Offset 8-11:  RSP0 (high 32 bits)
Offset 12-15: RSP1 (low 32 bits)
Offset 16-19: RSP1 (high 32 bits)
Offset 20-23: RSP2 (low 32 bits)
Offset 24-27: RSP2 (high 32 bits)
Offset 28-31: Reserved
Offset 32-35: Reserved
Offset 36-39: IST1 (low 32 bits)
...           ...
Offset 100-103: I/O map base
```

### 4.4 Local APIC (LAPIC) Emulation

| Register | Offset | Description |
|----------|--------|-------------|
| LAPIC_ID | 0x20 | Local APIC ID |
| LAPIC_VER | 0x30 | Version register |
| LAPIC_TPR | 0x80 | Task Priority Register |
| LAPIC_APR | 0x90 | Arbitration Priority Register |
| LAPIC_PPR | 0xA0 | Processor Priority Register |
| LAPIC_EOI | 0xB0 | End of Interrupt |
| LAPIC_LDR | 0xD0 | Logical Destination Register |
| LAPIC_DFR | 0xE0 | Destination Format Register |
| LAPIC_SVR | 0xF0 | Spurious Interrupt Vector Register |
| LAPIC_ISR | 0x100-0x170 | In-Service Register (256 bits) |
| LAPIC_TMR | 0x180-0x1F0 | Trigger Mode Register (256 bits) |
| LAPIC_IRR | 0x200-0x270 | Interrupt Request Register (256 bits) |
| LAPIC_ESR | 0x280 | Error Status Register |
| LAPIC_ICR | 0x300-0x310 | Interrupt Command Register (64-bit) |
| LAPIC_LVT_TIMER | 0x320 | LVT Timer Register |
| LAPIC_LVT_THERMAL | 0x330 | LVT Thermal Sensor Register |
| LAPIC_LVT_PERFMON | 0x340 | LVT Performance Monitor Register |
| LAPIC_LVT_LINT0 | 0x350 | LVT LINT0 Register |
| LAPIC_LVT_LINT1 | 0x360 | LVT LINT1 Register |
| LAPIC_LVT_ERROR | 0x370 | LVT Error Register |
| LAPIC_TIMER_ICR | 0x380 | Timer Initial Count Register |
| LAPIC_TIMER_CCR | 0x390 | Timer Current Count Register |
| LAPIC_TIMER_DCR | 0x3E0 | Timer Divide Configuration Register |

### 4.5 I/O APIC Emulation

| Register | Offset | Description |
|----------|--------|-------------|
| IOREGSEL | 0x00 | I/O Register Select (index register) |
| IOWIN | 0x10 | I/O Window (data register) |
| IOAPIC_ID | 0x00 (index) | I/O APIC ID |
| IOAPIC_VER | 0x01 (index) | Version register |
| IOAPIC_ARB | 0x02 (index) | Arbitration ID |
| IOREDTBLn | 0x10-0x3F (index) | Redirection Table Entries (24 entries, 2 registers each) |

**IOREDTBL entry format (64-bit):**
```
Bits 0-7:   Vector
Bits 8-10:  Delivery Mode (000=Fixed, 001=Lowest Priority, 010=SMI, 100=NMI, 101=INIT, 111=ExtINT)
Bit 11:     Destination Mode (0=Physical, 1=Logical)
Bit 12:     Delivery Status (0=Idle, 1=Send Pending)
Bit 13:     Interrupt Input Pin Polarity (0=Active High, 1=Active Low)
Bit 14:     Remote IRR
Bit 15:     Trigger Mode (0=Edge, 1=Level)
Bit 16:     Interrupt Mask (1=masked)
Bits 17-63: Reserved (bits 17-55) + Destination (bits 56-63)
```

---

## 5. Boot Process

### 5.1 Real Mode Startup (16-bit)

The x86 CPU starts in real mode after reset:
- CS = 0xF000, IP = 0xFFF0 (reset vector at physical address 0xFFFFFFF0)
- Real mode addressing: PhysAddr = (CS << 4) + IP (20-bit address space)
- All segment bases = segment selector << 4
- No paging, no protection
- Interrupt Vector Table (IVT) at physical address 0x0000-0x03FF

**Emulation steps:**
1. Initialize CPU state to real mode (CR0.PE=0)
2. Set CS=0xF000, RIP=0xFFF0
3. Map physical memory at 0xFFFFFFF0 (alias of BIOS region)
4. Begin fetch-decode-execute at reset vector

### 5.2 BIOS / Firmware Boot

| Firmware Type | Description | Emulation Required |
|---------------|-------------|-------------------|
| **Legacy BIOS** | Int 0x13 (disk), Int 0x10 (video), Int 0x16 (keyboard), Int 0x15 (memory map via E820) | Full BIOS emulation or SeaBIOS integration |
| **UEFI/EFI** | EFI firmware with GPT partition table, EFI system partition | OVMF (TianoCore) integration or custom UEFI |
| **CSM** | Compatibility Support Module — UEFI that also supports legacy BIOS boot | OVMF with CSM enabled |

**Recommended approach:** Integrate existing open-source firmware:
- **SeaBIOS** for legacy BIOS boot (BSD-licensed, ~60KB binary)
- **OVMF** (TianoCore) for UEFI boot (BSD-licensed)
- Load firmware binary into emulated memory at the reset vector address

### 5.3 FreeBSD Boot Sequence (amd64)

1. **boot1** (BTX loader) — loaded from disk by BIOS/UEFI
2. **loader** (Lua interpreter) — loads kernel, modules, reads /boot/loader.conf
3. **kernel** — ELF binary loaded at physical address 0x200000 (2MB)
4. **Kernel initialization:**
   - Entry point: `start` (locore.S) — set up page tables, enter long mode
   - `mi_startup()` — main initialization
   - `kmain()` — kernel main

### 5.4 Required Emulated Devices for Boot

| Device | Type | Address | Purpose |
|--------|------|---------|---------|
| **UART (NS16550)** | Serial | 0x3F8 (COM1) | Console output |
| **HPET** | Timer | 0xFED00000 | High-precision timer |
| **i8254 PIT** | Timer | 0x40-0x43 | Legacy timer |
| **i8259 PIC** | Interrupt | 0x20-0x21, 0xA0-0xA1 | Legacy interrupt controller |
| **I/O APIC** | Interrupt | 0xFEC00000 | Modern interrupt controller |
| **LAPIC** | Interrupt | 0xFEE00000 | Per-CPU interrupt controller |
| **AHCI/SATA** | Storage | PCI 0:0:0 function 2 | Disk access |
| **virtio-blk** | Storage | PCI or MMIO | Alternative disk access |
| **RTC (MC146818)** | Timer | 0x70-0x71 | Real-time clock |
| **ACPI** | System | FADT, DSDT, MADT tables | Power management, interrupt routing |

---

## 6. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| AMD64.1 | Implement x86-64 instruction decoder (primary opcode table) | NOT STARTED | | | `sys/emulation/amd64/emu_cpu_amd64.c` | Decode 0x00-0xFF opcodes. Handle REX prefix, ModRM, SIB, displacement, immediate. |
| AMD64.2 | Implement x86-64 instruction decoder (0x0F two-byte table) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | Decode 0x0F-prefixed opcodes (MOVZX, MOVSX, etc.) |
| AMD64.3 | Implement x86-64 instruction decoder (0x0F 0x38/0x3A three-byte tables) | NOT STARTED | | AMD64.2 | `sys/emulation/amd64/emu_cpu_amd64.c` | SSE4.x, AES, CLMUL opcodes |
| AMD64.4 | Implement data movement instructions (P0) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | MOV, MOVZX, MOVSX, PUSH, POP, XCHG, CMOVcc |
| AMD64.5 | Implement arithmetic instructions (P0) | NOT STARTED | | AMD64.4 | `sys/emulation/amd64/emu_cpu_amd64.c` | ADD, SUB, ADC, SBB, INC, DEC, NEG, CMP, MUL, IMUL, DIV, IDIV |
| AMD64.6 | Implement logical and shift instructions (P0) | NOT STARTED | | AMD64.4 | `sys/emulation/amd64/emu_cpu_amd64.c` | AND, OR, XOR, NOT, TEST, SHL, SHR, SAR, ROL, ROR |
| AMD64.7 | Implement control transfer instructions (P0) | NOT STARTED | | AMD64.5 | `sys/emulation/amd64/emu_cpu_amd64.c` | Jcc, JMP, CALL, RET, IRETQ |
| AMD64.8 | Implement string operations with REP prefix (P0) | NOT STARTED | | AMD64.7 | `sys/emulation/amd64/emu_cpu_amd64.c` | MOVS, CMPS, SCAS, LODS, STOS |
| AMD64.9 | Implement system register instructions (P0) | NOT STARTED | | AMD64.7 | `sys/emulation/amd64/emu_cpu_amd64.c` | MOV CRn, MOV DRn, LMSW, SMSW |
| AMD64.10 | Implement system instructions (P0) | NOT STARTED | | AMD64.9 | `sys/emulation/amd64/emu_cpu_amd64.c` | SYSCALL, SYSRET, INT n, INT3, HLT, UD2, NOP |
| AMD64.11 | Implement protected mode instructions (P0) | NOT STARTED | | AMD64.9 | `sys/emulation/amd64/emu_cpu_amd64.c` | LGDT, SGDT, LIDT, SIDT, LLDT, SLDT, LTR, STR, LAR, LSL, VERR, VERW |
| AMD64.12 | Implement segment register instructions (P1) | NOT STARTED | | AMD64.11 | `sys/emulation/amd64/emu_cpu_amd64.c` | MOV seg, PUSH/POP seg, LFS, LGS, LSS |
| AMD64.13 | Implement 4-level page table walk (P0) | NOT STARTED | | | `sys/emulation/amd64/emu_mmu_amd64.c` | PML4→PDPT→PD→PT walk. 4KB, 2MB, 1GB pages. NX, SMEP, SMAP. |
| AMD64.14 | Implement legacy 32-bit paging (P1) | NOT STARTED | | AMD64.13 | `sys/emulation/amd64/emu_mmu_amd64.c` | 2-level (no PAE) and 3-level (PAE) paging |
| AMD64.15 | Implement TLB simulation (P0) | NOT STARTED | | AMD64.13 | `sys/emulation/amd64/emu_mmu_amd64.c` | Software TLB with PCID, INVLPG, INVPCID, global pages |
| AMD64.16 | Implement IDT and exception dispatch (P0) | NOT STARTED | | AMD64.10 | `sys/emulation/amd64/emu_intr_amd64.c` | IDT entry parsing, exception vector dispatch, error code push, stack switch |
| AMD64.17 | Implement page fault handling (P0) | NOT STARTED | | AMD64.16 | `sys/emulation/amd64/emu_intr_amd64.c` | #PF with error code, CR2 update, address translation fault |
| AMD64.18 | Implement general protection fault handling (P0) | NOT STARTED | | AMD64.16 | `sys/emulation/amd64/emu_intr_amd64.c` | #GP with error code, privilege checks, segment validation |
| AMD64.19 | Implement LAPIC emulation (P0) | NOT STARTED | | AMD64.16 | `sys/emulation/amd64/emu_intr_amd64.c` | MMIO at 0xFEE00000. TPR, EOI, ICR, LVT entries, timer. |
| AMD64.20 | Implement I/O APIC emulation (P0) | NOT STARTED | | AMD64.19 | `sys/emulation/amd64/emu_intr_amd64.c` | MMIO at 0xFEC00000. IOREDTBL entries, interrupt routing. |
| AMD64.21 | Implement i8259 PIC emulation (P1) | NOT STARTED | | AMD64.16 | `sys/emulation/amd64/emu_intr_amd64.c` | Legacy PIC at 0x20-0x21, 0xA0-0xA1. Cascade mode. |
| AMD64.22 | Implement TSS and privilege level switching (P0) | NOT STARTED | | AMD64.16 | `sys/emulation/amd64/emu_intr_amd64.c` | TSS descriptor, RSP0-2, IST1-7, stack switch on CPL change |
| AMD64.23 | Implement real mode initialization (P0) | NOT STARTED | | AMD64.1 | `usr.sbin/emu/emu_arch_amd64.c` | Real mode CPU state, 20-bit address space, IVT at 0x0000 |
| AMD64.24 | Implement protected mode transition (P0) | NOT STARTED | | AMD64.23 | `usr.sbin/emu/emu_arch_amd64.c` | CR0.PE=1, GDT setup, far jump to flush prefetch |
| AMD64.25 | Implement long mode transition (P0) | NOT STARTED | | AMD64.24 | `usr.sbin/emu/emu_arch_amd64.c` | EFER.LME=1, CR4.PAE=1, CR3=PM4L, CR0.PG=1, EFER.LMA active |
| AMD64.26 | Implement SeaBIOS/OVMF firmware loading (P0) | NOT STARTED | | AMD64.23 | `usr.sbin/emu/emu_arch_amd64.c` | Load firmware binary from blob cache via `emu_blob_resolve()`. Support legacy BIOS (seabios) and UEFI (ovmf-x64). See `010-Emulation-Blob-Management.md`. |
| AMD64.27 | Implement x87 FPU emulation (P1) | NOT STARTED | | AMD64.4 | `sys/emulation/amd64/emu_cpu_amd64.c` | ST0-ST7, FPU control/status, FINIT, FLD, FST, FADD, FSUB, FMUL, FDIV, FCOM, FSQRT |
| AMD64.28 | Implement SSE/SSE2 emulation (P1) | NOT STARTED | | AMD64.27 | `sys/emulation/amd64/emu_cpu_amd64.c` | XMM registers, scalar and packed operations, MOVAPS, ADDPS, CVTSI2SS, etc. |
| AMD64.29 | Implement bit manipulation instructions (P1) | NOT STARTED | | AMD64.6 | `sys/emulation/amd64/emu_cpu_amd64.c` | BT, BTS, BTR, BTC, BSF, BSR, POPCNT, LZCNT |
| AMD64.30 | Implement BMI1/BMI2 instructions (P2) | NOT STARTED | | AMD64.29 | `sys/emulation/amd64/emu_cpu_amd64.c` | ANDN, BEXTR, BLSI, MULX, RORX, SARX, SHLX, SHRX, PDEP, PEXT |
| AMD64.31 | Implement AVX/AVX2 emulation (P2) | NOT STARTED | | AMD64.28 | `sys/emulation/amd64/emu_cpu_amd64.c` | VEX-prefixed SIMD, YMM registers, VBROADCAST, VFMADD |
| AMD64.32 | Implement AVX-512 emulation (P2) | NOT STARTED | | AMD64.31 | `sys/emulation/amd64/emu_cpu_amd64.c` | EVEX-prefixed, ZMM registers, mask registers, scatter/gather |
| AMD64.33 | Implement CPUID emulation (P0) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | CPUID leaves 0x00-0x1F, 0x80000000-0x8000001F. Feature masking based on CPU level. |
| AMD64.34 | Implement MSR read/write (P0) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | RDMSR, WRMSR. Emulate EFER, STAR, LSTAR, CSTAR, SF_MASK, FSBASE, GSBASE, APIC_BASE, TSC, MTRR, PAT, SYSENTER_*. |
| AMD64.35 | Implement I/O port emulation (P0) | NOT STARTED | | AMD64.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | IN, OUT, INS, OUTS. I/O permission bitmap in TSS. Dispatch to device models. |
| AMD64.36 | Write amd64 CPU emulation unit tests | NOT STARTED | | AMD64.1-AMD64.35 | `tests/sys/emulation/amd64/` | Test each instruction category. Test page table walk. Test interrupt dispatch. Test mode transitions. |
| AMD64.37 | Write amd64 boot integration test | NOT STARTED | | AMD64.26 | `tests/usr.sbin/emu/amd64_boot_test.sh` | Boot FreeBSD amd64 kernel in emulator. Verify console output. |
| AMD64.38 | Write amd64 kernel module load test | NOT STARTED | | AMD64.37 | `tests/usr.sbin/emu/amd64_module_test.sh` | Load, verify, unload kernel module in emulated amd64 environment. |

---

## 7. Cross-References

### 7.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Sections 4.3 (Kernel Module Architecture), Phase 3 (Architecture-Specific CPU Emulation), Phase 5 (Custom Emulator Engine). |
| `002-Emulation-Security-FS.md` | Security architecture. Section 3.5 (Kernel Module Security), Section 6.5 (Capsicum Sandboxing). |
| `004-Emulation-Arch-i386.md` | i386 (32-bit x86) architecture. Shares instruction decoder, MMU, and interrupt infrastructure. |
| `010-Emulation-Blob-Management.md` | Blob management and CPU model database. Firmware loading (SeaBIOS, OVMF). |

### 7.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| AMD64 Architecture Programmer's Manual Vol 1-3 | https://www.amd.com/en/developer/architecture.html | Primary ISA reference |
| Intel 64 and IA-32 SDM Vol 1-3 | https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html | Supplementary ISA reference |
| FreeBSD amd64 kernel entry (locore.S) | `sys/amd64/amd64/locore.S` | Boot sequence, initial page tables |
| FreeBSD amd64 pmap | `sys/amd64/amd64/pmap.c` | Page table management reference |
| FreeBSD amd64 trap handling | `sys/amd64/amd64/trap.c` | Exception dispatch reference |
| FreeBSD bhyve VMX implementation | `sys/amd64/vmm/intel/vmx.c` | Reference for CPU state management |
| FreeBSD bhyve SVM implementation | `sys/amd64/vmm/amd/svm.c` | Reference for CPU state management |
| SeaBIOS | https://www.seabios.org/ | Legacy BIOS firmware (BSD-licensed) |
| OVMF (TianoCore) | https://github.com/tianocore/tianocore.github.io/wiki/OVMF | UEFI firmware (BSD-licensed) |
| QEMU x86 target | https://github.com/qemu/qemu/tree/master/target/i386 | Reference instruction decoder implementation |

### 7.3 Shared Infrastructure

The amd64 emulation shares the following infrastructure with other architectures:

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
| i8259 PIC | i386 | `usr.sbin/emu/emu_dev_pic.c` |
| NS16550 UART | All architectures | `usr.sbin/emu/emu_dev_uart.c` |
| HPET timer | All architectures | `usr.sbin/emu/emu_dev_timer.c` |
| virtio-blk | All architectures | `usr.sbin/emu/emu_dev_storage.c` |

---

## 8. CPU Level Selection in Instance Configuration

The `cpu_level` field must be added to the instance configuration to specify which CPU features to emulate:

```c
/* In sys/emulation/emu_internal.h */
struct emu_config {
    char name[64];           /* Instance name (unique) */
    char arch[32];           /* Target architecture */
    char cpu_level[32];      /* CPU level/feature tier (e.g., "x86-64-v3", "i686") */
    int mode;                /* EMU_MODE_BHYVE or EMU_MODE_EMULATOR */
    int memory_mb;           /* Configured memory size in MB */
    int ncpus;               /* Number of CPUs */
    char image_path[PATH_MAX]; /* VM image path */
    char kernel_path[PATH_MAX]; /* Kernel/module path */
    char memory_policy[16];  /* "prealloc", "demand", "balloon" */
    int balloon_min_pct;     /* Minimum balloon size as % of configured */
    bool memory_overcommit;  /* Allow overcommit for this instance */
};
```

**CPU level validation:** When creating an instance, validate that the requested `cpu_level` is valid for the target architecture. For amd64, valid values are: `x86-64`, `x86-64-v2`, `x86-64-v3`, `x86-64-v4`, `i386`, `i486`, `i586`, `i686`.

**Default:** If not specified, use the architecture's baseline level (`x86-64` for amd64).

**CPU level impact on CPUID:** The CPU level controls which CPUID feature bits are exposed. For example, `x86-64-v3` exposes AVX, AVX2, BMI1, BMI2, FMA, MOVBE, while `x86-64` does not.

---

## 9. Notes

- The amd64 instruction decoder is the most complex of all architectures due to variable-length encoding (1-15 bytes), multiple prefix types (REX, VEX, EVEX), and a large instruction set.
- Start with interpretive emulation (no JIT). Add JIT compilation as a future enhancement.
- The i386 (32-bit) architecture shares significant infrastructure with amd64. See `004-Emulation-Arch-i386.md` for i386-specific details.
- For the bhyve path, CPU level is less relevant since the real hardware determines available features. The CPU level field primarily affects the custom emulator path.
- SeaBIOS and OVMF are well-tested, BSD-licensed firmware options. They are **not** included in the FreeBSD source tree or release; they are downloaded at runtime via `emu blob fetch`. See `010-Emulation-Blob-Management.md` for the complete blob management system.
