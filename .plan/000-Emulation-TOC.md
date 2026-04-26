# Kernel Emulation Framework for FreeBSD — Master Table of Contents

> **Purpose:** This file is the master index for all plan documents in the `.plan/` directory. Each document is a chapter in the overall implementation plan. Use this TOC to navigate between documents and understand dependencies.

> **Note for agents:** When starting work on any task, first consult this TOC to identify all relevant documents. Cross-reference between documents before making changes.

---

## Document Map

| # | File | Title | Lines | Status | Description |
|---|------|-------|-------|--------|-------------|
| 000 | `000-Emulation-TOC.md` | **Master Table of Contents** | — | ✅ ACTIVE | This file — master index for all plan documents |
| 001 | `001-Emulation-Overview.md` | **Implementation Plan Overview** | 863 | ✅ ACTIVE | High-level architecture, design principles, implementation phases (1-9), data structures, sysctl interface, risks, TODO tracker |
| 002a | `002-Emulation-Security-p1-ThreatModel-Isolation.md` | **Security: Threat Model & Isolation** | — | 🔄 IN PROGRESS | Executive summary, threat model, trust model, isolation architecture, bhyve/VMM path security |
| 002b | `002-Emulation-Security-p2-AccessControl.md` | **Security: Access Control & Authorization** | — | 🔄 IN PROGRESS | Group configuration, ownership model, granular permissions, permission matrix, privilege definitions, jail integration, resource limits |
| 002c | `002-Emulation-Security-p3-CustomEmulator.md` | **Security: Custom Emulator Deep-Dive** | — | 🔄 IN PROGRESS | Attack surface, instruction decoder safety, memory safety, ELF loader, Capsicum sandboxing, timing side-channels, VM introspection, resource accounting, rctl, DTrace, Veriexec, zombie handling, swap storms, devfs, procfs, NUMA, huge pages, swap encryption, KASLR, SMM, virtio security, 9p security, NVMe, TPM, Secure Boot, performance counters, NMI, ACPI, MSI/MSI-X, DMA remapping, SR-IOV, KSM, FUSE, sysctl hardening, PMU, watchdog, env leakage |
| 002d | `002-Emulation-Security-p4-Filesystem-Devices-Crash.md` | **Security: Filesystem, Devices & Crash Safety** | — | 🔄 IN PROGRESS | Filesystem strategy, base images, source sharing, ZFS integration, device model principles, device attack surface, network security, crash containment, host safety |
| 002e | `002-Emulation-Security-p5-AdditionalAnalysis.md` | **Security: Additional Analysis** | — | 🔄 IN PROGRESS | Audit logging, MAC framework, securelevel, memory scrubbing, core dump security, ptrace, TOCTOU, signal handling, OOM killer, entropy/RNG, supply chain, firmware security |
| 002f | `002-Emulation-Security-p6-Implementation.md` | **Security: Implementation Phases & Checklists** | — | 🔄 IN PROGRESS | Implementation phases S0-S18, security recommendations, data structures, sysctl interface, risks & mitigations, future enhancements, task completion checklist, conclusion |
| 003 | `003-Emulation-Arch-amd64.md` | **Architecture: amd64** | 573 | ✅ ACTIVE | x86-64 instruction set, CPU levels, register state, MMU, interrupt model, boot process, implementation tasks |
| 004 | `004-Emulation-Arch-i386.md` | **Architecture: i386** | 351 | ✅ ACTIVE | IA-32 instruction set, CPU levels, differences from amd64, implementation tasks |
| 005 | `005-Emulation-Arch-arm64.md` | **Architecture: arm64** | 485 | ✅ ACTIVE | AArch64 instruction set, CPU levels, register state, MMU, GIC, implementation tasks |
| 006 | `006-Emulation-Arch-arm.md` | **Architecture: arm (32-bit)** | 260 | ✅ ACTIVE | ARMv7-A instruction set, CPU levels, ARM/Thumb, implementation tasks |
| 007 | `007-Emulation-Arch-powerpc.md` | **Architecture: PowerPC** | 237 | ✅ ACTIVE | PowerISA instruction set, CPU levels, hash/radix MMU, implementation tasks |
| 008 | `008-Emulation-Arch-riscv.md` | **Architecture: RISC-V** | 393 | ✅ ACTIVE | RISC-V instruction set, CPU levels, Sv39/Sv48, CLINT/PLIC, implementation tasks |
| 009 | `009-Emulation-Devices.md` | **Device Emulation** | 1387 | ✅ ACTIVE | 27+ device models, sound cards, USB, NIC stubs, display servers, ring buffer, device driver hooks, implementation tasks |
| 010 | `010-Emulation-Blob-Management.md` | **Blob Management & CPU Models** | 1361 | ✅ ACTIVE | Firmware blob acquisition, verification, caching, 30+ CPU models, implementation tasks |

---

## Document Dependencies

```
000-Emulation-TOC.md (master index)
  │
  ├── 001-Emulation-Overview.md (main plan — read first)
  │     ├── References all 002* security documents
  │     ├── References 003-008 architecture documents
  │     ├── References 009-Emulation-Devices.md
  │     └── References 010-Emulation-Blob-Management.md
  │
  ├── 002a-Emulation-Security-p1-ThreatModel-Isolation.md
  │     └── Referenced by 001 (Section 9 Risks)
  │
  ├── 002b-Emulation-Security-p2-AccessControl.md
  │     └── Referenced by 001 (Section 9 Risks)
  │
  ├── 002c-Emulation-Security-p3-CustomEmulator.md
  │     └── Referenced by 001 (Section 9 Risks)
  │
  ├── 002d-Emulation-Security-p4-Filesystem-Devices-Crash.md
  │     └── Referenced by 001 (Section 9 Risks)
  │
  ├── 002e-Emulation-Security-p5-AdditionalAnalysis.md
  │     └── Referenced by 001 (Section 9 Risks)
  │
  ├── 002f-Emulation-Security-p6-Implementation.md
  │     └── Referenced by 001 (Section 5, 10, 12)
  │
  ├── 003-008 Architecture files
  │     └── Referenced by 001 (Section 3)
  │
  ├── 009-Emulation-Devices.md
  │     └── Referenced by 001 (Phase 5)
  │
  └── 010-Emulation-Blob-Management.md
        └── Referenced by 001 (Section 1, Phase 6)
```

---

## Reading Order

For a new reader, the recommended reading order is:

1. **`001-Emulation-Overview.md`** — Start here for the big picture
2. **`002a` through `002f`** — Security architecture (read in order)
3. **`010-Emulation-Blob-Management.md`** — Blob management (referenced by architecture files)
4. **`009-Emulation-Devices.md`** — Device models (referenced by Phase 5)
5. **`003` through `008`** — Architecture-specific details (as needed)

---

## Cross-Reference Index

| Topic | Primary Document | Secondary Documents |
|-------|-----------------|---------------------|
| Architecture overview | `001` | All |
| Threat model | `002a` | `002c`, `002e` |
| Access control | `002b` | `002f` (S2) |
| Custom emulator security | `002c` | `002a` (Section 3) |
| Filesystem security | `002d` | `002f` (S3) |
| Device security | `002d` | `009` |
| Crash safety | `002d` | `002f` (S1) |
| Audit logging | `002e` | `002f` (S7) |
| MAC framework | `002e` | `002f` (S8) |
| Memory management | `002f` (S6) | `001` (2.16, 5.18) |
| Kernel module security | `002f` (S0) | `001` (4.3) |
| Implementation phases | `002f` | `001` (5, 10) |
| Task checklists | `002f` | `001` (12) |
| amd64 architecture | `003` | `002c` (instruction decoder) |
| i386 architecture | `004` | `002c` |
| arm64 architecture | `005` | `002c` |
| arm architecture | `006` | `002c` |
| PowerPC architecture | `007` | `002c` |
| RISC-V architecture | `008` | `002c` |
| Device models | `009` | `002d` (device security) |
| Blob management | `010` | `001` (6.18) |
| CPU models | `010` | `003-008` |

---

## Status Legend

| Status | Meaning |
|--------|---------|
| ✅ ACTIVE | Document is complete and current |
| 🔄 IN PROGRESS | Document is being written or updated |
| 📝 PLANNED | Document is planned but not yet created |
| ❌ DEPRECATED | Document is outdated and should not be used |

---

## File Naming Convention

Files follow the pattern: `###-MainTopic-SubTopic[-subsubtopic].md`

- `###` — Two or three-digit sequence number for ordering
- `MainTopic` — Primary topic (e.g., `Emulation`, `Arch`)
- `SubTopic` — Secondary topic (e.g., `Security`, `Devices`, `Blob`)
- `-subsubtopic` — Optional further refinement (e.g., `p1-ThreatModel-Isolation`)

Security documents use `002` prefix with letter suffixes (`a` through `f`) and part numbers (`p1` through `p6`) to indicate reading order within the security chapter series.
