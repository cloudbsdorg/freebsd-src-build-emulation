# Kernel Emulation Framework for FreeBSD — Implementation Plan

## 1. Executive Summary

This document outlines a comprehensive, incremental approach to adding a kernel emulation framework to FreeBSD for testing kernel modules, examining system behavior, and providing safe isolation for experimental kernel code. The framework supports two modes of operation:

- **Native + VMM/bhyve mode**: When the host architecture matches the target and VMM/bhyve is available, use FreeBSD's native hypervisor for near-native performance. This reuses and extends existing `vmm.ko` / bhyve infrastructure.
- **Pure emulation mode**: A custom lightweight emulator built from scratch, inspired by the architecture of QEMU, bhyve/VMM, and other open-source emulators. No external dependencies.

**Primary Recommendation:** A unified kernel emulation framework with per-architecture kernel options (`KERNEL_EMULATION_{ARCH}`), a kernel-side emulation subsystem in `sys/emulation/`, and userland tooling in `usr.sbin/emu/` to orchestrate multiple emulated instances across different architectures simultaneously.

**Security, Access Control & Filesystem Strategy:** See `002-Emulation-Security-FS.md` for the detailed security architecture, access control model, threat model, filesystem sharing strategy, and device emulation security covering both the bhyve/VMM and custom emulator paths.

---

## 2. Motivation & Problem Statement

### 2.1 Why Emulation?

Testing kernel modules on bare metal or production systems carries significant risk:

- **System instability**: A buggy kernel module can panic the system, corrupt filesystems, or cause data loss.
- **Recovery time**: Rebooting after a kernel panic, especially on remote or embedded systems, is time-consuming.
- **Limited architectures**: Developers may not have physical hardware for all architectures FreeBSD supports (arm, arm64, powerpc, riscv, etc.).
- **Reproducibility**: Manual testing environments are inconsistent between developers.

### 2.2 Why Not Just bhyve?

bhyve is excellent for amd64-on-amd64 virtualization but has limitations:

- **Architecture lock-in**: bhyve only runs on amd64 hosts and can only virtualize amd64 guests.
- **No cross-architecture support**: You cannot test an arm64 kernel module on an amd64 host with bhyve.
- **Hardware requirements**: bhyve requires VMM-capable hardware (Intel VT-x / AMD-V).

A custom emulator fills these gaps, albeit at a performance cost, and avoids external dependencies.

### 2.3 Why Build Our Own Emulator?

- **No external dependencies**: QEMU is not part of FreeBSD base; requiring it adds friction.
- **Tight integration**: A FreeBSD-native emulator can leverage existing kernel infrastructure (VMM, bhyve, DDB, KDB, etc.).
- **Focused scope**: We only need to emulate FreeBSD-supported architectures, not every device under the sun.
- **AI-agent friendly**: Structured output, stack examination, and module state inspection are first-class features, not afterthoughts.

### 2.4 Target Use Cases

| Use Case | Recommended Mode | Why |
|----------|-----------------|-----|
| Test amd64 kernel module on amd64 host | bhyve (native) | Fastest, uses hardware virtualization |
| Test arm64 kernel module on amd64 host | Custom emulator (pure emulation) | Only option for cross-arch |
| Test riscv kernel module on amd64 host | Custom emulator (pure emulation) | Only option for cross-arch |
| CI/CD pipeline for kernel modules | bhyve or custom emulator | Isolated, reproducible |
| Kernel crash analysis | Either | Capture dumps, examine stacks |
| Educational/demonstration | Either | Safe environment for experimentation |

---

## 3. Supported Architectures

FreeBSD currently supports (or has historically supported) the following architectures. The emulation framework should target all of them:

| Architecture | bhyve Support | Custom Emulator | Priority |
|-------------|---------------|-----------------|----------|
| amd64 | ✅ Native | ✅ Planned | P0 |
| i386 | ❌ | ✅ Planned | P1 |
| arm64 (AArch64) | ❌ | ✅ Planned | P0 |
| arm (32-bit) | ❌ | ✅ Planned | P1 |
| powerpc64 | ❌ | ✅ Planned | P2 |
| powerpc64le | ❌ | ✅ Planned | P2 |
| powerpc (32-bit) | ❌ | ✅ Planned | P2 |
| riscv64 | ❌ | ✅ Planned | P1 |

---

## 4. Proposed Architecture

### 4.1 High-Level Design

```
+-------------------------------------------------------------+
|                    User / CI / Developer                       |
|                      (emu CLI tool)                           |
+----------------------------+--------------------------------+
                             |
                             v
+-------------------------------------------------------------+
|              Emulation Orchestration Layer (usr.sbin/emu)      |
|  - Detects host capabilities (VMM available?)                |
|  - Selects optimal mode (bhyve vs custom emulator)           |
|  - Manages multiple instances (different archs simultaneously)|
|  - Collects output (console, logs, crash dumps, test results) |
|  - Examines kernel stacks and module state                   |
+--+-----------+-----------+-----------+-----------+----------+
   |           |           |           |           |
   v           v           v           v           v
+------+   +------+   +------+   +------+   +------+
|emu   |   |emu   |   |emu   |   |emu   |   |emu   |
|amd64 |   |arm64 |   |riscv |   |i386  |   |ppc64 |
|inst#0|   |inst#0|   |inst#0|   |inst#0|   |inst#0|
+------+   +------+   +------+   +------+   +------+
+------+   +------+
|emu   |   |emu   |
|amd64 |   |arm64 |
|inst#1|   |inst#1|
+------+   +------+
```

### 4.2 Key Design Principles

1. **Host safety**: Kernel testing must never run on the development or CI host directly.
2. **Mode transparency**: The user specifies the target architecture; the framework selects the best available mode.
3. **Loadable kernel modules**: The emulation framework is implemented as loadable kernel modules (`emu.ko`, `emu_core.ko`, `emu_amd64.ko`, etc.), not compiled into the kernel via options. This allows runtime selection of which architectures to support.
4. **One module to load them all**: `kldload emu` loads the master module which automatically loads all sub-modules via `MODULE_DEPEND`.
5. **Per-architecture granularity**: Individual architecture modules (`emu_amd64.ko`, `emu_aarch64.ko`, etc.) can be loaded independently for minimal footprint.
6. **Off by default**: No emulation modules are loaded at boot. Explicit `kldload` required.
7. **No external dependencies**: The emulator is built from scratch as part of the FreeBSD source tree.
8. **Multi-instance support**: Multiple emulated instances of different architectures can run simultaneously, each with its own name, PID, and state.
9. **Structured output**: All emulation output must be collected in formats parseable by both humans and AI agents (JSON, TAP, JUnit XML).
10. **Stack examination**: The framework must provide tools to capture and analyze kernel stacks from emulated environments.
11. **Module lifecycle testing**: Ability to load, unload, and verify kernel module behavior without crashing the host.

### 4.3 Kernel Module Architecture

The emulation framework is structured as a hierarchy of loadable kernel modules:

```
sys/modules/emu/Makefile          → emu.ko   (master module, loads all sub-modules)
sys/modules/emu_core/Makefile     → emu_core.ko (core framework)
sys/modules/emu_amd64/Makefile    → emu_amd64.ko (amd64 CPU emulation)
sys/modules/emu_aarch64/Makefile  → emu_aarch64.ko (arm64 CPU emulation)
sys/modules/emu_arm/Makefile      → emu_arm.ko (arm 32-bit CPU emulation)
sys/modules/emu_i386/Makefile     → emu_i386.ko (i386 CPU emulation)
sys/modules/emu_powerpc/Makefile  → emu_powerpc.ko (powerpc CPU emulation)
sys/modules/emu_riscv/Makefile    → emu_riscv.ko (riscv CPU emulation)
```

**Module dependency chain:**

```
emu.ko (master)
  ├── MODULE_DEPEND(emu_core)     → emu_core.ko
  ├── MODULE_DEPEND(emu_amd64)    → emu_amd64.ko
  ├── MODULE_DEPEND(emu_aarch64)  → emu_aarch64.ko
  ├── MODULE_DEPEND(emu_arm)      → emu_arm.ko
  ├── MODULE_DEPEND(emu_i386)     → emu_i386.ko
  ├── MODULE_DEPEND(emu_powerpc)  → emu_powerpc.ko
  └── MODULE_DEPEND(emu_riscv)    → emu_riscv.ko

Each arch module:
  └── MODULE_DEPEND(emu_core)     → emu_core.ko (loaded automatically)
```

**Loading behavior:**
- `kldload emu` → loads master module, which triggers automatic loading of `emu_core.ko` and all arch modules
- `kldload emu_amd64` → loads amd64 module, which triggers automatic loading of `emu_core.ko`
- `kldload emu_core` → loads only the core framework (no CPU emulation, useful for management only)
- `kldunload emu` → unloads master module and all sub-modules (if no other dependents)

**Source file layout:**
- Architecture-independent source: `sys/emulation/emu_*.c` (compiled into `emu_core.ko`)
- Architecture-specific source: `sys/emulation/<arch>/emu_*.c` (compiled into respective `emu_<arch>.ko`)
- Module Makefiles: `sys/modules/emu*/Makefile`

### 4.4 Reference Architecture: bhyve/VMM

The existing bhyve/VMM codebase provides key architectural patterns:

- **Kernel module (`vmm.ko`)**: Manages VMCS/VMCB (Intel/AMD), EPT/NPT, VM exits, interrupt injection
- **Userland process (`bhyve`)**: Device emulation (ACPI, IOAPIC, LAPIC, HPET, storage, network), memory mapping via `/dev/vmm`
- **`kernemu_dev`**: Kernel-emulated device pass-through for LAPIC/IOAPIC/HPET MMIO regions
- **`vmmapi`**: Library interface (`/usr/src/lib/libvmmapi`) for userland-VMM communication

Our custom emulator follows a similar split: kernel modules for CPU emulation and a userland component for device emulation and orchestration.

### 4.5 Multi-Instance Architecture

Each emulated instance is identified by a unique name and tracked independently:

```
Instance Registry (kernel: sys/emulation/emu_instance.c)
├── Instance "test-amd64-1" → { arch: amd64, mode: bhyve, pid: 1234, status: running }
├── Instance "test-arm64-1" → { arch: arm64, mode: emulator, pid: 5678, status: running }
├── Instance "test-riscv-1" → { arch: riscv, mode: emulator, pid: 9012, status: stopped }
└── Instance "test-i386-1"  → { arch: i386,  mode: emulator, pid: 3456, status: crashed }
```

Each instance has:
- A unique name (user-assigned or auto-generated)
- A target architecture
- An execution mode (bhyve or emulator)
- A PID for the backing process
- A status (running, stopped, crashed, destroyed)
- Instance-specific configuration (memory, CPUs, image path, kernel path)
- A console log buffer
- A crash dump (if applicable)
- A stack trace cache (last captured)

---

## 5. Implementation Phases

### Phase 1: Kernel Module Build System Integration

**Objective:** Create the kernel module Makefiles and integrate the emulation modules into the build system.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 1.1 | Create `sys/modules/emu_core/Makefile` | NOT STARTED | | | | | `sys/modules/emu_core/Makefile` | Builds `emu_core.ko`. Source: `sys/emulation/emu_main.c`, `emu_sysctl.c`, `emu_instance.c`, `emu_stack.c`, `emu_vmm.c`, `emu_cpu.c`, `emu_mem.c`, `emu_intr.c`, `emu_device.c`, `emu_console.c`, `emu_crash.c`, `emu_module.c`, `emu_memmgmt.c`. Declares `MODULE_VERSION(emu_core, 1)`. |
| 1.2 | Create `sys/modules/emu_amd64/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_amd64/Makefile` | Builds `emu_amd64.ko`. Source: `sys/emulation/amd64/emu_cpu_amd64.c`, `emu_mmu_amd64.c`, `emu_intr_amd64.c`. Declares `MODULE_DEPEND(emu_amd64, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_amd64, 1)`. |
| 1.3 | Create `sys/modules/emu_aarch64/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_aarch64/Makefile` | Builds `emu_aarch64.ko`. Source: `sys/emulation/arm64/emu_cpu_arm64.c`, `emu_mmu_arm64.c`, `emu_intr_arm64.c`. Declares `MODULE_DEPEND(emu_aarch64, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_aarch64, 1)`. |
| 1.4 | Create `sys/modules/emu_arm/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_arm/Makefile` | Builds `emu_arm.ko`. Source: `sys/emulation/arm/emu_cpu_arm.c`. Declares `MODULE_DEPEND(emu_arm, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_arm, 1)`. |
| 1.5 | Create `sys/modules/emu_i386/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_i386/Makefile` | Builds `emu_i386.ko`. Source: `sys/emulation/i386/emu_cpu_i386.c`. Declares `MODULE_DEPEND(emu_i386, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_i386, 1)`. |
| 1.6 | Create `sys/modules/emu_powerpc/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_powerpc/Makefile` | Builds `emu_powerpc.ko`. Source: `sys/emulation/powerpc/emu_cpu_ppc.c`. Declares `MODULE_DEPEND(emu_powerpc, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_powerpc, 1)`. |
| 1.7 | Create `sys/modules/emu_riscv/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_riscv/Makefile` | Builds `emu_riscv.ko`. Source: `sys/emulation/riscv/emu_cpu_riscv.c`, `emu_mmu_riscv.c`, `emu_intr_riscv.c`. Declares `MODULE_DEPEND(emu_riscv, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_riscv, 1)`. |
| 1.8 | Create `sys/modules/emu/Makefile` (master module) | NOT STARTED | | | | 1.2–1.7 | `sys/modules/emu/Makefile` | Builds `emu.ko` — master module with no source files. Declares `MODULE_DEPEND(emu, emu_core, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_amd64, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_aarch64, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_arm, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_i386, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_powerpc, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_riscv, 1, 1, 1)`. `kldload emu` loads all emulation modules. |
| 1.9 | Add emulation modules to `sys/modules/Makefile` | NOT STARTED | | | | 1.8 | `sys/modules/Makefile` | Add `emu`, `emu_core`, `emu_amd64`, `emu_aarch64`, `emu_arm`, `emu_i386`, `emu_powerpc`, `emu_riscv` to SUBDIR. Conditional on `MACHINE_CPUARCH` where appropriate (e.g., `emu_amd64` only on amd64 host). |
| 1.10 | Add `MK_EMULATION` build option to `share/mk/bsd.opts.mk` | NOT STARTED | | | | 1.9 | `share/mk/bsd.opts.mk` | Add `__DEFAULT_NO_OPTIONS` entry for `MK_EMULATION`. Controls whether emulation modules are built as part of `make buildworld` / `make modules`. |
| 1.11 | Add `EMULATION` to `share/mk/src.opts.mk` | NOT STARTED | | | | 1.10 | `share/mk/src.opts.mk` | Register `MK_EMULATION` as a src option so it appears in `make showconfig`. |
| 1.12 | Create `sys/emulation/` directory structure | NOT STARTED | | | | 1.1 | `sys/emulation/`, `sys/emulation/amd64/`, `sys/emulation/arm64/`, `sys/emulation/arm/`, `sys/emulation/i386/`, `sys/emulation/powerpc/`, `sys/emulation/riscv/` | New directories for emulation subsystem source files. |

### Phase 2: Kernel-Side Emulation Framework (`sys/emulation/`)

**Objective:** Create the kernel subsystem that provides emulation services, instance management, and sysctl interface.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 2.1 | Create `sys/emulation/` directory | NOT STARTED | | | | 1.8 | | New directory for emulation subsystem. Create subdirectories for each arch: `amd64/`, `arm64/`, `arm/`, `i386/`, `powerpc/`, `riscv/`. |
| 2.2 | Implement `emu_main.c` — core framework | NOT STARTED | | | | 2.1 | `sys/emulation/emu_main.c` | Module declaration (`EMU_MODULE`), `emu_modevent()` for load/unload, capability detection (`emu_detect_caps()`), initialization of instance registry and sysctl tree. Uses `SYSINIT`/`SYSUNINIT`. |
| 2.3 | Implement `emu_sysctl.c` — sysctl interface | NOT STARTED | | | | 2.2 | `sys/emulation/emu_sysctl.c` | `kern.emulation.*` sysctl tree. Nodes: `enabled` (int, default 0), `mode` (string, read-only), `instances` (string, list all instances), `caps` (string, read-only capabilities). Per-instance nodes under `kern.emulation.instance.<name>.*`. |
| 2.4 | Implement `emu_instance.c` — instance registry | NOT STARTED | | | | 2.2 | `sys/emulation/emu_instance.c` | Multi-instance management. `emu_instance_create()`, `emu_instance_destroy()`, `emu_instance_find()`, `emu_instance_list()`. Each instance has: name, arch, mode, pid, status, config, console buffer, crash dump pointer, stack cache. Locked by `emu_instance_lock`. |
| 2.5 | Implement `emu_stack.c` — stack examination | NOT STARTED | | | | 2.2 | `sys/emulation/emu_stack.c` | Stack trace capture and formatting. `emu_stack_capture()`, `emu_stack_format()`. Integrates with DDB/KDB for stack unwinding. Outputs structured frame data (PC, SP, FP, symbol, module). |
| 2.6 | Implement `emu_internal.h` — internal header | NOT STARTED | | | | 2.2 | `sys/emulation/emu_internal.h` | Internal data structures: `struct emu_caps`, `struct emu_instance`, `struct emu_stack_frame`, `struct emu_config`. Internal APIs for module use only. |
| 2.7 | Implement `emu.h` — public API header | NOT STARTED | | | | 2.2 | `sys/emulation/emu.h` | Public API for kernel consumers. `emu_instance_*()` functions, `emu_stack_*()` functions, `emu_caps_*()` functions. Documented with KDoc-style comments. |
| 2.8 | Implement `emu_vmm.c` — VMM integration | NOT STARTED | | | | 2.2 | `sys/emulation/emu_vmm.c` | Interface to vmm.ko for bhyve mode. `emu_vmm_available()` checks `/dev/vmm` and CPU features. `emu_vmm_create_vm()`, `emu_vmm_destroy_vm()`, `emu_vmm_snapshot()`. Uses `vmmapi` ioctls. |
| 2.9 | Implement `emu_cpu.c` — CPU emulation core | NOT STARTED | | | | 2.2 | `sys/emulation/emu_cpu.c` | Architecture-independent CPU emulation primitives. Register file abstraction, instruction fetch interface, exception dispatch. Dispatches to arch-specific handlers via function pointers. |
| 2.10 | Implement `emu_mem.c` — memory emulation | NOT STARTED | | | | 2.9 | `sys/emulation/emu_mem.c` | Memory region management, MMIO dispatch. `emu_mem_region_register()`, `emu_mem_region_unregister()`, `emu_mem_read()`, `emu_mem_write()`. Supports overlapping regions with priority. |
| 2.11 | Implement `emu_intr.c` — interrupt controller emulation | NOT STARTED | | | | 2.9 | `sys/emulation/emu_intr.c` | Interrupt injection and delivery framework. `emu_intr_raise()`, `emu_intr_lower()`, `emu_intr_pending()`. Dispatches to arch-specific interrupt controllers. |
| 2.12 | Implement `emu_device.c` — device model framework | NOT STARTED | | | | 2.10 | `sys/emulation/emu_device.c` | Device registration, MMIO/PIO dispatch. `emu_device_register()`, `emu_device_unregister()`, `emu_device_dispatch()`. Devices register read/write handlers for address ranges. |
| 2.13 | Implement `emu_console.c` — console capture | NOT STARTED | | | | 2.2 | `sys/emulation/emu_console.c` | Capture guest console output into per-instance ring buffer. `emu_console_write()`, `emu_console_read()`, `emu_console_clear()`. Configurable buffer size (default 64KB). |
| 2.14 | Implement `emu_crash.c` — crash detection | NOT STARTED | | | | 2.2 | `sys/emulation/emu_crash.c` | Detect panics in emulated environments. `emu_crash_detect()`, `emu_crash_capture()`, `emu_crash_dump()`. Captures register state, stack trace, and panic message. |
| 2.15 | Implement `emu_module.c` — module state tracking | NOT STARTED | | | | 2.2 | `sys/emulation/emu_module.c` | Track loaded modules in emulated environment. `emu_module_loaded()`, `emu_module_unloaded()`, `emu_module_list()`. Maintains a list of (module_name, load_address, size, status) per instance. |
| 2.16 | Implement `emu_memmgmt.c` — memory management & tracking | NOT STARTED | | | | 2.3 | `sys/emulation/emu_memmgmt.c` | Memory policy sysctls (`memory_policy`, `memory_overcommit`, `memory_warn_percent`, `memory_balloon_min_pct`, `memory_balloon_interval`, `memory_system_reserve_percent`), per-instance `memory_used` tracking, host memory capacity detection (total physical minus system-wide used memory (OS + other processes) minus already-consumed by other instances minus safety margin), overcommit warning logic, per-instance balloon target interface |

### Phase 3: Architecture-Specific CPU Emulation

**Objective:** Implement CPU emulation for each target architecture. Each architecture gets its own subdirectory under `sys/emulation/`.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 3.1 | Implement amd64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/amd64/emu_cpu_amd64.c` | x86-64 instruction decoder. Register state: RAX, RBX, RCX, RDX, RSI, RDI, RBP, RSP, R8-R15, RIP, RFLAGS, segment registers, control registers, MSRs. Implements common instructions (MOV, ADD, SUB, CMP, JMP, CALL, RET, PUSH, POP, INT, SYSCALL, etc.). |
| 3.2 | Implement amd64 MMU emulation | NOT STARTED | | | | 3.1 | `sys/emulation/amd64/emu_mmu_amd64.c` | Page table walk for 4-level paging (PML4 → PDPT → PD → PT). TLB simulation. Support for 4KB, 2MB, 1GB pages. NX bit, SMEP, SMAP awareness. |
| 3.3 | Implement amd64 interrupt model | NOT STARTED | | | | 3.1 | `sys/emulation/amd64/emu_intr_amd64.c` | IDT handling, exception vectors (PF, GP, UD, DB, BP, etc.), LAPIC emulation, I/O APIC emulation. Interrupt gate, trap gate, task gate dispatch. |
| 3.4 | Implement arm64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/arm64/emu_cpu_arm64.c` | AArch64 instruction decoder. Register state: X0-X30, SP, PC, PSTATE, SPSR_ELx, ELR_ELx, system registers. Implements common A64 instructions (ADD, SUB, LDR, STR, B, BL, BR, RET, SVC, etc.). Exception levels: EL0-EL3. |
| 3.5 | Implement arm64 MMU emulation | NOT STARTED | | | | 3.4 | `sys/emulation/arm64/emu_mmu_arm64.c` | Stage 1/2 page tables. 4KB/16KB/64KB page support. TCR_ELx, TTBR0_ELx, TTBR1_ELx. Translation table walk with VMSAv8-64 format. |
| 3.6 | Implement arm64 interrupt model | NOT STARTED | | | | 3.4 | `sys/emulation/arm64/emu_intr_arm64.c` | GICv3 emulation (redistributor, distributor, CPU interface). Interrupt IDs, priority, affinity routing. SGI, PPI, SPI handling. |
| 3.7 | Implement riscv64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/riscv/emu_cpu_riscv.c` | RISC-V 64-bit instruction decoder (RV64I base + M/A/F/D extensions). Register state: X0-X31, PC. Implements common instructions (LUI, AUIPC, JAL, JALR, BEQ, BNE, BLT, BGE, BLTU, BGEU, LB, LH, LW, LD, LBU, LHU, LWU, SB, SH, SW, SD, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, FENCE, ECALL, EBREAK, CSRRW, CSRRS, CSRRC). |
| 3.8 | Implement riscv64 MMU emulation | NOT STARTED | | | | 3.7 | `sys/emulation/riscv/emu_mmu_riscv.c` | Sv39/Sv48 page tables. satp CSR handling. Page table walk with VPN[0-2]/VPN[0-3]. PTE bits: V, R, W, X, U, G, A, D. |
| 3.9 | Implement riscv64 interrupt model | NOT STARTED | | | | 3.7 | `sys/emulation/riscv/emu_intr_riscv.c` | CLINT/PLIC emulation. mtime/mtimecmp timer. Machine-level interrupts (MEI, MSI, MTI, SEI, SSI, STI). S-mode interrupt delegation via mideleg/sideleg. |
| 3.10 | Implement i386 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/i386/emu_cpu_i386.c` | x86-32 instruction decoder. Register state: EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP, EFLAGS, segment registers, control registers. Implements common 32-bit instructions. |
| 3.11 | Implement arm (32-bit) CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/arm/emu_cpu_arm.c` | ARMv7-A instruction decoder. Register state: R0-R15, CPSR, SPSR. Implements ARM and Thumb instruction sets. Exception handling: IRQ, FIQ, SVC, ABORT, UNDEFINED. |
| 3.12 | Implement powerpc CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/powerpc/emu_cpu_ppc.c` | PowerISA instruction decoder (64-bit and 32-bit). Register state: GPR0-GPR31, LR, CTR, XER, CR, MSR, SRR0, SRR1, SPRG0-SPRG3. Implements common PowerISA instructions. |

### Phase 4: bhyve/VMM Integration (Native Path)

**Objective:** When running on amd64 with VMM hardware, use bhyve for native-speed emulation. Each instance gets its own bhyve process.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 4.1 | Implement VMM capability detection | NOT STARTED | | | | 2.8 | `sys/emulation/emu_vmm.c` | Check `/dev/vmm` exists, CPU features (VMX/SVM), vmm.ko loaded. `emu_vmm_available()` returns bool. Also check `kern.vmm.available` sysctl. |
| 4.2 | Implement bhyve VM creation via ioctl | NOT STARTED | | | | 4.1 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_create(name, arch, memory_mb, ncpus)`. Uses `vm_create()`, `vm_setup_memory()`, `vm_setup_cpus()` from vmmapi. Returns fd to `/dev/vmm/<name>`. |
| 4.3 | Implement bhyve VM boot (bhyveload) | NOT STARTED | | | | 4.2 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_load(instance, kernel_path)`. Uses `bhyveload` or direct `vm_load_kernel()` to load kernel/module into VM memory. Sets up initial register state. |
| 4.4 | Implement bhyve console capture | NOT STARTED | | | | 4.3 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_console_start(instance)`. Captures serial output (COM1) via pty or pipe. Feeds into `emu_console_write()` for the instance. |
| 4.5 | Implement bhyve crash detection | NOT STARTED | | | | 4.4 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_crash_check(instance)`. Detects panics via VMM snapshot. Monitors VM exit reason for TRIPLE_FAULT or SHUTDOWN. Captures register state via `vm_get_register_set()`. |
| 4.6 | Implement bhyve stack capture | NOT STARTED | | | | 4.5 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_stack_capture(instance)`. Uses VMM snapshot to capture register state (RIP, RSP, RBP). Walks stack frames via frame pointer chain. Resolves symbols via kernel symbol table. |
| 4.7 | Implement bhyve VM stop/destroy | NOT STARTED | | | | 4.6 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_stop(instance)`, `emu_bhyve_destroy(instance)`. Sends SIGTERM to bhyve process, waits for exit, closes `/dev/vmm/<name>`, cleans up instance state. |
| 4.8 | Implement bhyve snapshot/restore | NOT STARTED | | | | 4.7 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_snapshot(instance)`, `emu_bhyve_restore(instance)`. Uses `BHYVE_SNAPSHOT` ioctl to save/restore VM state. Enables fast test iteration without rebooting. |

### Phase 5: Custom Emulator Engine (Pure Emulation Path)

**Objective:** Build a lightweight, FreeBSD-native emulator for cross-architecture testing. Each instance runs as a separate process.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 5.1 | Design emulator architecture document | NOT STARTED | | | | 2.9 | `docs/emulator-arch.md` | Document the emulator design: fetch-decode-execute loop, memory hierarchy, device model, interrupt controller chain, GDB stub protocol. |
| 5.2 | Implement emulator main loop | NOT STARTED | | | | 5.1 | `usr.sbin/emu/emu_engine.c` | `emu_engine_run(instance)`. Fetch-decode-execute loop. Calls arch-specific `cpu_fetch()`, `cpu_decode()`, `cpu_execute()`. Handles interrupts, exceptions, MMIO exits. |
| 5.3 | Implement memory bus | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | `emu_mem_bus_read(addr, size)`, `emu_mem_bus_write(addr, size, val)`. Dispatches to RAM regions or MMIO handlers. Supports read-only, write-only, execute-only regions. |
| 5.4 | Implement MMIO dispatch | NOT STARTED | | | | 5.3 | `usr.sbin/emu/emu_engine.c` | `emu_mmio_dispatch(addr, dir, size, val)`. Routes MMIO accesses to registered device handlers. Devices register via `emu_device_register_mmio(base, size, handler)`. |
| 5.5 | Implement boot ROM loader | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_boot.c` | `emu_boot_load(instance, kernel_path)`. Loads FreeBSD kernel into emulated memory at the correct load address. Parses ELF headers, sets up initial page tables, jumps to entry point. |
| 5.6 | Implement serial console device | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_uart.c` | NS16550-compatible UART. Registers MMIO region at standard COM1/COM2 addresses. Implements THR, RBR, LSR, IER, IIR, FCR, LCR, MCR, LSR, MSR registers. Output goes to instance console buffer. |
| 5.7 | Implement timer device (HPET/PIT) | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_timer.c` | HPET-compatible timer with 3 comparators. Configurable frequency (default 10MHz). Generates periodic or one-shot interrupts. Also implement i8254 PIT for legacy compatibility. |
| 5.8 | Implement interrupt controller | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_intr.c` | Interrupt routing framework. Supports wired interrupts and MSI. Maintains pending interrupt bitmap. Dispatches to arch-specific interrupt controller (LAPIC, GIC, CLINT/PLIC). |
| 5.9 | Implement virtio-style storage device | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_storage.c` | Block device for root filesystem. Implements virtio-blk transport over MMIO. Supports read/write/flush operations. Backed by a raw disk image file. |
| 5.10 | Implement amd64 emulation frontend | NOT STARTED | | | | 3.1, 5.2 | `usr.sbin/emu/emu_arch_amd64.c` | amd64-specific emulator setup. Initializes amd64 CPU state, page tables, GDT, IDT. Sets up initial register state for FreeBSD kernel entry. |
| 5.11 | Implement arm64 emulation frontend | NOT STARTED | | | | 3.4, 5.2 | `usr.sbin/emu/emu_arch_arm64.c` | arm64-specific emulator setup. Initializes AArch64 CPU state, page tables, exception vectors. Sets up initial register state for FreeBSD kernel entry. |
| 5.12 | Implement riscv64 emulation frontend | NOT STARTED | | | | 3.7, 5.2 | `usr.sbin/emu/emu_arch_riscv.c` | riscv64-specific emulator setup. Initializes RISC-V CPU state, satp, mtvec/stvec. Sets up initial register state for FreeBSD kernel entry. |
| 5.13 | Implement i386 emulation frontend | NOT STARTED | | | | 3.10, 5.2 | `usr.sbin/emu/emu_arch_i386.c` | i386-specific emulator setup. Initializes x86-32 CPU state, page tables, GDT, IDT. Sets up initial register state for FreeBSD kernel entry. |
| 5.14 | Implement arm emulation frontend | NOT STARTED | | | | 3.11, 5.2 | `usr.sbin/emu/emu_arch_arm.c` | arm (32-bit) emulator setup. Initializes ARMv7 CPU state, page tables, exception vectors. Sets up initial register state for FreeBSD kernel entry. |
| 5.15 | Implement powerpc emulation frontend | NOT STARTED | | | | 3.12, 5.2 | `usr.sbin/emu/emu_arch_ppc.c` | powerpc-specific emulator setup. Initializes PowerISA CPU state, page tables, MSR. Sets up initial register state for FreeBSD kernel entry. |
| 5.16 | Implement GDB stub for debugging | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_gdb.c` | Remote GDB protocol implementation. Listens on a TCP port. Supports 'g' (read registers), 'G' (write registers), 'm' (read memory), 'M' (write memory), 'c' (continue), 's' (step), 'k' (kill), '?' (halt reason), 'z'/'Z' (breakpoints). |
| 5.17 | Implement snapshot/restore | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_snapshot.c` | `emu_snapshot_save(instance, path)`, `emu_snapshot_restore(instance, path)`. Serializes full emulator state (CPU registers, memory, devices, interrupts) to a file. Enables fast test iteration. |

### Phase 6: Userland Tooling (`usr.sbin/emu/`)

**Objective:** Provide a unified command-line tool for managing multiple emulated instances across different architectures simultaneously.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 6.1 | Create `usr.sbin/emu/` directory | NOT STARTED | | | | | | New directory for emu tool. Create subdirectories for arch-specific frontends if needed. |
| 6.2 | Implement `emu.c` — main CLI entry point | NOT STARTED | | | | 6.1 | `usr.sbin/emu/emu.c` | Command dispatch, option parsing. Subcommands: `init`, `start`, `stop`, `status`, `load`, `unload`, `stack`, `test`, `console`, `destroy`, `list`, `snapshot`, `restore`. Global flags: `--arch`, `--name`, `--memory`, `--cpus`, `--image`, `--kernel`, `--mode`, `--output-format` (json/tap/junit). |
| 6.3 | Implement `emu.h` — main header | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu.h` | Shared definitions and APIs. `struct emu_instance_config`, `struct emu_instance_state`, `struct emu_stack_output`. Function declarations for all subcommands. Constants for modes, statuses, output formats. |
| 6.4 | Implement `emu_init.c` — init command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_init.c` | `emu init --arch <arch> [--name <name>]`. Download/cache VM images, check dependencies (vmm.ko, bhyve). Creates instance configuration directory under `/var/emu/<name>/`. Validates architecture support. |
| 6.5 | Implement `emu_start.c` — start command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_start.c` | `emu start --name <name>`. Starts emulated instance. Selects optimal mode (bhyve vs emulator) based on host capabilities and target architecture. Forks child process, tracks PID. Updates instance state to RUNNING. |
| 6.6 | Implement `emu_stop.c` — stop command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_stop.c` | `emu stop --name <name> [--force]`. Stops emulated instance. Sends SIGTERM (or SIGKILL with --force). Waits for process exit. Captures final console output. Updates instance state to STOPPED. |
| 6.7 | Implement `emu_status.c` — status command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_status.c` | `emu status [--name <name>]`. Show status of one or all instances. Displays: name, arch, mode, pid, status, memory, cpus, uptime, console size. Supports `--output-format json` for AI-agent consumption. |
| 6.8 | Implement `emu_list.c` — list command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_list.c` | `emu list [--arch <arch>] [--status <status>]`. List all instances, optionally filtered by architecture or status. Shows summary table. |
| 6.9 | Implement `emu_load.c` — load command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_load.c` | `emu load --name <name> --module <path>`. Load kernel module into running instance. Uses kldload equivalent inside emulated environment. Verifies module loaded successfully. Reports load address and size. |
| 6.10 | Implement `emu_unload.c` — unload command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_unload.c` | `emu unload --name <name> --module <name>`. Unload kernel module from running instance. Uses kldunload equivalent. Verifies module unloaded. Reports any refcount issues. |
| 6.11 | Implement `emu_stack.c` — stack command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_stack.c` | `emu stack --name <name> [--pid <pid>]`. Capture/display kernel stacks from instance. Captures all thread stacks or specific PID. Outputs structured frame data. Supports `--output-format json`. |
| 6.12 | Implement `emu_test.c` — test command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_test.c` | `emu test --name <name> --test <path>`. Run test suite against instance. Supports TAP and JUnit XML output. Reports pass/fail/skip counts. Can run pre-defined test scripts. |
| 6.13 | Implement `emu_console.c` — console command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_console.c` | `emu console --name <name> [--tail] [--lines <n>]`. Display or tail serial console output from instance. Supports `--follow` for live tailing. Can dump full console buffer with `--dump`. |
| 6.14 | Implement `emu_destroy.c` — destroy command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_destroy.c` | `emu destroy --name <name> [--force]`. Destroy emulated instance. Stops if running, removes instance directory, cleans up `/dev/vmm/<name>` if applicable. With `--force`, skips confirmation. |
| 6.15 | Implement `emu_snapshot.c` — snapshot/restore | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_snapshot.c` | `emu snapshot --name <name> [--file <path>]`, `emu restore --name <name> --file <path>`. Save/restore emulator state. For bhyve mode, uses VMM snapshot ioctl. For emulator mode, uses `emu_snapshot_save/restore()`. |
| 6.16 | Implement `emu_config.c` — configuration | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_config.c` | Config file parsing (`emu.conf`). Reads `/usr/local/etc/emu.conf` and `~/.config/emu/emu.conf`. Supports: default_arch, default_memory, default_cpus, image_cache_dir, instance_dir, output_format. Uses XDG Base Directory spec. |
| 6.17 | Implement `emu_output.c` — output formatting | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_output.c` | Structured output formatting. `emu_output_json()`, `emu_output_tap()`, `emu_output_junit()`, `emu_output_table()`. Used by all subcommands for consistent output. |
| 6.18 | Implement `Makefile` for emu tool | NOT STARTED | | | | 6.2 | `usr.sbin/emu/Makefile` | Build system integration. Links with `libvmmapi` for bhyve mode. Conditional compilation for arch-specific frontends. `MAN= emu.8 emu.conf.5`. |
| 6.19 | Add `emu` to `usr.sbin/Makefile` | NOT STARTED | | | | 6.18 | `usr.sbin/Makefile` | Add `emu` to SUBDIR. Conditional on `MK_EMULATION != no`. |
| 6.20 | Add `emu` to per-arch `usr.sbin/Makefile.*` | NOT STARTED | | | | 6.19 | `usr.sbin/Makefile.amd64`, `Makefile.arm64`, `Makefile.arm`, `Makefile.i386`, `Makefile.powerpc`, `Makefile.riscv` | Per-arch build integration. Each arch's Makefile includes its emulation frontend. |

### Phase 7: Stack Examination & Debugging

**Objective:** Provide tools to capture and analyze kernel stacks from emulated environments, with structured output for AI-agent consumption.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 7.1 | Implement kernel-side stack capture interface | NOT STARTED | | | | 2.5 | `sys/emulation/emu_stack.c` | DDB/KDB integration for stack traces. `emu_stack_capture_kernel()` uses `db_trace_thread()` or `kdb_backtrace()`. Captures frame pointers, program counters, and stack pointers for each frame. |
| 7.2 | Implement bhyve stack capture via VMM snapshot | NOT STARTED | | | | 4.6 | `usr.sbin/emu/emu_bhyve.c` | Capture register state via VMM snapshot. Uses `vm_get_register_set()` to capture RIP, RSP, RBP, and other registers. Walks stack frames using frame pointer chain. |
| 7.3 | Implement custom emulator stack capture | NOT STARTED | | | | 5.16 | `usr.sbin/emu/emu_engine.c` | Capture guest register/stack state from emulator. Reads guest memory at SP to walk stack frames. Uses arch-specific frame layout (x86: RBP chain, arm64: FP/LR, riscv: s0/ra). |
| 7.4 | Implement symbol resolution | NOT STARTED | | | | 7.1 | `usr.sbin/emu/emu_sym.c` | Resolve kernel symbols from stack addresses. Reads kernel symbol table (from kernel file or running kernel). Uses `nlist()` or custom ELF symbol parsing. Returns symbol name + offset for each frame. |
| 7.5 | Implement frame pointer unwinding | NOT STARTED | | | | 7.4 | `usr.sbin/emu/emu_stack.c` | Walk stack frames using frame pointer chain. For x86: follow RBP chain. For arm64: follow FP chain. For riscv: follow s0 chain. Detects stack corruption and terminates early. |
| 7.6 | Implement structured stack output (JSON) | NOT STARTED | | | | 7.5 | `usr.sbin/emu/emu_output.c` | AI-agent friendly output. JSON format: `{"frames": [{"pc": ..., "sp": ..., "fp": ..., "symbol": "...", "module": "..."}], "arch": "...", "timestamp": "..."}`. |
| 7.7 | Implement module state inspection | NOT STARTED | | | | 2.15 | `usr.sbin/emu/emu_load.c` | List loaded modules, check status. `emu_module_list(instance)` returns list of (name, address, size, refcount, status). Can check if a specific module is loaded and healthy. |
| 7.8 | Implement crash dump analysis | NOT STARTED | | | | 7.1 | `usr.sbin/emu/emu_crash.c` | Parse and analyze vmcores from emulated environments. `emu_crash_analyze(dump_path)`. Extracts panic string, stack trace, CPU state, loaded modules. Outputs structured JSON. |

### Phase 8: Testing & Verification

**Objective:** Ensure the emulation framework itself is reliable and well-tested. All kernel module testing must occur inside emulated environments, never on the host.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 8.1 | Write unit tests for kernel option parsing | NOT STARTED | | | | 1.7 | `tests/sys/emulation/option_test.c` | Verify option enable/disable via `opt_emulation.h`. Test all `KERNEL_EMULATION_*` combinations. No kernel code loaded. |
| 8.2 | Write unit tests for capability detection | NOT STARTED | | | | 2.2 | `tests/sys/emulation/caps_test.c` | Mock VMM availability. Test `emu_detect_caps()` with VMM present/absent, native/cross-arch. No kernel code loaded. |
| 8.3 | Write unit tests for instance registry | NOT STARTED | | | | 2.4 | `tests/sys/emulation/instance_test.c` | Test create/destroy/find/list operations. Test concurrent access. Test name uniqueness. No kernel code loaded. |
| 8.4 | Write unit tests for stack capture | NOT STARTED | | | | 2.5 | `tests/sys/emulation/stack_test.c` | Verify stack trace formatting with mock frame data. Test frame unwinding with valid/corrupted chains. No kernel code loaded. |
| 8.5 | Write unit tests for mode selection | NOT STARTED | | | | 6.2 | `tests/usr.sbin/emu/mode_test.c` | Verify bhyve vs emulator selection logic. Test with VMM available/unavailable, native/cross-arch targets. No kernel code loaded. |
| 8.6 | Write integration test for bhyve lifecycle | NOT STARTED | | | | 4.7 | `tests/usr.sbin/emu/bhyve_lifecycle_test.sh` | Start bhyve instance, load test kernel module, verify module loaded, unload module, stop instance, destroy. All inside bhyve VM, never on host. |
| 8.7 | Write integration test for emulator lifecycle | NOT STARTED | | | | 5.2 | `tests/usr.sbin/emu/emulator_lifecycle_test.sh` | Start emulator instance, load test kernel module, verify module loaded, unload module, stop instance, destroy. All inside emulator, never on host. |
| 8.8 | Write integration test for crash detection | NOT STARTED | | | | 2.14 | `tests/usr.sbin/emu/crash_test.sh` | Load intentionally crashing kernel module, verify crash detected, capture crash dump, analyze stack. All inside emulated environment, never on host. |
| 8.9 | Write integration test for stack examination | NOT STARTED | | | | 7.6 | `tests/usr.sbin/emu/stack_test.sh` | Load test kernel module, capture stack trace, verify frame structure, verify symbol resolution, verify JSON output format. |
| 8.10 | Write integration test for cross-arch module loading | NOT STARTED | | | | 5.10–5.15 | `tests/usr.sbin/emu/cross_arch_test.sh` | Load module compiled for non-native arch. Verify emulator handles cross-arch binary correctly. Verify module loads and functions. |
| 8.11 | Write integration test for multi-instance management | NOT STARTED | | | | 6.7 | `tests/usr.sbin/emu/multi_instance_test.sh` | Start 3 instances of different architectures simultaneously. Verify all run independently. Stop one, verify others unaffected. Destroy all. |
| 8.12 | Write performance benchmark | NOT STARTED | | | | 8.6 | `tests/usr.sbin/emu/benchmark.sh` | Measure emulation overhead. Compare bhyve vs emulator vs native. Metrics: instructions/sec, module load time, stack capture time, memory usage. |

### Phase 9: Documentation & Release

**Objective:** Document the emulation framework for users and developers.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 9.1 | Write `emu.8` man page | NOT STARTED | | | | 6.18 | `usr.sbin/emu/emu.8` | User-facing documentation. Covers all subcommands, flags, configuration, examples. Includes sections: NAME, SYNOPSIS, DESCRIPTION, SUBCOMMANDS, OPTIONS, FILES, EXAMPLES, SEE ALSO. |
| 9.2 | Write `emu.conf.5` man page | NOT STARTED | | | | 6.16 | `usr.sbin/emu/emu.conf.5` | Configuration file documentation. Covers all config keys, default values, file locations, format. |
| 9.3 | Write `emulation.4` man page | NOT STARTED | | | | 2.2 | `sys/emulation/emulation.4` | Kernel subsystem documentation. Covers sysctl interface, kernel options, architecture support, device model. |
| 9.4 | Write developer documentation | NOT STARTED | | | | 9.3 | `share/doc/emulation/` | Architecture overview, API reference, porting guide for new architectures, device model documentation, GDB stub protocol. |
| 9.5 | Update `RELNOTES` | NOT STARTED | | | | 9.1 | `RELNOTES` | Summarize feature for release notes. Mention new kernel options, new userland tool, supported architectures. |
| 9.6 | Update `UPDATING` | NOT STARTED | | | | 9.1 | `UPDATING` | Admin-visible changes. Note new kernel options (default off), new `emu` command, configuration file locations. |
| 9.7 | Final code review | NOT STARTED | | | | 9.6 | | All phases complete. Review for style, locking correctness, memory safety, error handling, documentation completeness. |
| 9.8 | Commit to GitHub | NOT STARTED | | | | 9.7 | | Push to repository. Author: Mark LaPointe <mark@cloudbsd.org>. Co-authored-by: Junie <junie@jetbrains.com>. BSD 3-Clause license headers on all new files. |

---

## 6. Prior Art & Reference Works

### 6.1 bhyve / VMM (FreeBSD Base System)

The existing bhyve/VMM codebase in `sys/amd64/vmm/` and `usr.sbin/bhyve/` provides:

- **Kernel module architecture**: `vmm.ko` with VMCS/VMCB management, VM exit handling, EPT/NPT
- **Userland device model**: ACPI, IOAPIC, LAPIC, HPET, storage, network device emulation
- **`kernemu_dev`**: Kernel-emulated device pass-through for LAPIC/IOAPIC/HPET MMIO
- **`vmmapi` library**: `libvmmapi` for userland-VMM communication via `/dev/vmm` ioctls
- **Snapshot capability**: `BHYVE_SNAPSHOT` option for saving/restoring VM state

**Key files to reference:**

| File | What to Learn |
|------|---------------|
| `sys/amd64/vmm/vmm.c` | VM lifecycle management, VM exit dispatch |
| `sys/amd64/vmm/vmm_instruction_emul.c` | Instruction emulation for VM exits |
| `sys/amd64/vmm/intel/vmx.c` | Intel VT-x implementation |
| `sys/amd64/vmm/amd/svm.c` | AMD SVM implementation |
| `usr.sbin/bhyve/amd64/vmexit.c` | VM exit handling in userland |
| `usr.sbin/bhyve/amd64/kernemu_dev.c` | Kernel-emulated device pass-through |
| `usr.sbin/bhyve/bhyverun.c` | Main bhyve event loop |
| `lib/libvmmapi/vmmapi.c` | Userland VMM API |

### 6.2 QEMU (Reference Architecture)

QEMU's architecture provides design patterns for our custom emulator:

- **TCG (Tiny Code Generator)**: Dynamic binary translation for CPU emulation
- **Device model**: QOM (QEMU Object Model) for device hierarchy
- **Memory hierarchy**: AddressSpace, MemoryRegion, FlatView for memory management
- **QMP (QEMU Monitor Protocol)**: JSON-based control interface
- **GDB stub**: Remote debugging protocol support

**Key architectural patterns to adopt:**

| Pattern | Description |
|---------|-------------|
| Fetch-decode-execute loop | Main CPU emulation loop |
| Translation block caching | Cache translated instruction blocks |
| Memory region hierarchy | Overlapping memory regions with priority |
| Device bus model | Devices register on buses (PCI, ISA, MMIO) |
| Timer subsystem | Timers fire after configurable delays |
| Interrupt controller chain | Cascaded interrupt controllers |

### 6.3 Other Open-Source Emulators

| Emulator | Architecture | Key Takeaway |
|----------|-------------|--------------|
| TinyEmu | riscv | Minimalist design, easy to understand |
| Unicorn Engine | Multiple | Lightweight CPU-only emulation |
| Bochs | x86 | Interpretive emulation (no JIT) |
| GXemul | Multiple | Clean separation of CPU and device models |

---

## 7. Key Data Structures

### 7.1 Kernel-Side Structures (`sys/emulation/emu_internal.h`)

```c
/* Per-architecture emulation capabilities */
struct emu_caps {
    bool vmm_available;      /* bhyve/VMM available */
    bool native_arch;        /* Host == target architecture */
    char host_arch[32];      /* e.g., "amd64" */
};

/* Emulation instance configuration */
struct emu_config {
    char name[64];           /* Instance name (unique) */
    char arch[32];           /* Target architecture */
    int mode;                /* EMU_MODE_BHYVE or EMU_MODE_EMULATOR */
    int memory_mb;           /* Configured memory size in MB */
    int ncpus;               /* Number of CPUs */
    char image_path[PATH_MAX]; /* VM image path */
    char kernel_path[PATH_MAX]; /* Kernel/module path */
    char memory_policy[16];  /* "prealloc", "demand", "balloon" */
    int balloon_min_pct;     /* Minimum balloon size as %% of configured */
    bool memory_overcommit;  /* Allow overcommit for this instance */
};

/* Emulation instance state (kernel side) */
struct emu_instance {
    struct emu_config config; /* Instance configuration */
    pid_t pid;               /* PID of the backing process */
    int status;              /* EMU_STATUS_RUNNING, _STOPPED, _CRASHED */
    struct mtx lock;         /* Instance lock */
    struct emu_console *console; /* Console ring buffer */
    struct emu_crash *crash; /* Crash dump (if applicable) */
    struct emu_stack *stack; /* Last captured stack trace */
    struct emu_module_list modules; /* Loaded modules in guest */
    uint64_t memory_used;    /* Current actual memory usage in bytes */
    uint64_t memory_balloon_target; /* Balloon target size in bytes */
    struct mtx mem_lock;     /* Memory tracking lock */
    LIST_ENTRY(emu_instance) entries; /* List linkage */
};

/* Stack frame entry */
struct emu_stack_frame {
    uintptr_t pc;            /* Program counter */
    uintptr_t sp;            /* Stack pointer */
    uintptr_t fp;            /* Frame pointer */
    char symbol[256];        /* Resolved symbol name */
    char module[64];         /* Module containing this frame */
};

/* Stack trace */
struct emu_stack {
    int frame_count;
    struct emu_stack_frame frames[64]; /* Max 64 frames */
    char arch[32];
    struct timeval timestamp;
};
```

### 7.2 Userland Structures (`usr.sbin/emu/emu.h`)

```c
/* Emulator instance configuration (userland) */
struct emu_instance_config {
    char name[64];           /* Instance name */
    char arch[32];           /* Target architecture */
    int mode;                /* EMU_MODE_BHYVE or EMU_MODE_EMULATOR */
    pid_t pid;               /* Process ID */
    int status;              /* Running, stopped, crashed */
    char image_path[PATH_MAX]; /* VM image path */
    char kernel_path[PATH_MAX]; /* Kernel/module path */
    int memory_mb;           /* Memory size in MB */
    int ncpus;               /* Number of CPUs */
    char instance_dir[PATH_MAX]; /* /var/emu/<name>/ */
};

/* Stack trace output (JSON-friendly) */
struct emu_stack_output {
    int frame_count;
    struct {
        uintptr_t pc;
        uintptr_t sp;
        uintptr_t fp;
        char symbol[256];
        char module[64];
    } frames[64];            /* Max 64 frames */
    char arch[32];
    char timestamp[64];
};

/* Instance status output */
struct emu_instance_status {
    char name[64];
    char arch[32];
    char mode[16];           /* "bhyve" or "emulator" */
    int pid;
    char status[16];         /* "running", "stopped", "crashed" */
    int memory_mb;           /* Configured memory in MB */
    uint64_t memory_used;    /* Actual memory used in bytes */
    char memory_policy[16];  /* "prealloc", "demand", "balloon" */
    int ncpus;
    time_t uptime_sec;
    int console_size;        /* Bytes in console buffer */
    int module_count;        /* Loaded modules */
};
```

---

## 8. Sysctl Interface

Read/write sysctl nodes under `kern.emulation.*`:

| Sysctl | Type | Description |
|--------|------|-------------|
| `kern.emulation.enabled` | CTLTYPE_INT | Enable/disable emulation framework (default: 0) |
| `kern.emulation.mode` | CTLTYPE_STRING | Current mode: "disabled", "bhyve", "emulator" (read-only) |
| `kern.emulation.caps` | CTLTYPE_STRING | Emulation capabilities (read-only) |
| `kern.emulation.max_instances` | CTLTYPE_INT | Maximum number of concurrent instances (default: 16) |
| `kern.emulation.instances` | CTLTYPE_STRING | List all instances (read-only) |
| `kern.emulation.instance.<name>.status` | CTLTYPE_STRING | Instance status (read-only) |
| `kern.emulation.instance.<name>.arch` | CTLTYPE_STRING | Instance architecture (read-only) |
| `kern.emulation.instance.<name>.mode` | CTLTYPE_STRING | Instance mode (read-only) |
| `kern.emulation.instance.<name>.pid` | CTLTYPE_INT | Instance PID (read-only) |
| `kern.emulation.instance.<name>.stack` | CTLTYPE_STRING | Trigger/read stack trace (write to trigger, read to get) |
| `kern.emulation.instance.<name>.console` | CTLTYPE_STRING | Read console output (read-only) |
| `kern.emulation.instance.<name>.modules` | CTLTYPE_STRING | List loaded modules (read-only) |
| `kern.emulation.memory_policy` | CTLTYPE_STRING | Memory allocation policy: "prealloc", "demand", "balloon" (default: "demand") |
| `kern.emulation.memory_overcommit` | CTLTYPE_INT | Allow memory overcommit (0=off, 1=warn, 2=silent; default: 0) |
| `kern.emulation.memory_warn_percent` | CTLTYPE_INT | Warn when configured memory exceeds this % of available host RAM (default: 80) |
| `kern.emulation.memory_balloon_min_pct` | CTLTYPE_INT | Minimum balloon size as % of configured RAM (default: 10) |
| `kern.emulation.memory_balloon_interval` | CTLTYPE_INT | Balloon adjustment interval in seconds (default: 5) |
| `kern.emulation.memory_system_reserve_percent` | CTLTYPE_INT | Percentage of total physical memory reserved for OS and non-emulation processes (default: 20) |
| `kern.emulation.sandbox_capsicum` | CTLTYPE_INT | Enable Capsicum sandboxing for emulator processes (default: 1) |
| `kern.emulation.sandbox_strict` | CTLTYPE_INT | Strict mode: fail on Capsicum error (default: 0) |
| `kern.emulation.modules_loaded` | CTLTYPE_STRING | Comma-separated list of loaded emulation modules (read-only) |
| `kern.emulation.module.<name>.version` | CTLTYPE_INT | Version of loaded emulation module (read-only) |
| `kern.emulation.instance.<name>.memory_used` | CTLTYPE_UINT64 | Current actual memory usage in bytes (read-only) |
| `kern.emulation.instance.<name>.memory_policy` | CTLTYPE_STRING | Per-instance memory policy override (read-only) |
| `kern.emulation.instance.<name>.balloon_target` | CTLTYPE_UINT64 | Balloon target size in bytes (writable) |

---

## 9. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Custom CPU emulation is extremely complex | High | Start with interpretive emulation; add JIT later; leverage bhyve/VMM for native path |
| bhyve API changes between FreeBSD versions | Medium | Abstract bhyve interaction behind a stable interface in `emu_vmm.c` |
| Cross-architecture image management complexity | Medium | Provide image download/caching in `emu init` command |
| Kernel module ABI mismatch between host and emulated environment | High | Ensure the emulated kernel matches the module's target version; validate at load time |
| Emulator performance too slow for practical testing | Medium | Use bhyve when available; optimize hot paths; add JIT compilation later |
| Instruction decoder bugs causing incorrect emulation | High | Comprehensive test suite; compare against real hardware; fuzz testing |
| Multiple instances competing for resources | Medium | Per-instance resource limits; max_instances sysctl; memory caps |
| Race conditions in instance registry | Medium | Proper locking (`emu_instance_lock`); use `LIST` macros with mutex |
| Emulator escape via instruction decoder exploit | Critical | Bounds checking, no JIT (no WX memory), Capsicum sandboxing (see Section 6.5 of `002-Emulation-Security-FS.md` for full implementation) |
| Filesystem escape via shared directory symlinks | High | `realpath()` resolution, blocked path prefixes, read-only by default — see `002-Emulation-Security-FS.md` |
| Guest resource exhaustion (CPU/memory) | High | Per-instance memory limits, instruction count limits, watchdog timers — see `002-Emulation-Security-FS.md` |
| Network-based lateral movement from emulated instance | Medium | Host-only mode by default, MAC filtering, rate limiting — see `002-Emulation-Security-FS.md` |
| Unauthorized non-root access to emulation framework | High | Root-only default, `kern.emulation.allow_nonroot` sysctl, `emu` group membership — see `002-Emulation-Security-FS.md` |
| User destroys another user's instance | High | Ownership model, granular permissions, `PRIV_EMU_DESTROY` privilege — see `002-Emulation-Security-FS.md` |
| User exhausts system resources via excessive instances | Medium | Per-user instance/memory limits, `max_instances_per_user` sysctl — see `002-Emulation-Security-FS.md` |
| Memory overcommit causes host OOM or swap thrashing | High | Demand paging with `MAP_NORESERVE`, `memory_overcommit` sysctl (default off), `memory_warn_percent` threshold (based on total host physical minus system-wide used memory (OS + other processes) minus already-consumed by other instances minus safety margin), balloon driver to reclaim memory under pressure — see `002-Emulation-Security-FS.md` |
| Balloon driver bug causes guest instability or memory corruption | High | Balloon operates within guest-allocated pages only, min balloon floor via `memory_balloon_min_pct`, validation of balloon target values |

---

## 10. TODO — Step-by-Step Implementation Tracker

This section is the master checklist for implementing the kernel emulation framework. Each task includes:
- **Status:** `NOT STARTED` | `IN PROGRESS` | `COMPLETED`
- **Owner:** Who is working on it
- **Start Date:** When work began
- **End Date:** When work finished
- **Dependencies:** What must be done first
- **Files Modified:** What files are touched
- **Notes:** Any blockers, decisions, or context

### Phase 0: Foundation and Setup

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 0.1 | Create feature branch `feature/kernel-emulation` | NOT STARTED | | | | | | Branch from `main` |
| 0.2 | Set up bhyve test environment for native-path testing | NOT STARTED | | | | | | Must support VMM and snapshot rollback |
| 0.3 | Verify existing kernel builds pass on clean branch | NOT STARTED | | | | 0.2 | | Baseline before any changes |
| 0.4 | Document baseline kernel module test workflow | NOT STARTED | | | | 0.3 | | Current process, pain points, metrics |

### Phase 1: Kernel Module Build System Integration

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 1.1 | Create `sys/modules/emu_core/Makefile` | NOT STARTED | | | | 0.1 | `sys/modules/emu_core/Makefile` | Builds `emu_core.ko`. Source: `sys/emulation/emu_main.c`, `emu_sysctl.c`, `emu_instance.c`, `emu_stack.c`, `emu_vmm.c`, `emu_cpu.c`, `emu_mem.c`, `emu_intr.c`, `emu_device.c`, `emu_console.c`, `emu_crash.c`, `emu_module.c`, `emu_memmgmt.c`. Declares `MODULE_VERSION(emu_core, 1)`. |
| 1.2 | Create `sys/modules/emu_amd64/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_amd64/Makefile` | Builds `emu_amd64.ko`. Source: `sys/emulation/amd64/emu_cpu_amd64.c`, `emu_mmu_amd64.c`, `emu_intr_amd64.c`. Declares `MODULE_DEPEND(emu_amd64, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_amd64, 1)`. |
| 1.3 | Create `sys/modules/emu_aarch64/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_aarch64/Makefile` | Builds `emu_aarch64.ko`. Source: `sys/emulation/arm64/emu_cpu_arm64.c`, `emu_mmu_arm64.c`, `emu_intr_arm64.c`. Declares `MODULE_DEPEND(emu_aarch64, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_aarch64, 1)`. |
| 1.4 | Create `sys/modules/emu_arm/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_arm/Makefile` | Builds `emu_arm.ko`. Source: `sys/emulation/arm/emu_cpu_arm.c`. Declares `MODULE_DEPEND(emu_arm, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_arm, 1)`. |
| 1.5 | Create `sys/modules/emu_i386/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_i386/Makefile` | Builds `emu_i386.ko`. Source: `sys/emulation/i386/emu_cpu_i386.c`. Declares `MODULE_DEPEND(emu_i386, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_i386, 1)`. |
| 1.6 | Create `sys/modules/emu_powerpc/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_powerpc/Makefile` | Builds `emu_powerpc.ko`. Source: `sys/emulation/powerpc/emu_cpu_ppc.c`. Declares `MODULE_DEPEND(emu_powerpc, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_powerpc, 1)`. |
| 1.7 | Create `sys/modules/emu_riscv/Makefile` | NOT STARTED | | | | 1.1 | `sys/modules/emu_riscv/Makefile` | Builds `emu_riscv.ko`. Source: `sys/emulation/riscv/emu_cpu_riscv.c`, `emu_mmu_riscv.c`, `emu_intr_riscv.c`. Declares `MODULE_DEPEND(emu_riscv, emu_core, 1, 1, 1)` and `MODULE_VERSION(emu_riscv, 1)`. |
| 1.8 | Create `sys/modules/emu/Makefile` (master module) | NOT STARTED | | | | 1.2–1.7 | `sys/modules/emu/Makefile` | Builds `emu.ko` — master module with no source files. Declares `MODULE_DEPEND(emu, emu_core, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_amd64, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_aarch64, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_arm, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_i386, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_powerpc, 1, 1, 1)`, `MODULE_DEPEND(emu, emu_riscv, 1, 1, 1)`. `kldload emu` loads all emulation modules. |
| 1.9 | Add emulation modules to `sys/modules/Makefile` | NOT STARTED | | | | 1.8 | `sys/modules/Makefile` | Add `emu`, `emu_core`, `emu_amd64`, `emu_aarch64`, `emu_arm`, `emu_i386`, `emu_powerpc`, `emu_riscv` to SUBDIR. Conditional on `MACHINE_CPUARCH` where appropriate (e.g., `emu_amd64` only on amd64 host). |
| 1.10 | Add `MK_EMULATION` build option to `share/mk/bsd.opts.mk` | NOT STARTED | | | | 1.9 | `share/mk/bsd.opts.mk` | Add `__DEFAULT_NO_OPTIONS` entry for `MK_EMULATION`. Controls whether emulation modules are built as part of `make buildworld` / `make modules`. |
| 1.11 | Add `EMULATION` to `share/mk/src.opts.mk` | NOT STARTED | | | | 1.10 | `share/mk/src.opts.mk` | Register `MK_EMULATION` as a src option so it appears in `make showconfig`. |
| 1.12 | Create `sys/emulation/` directory structure | NOT STARTED | | | | 1.1 | `sys/emulation/`, `sys/emulation/amd64/`, `sys/emulation/arm64/`, `sys/emulation/arm/`, `sys/emulation/i386/`, `sys/emulation/powerpc/`, `sys/emulation/riscv/` | New directories for emulation subsystem source files. |

### Phase 2: Kernel-Side Emulation Framework (`sys/emulation/`)

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 2.1 | Create `sys/emulation/` directory | NOT STARTED | | | | 1.8 | | New directory for emulation subsystem |
| 2.2 | Implement `emu_main.c` — core framework | NOT STARTED | | | | 2.1 | `sys/emulation/emu_main.c` | Module declaration, init/deinit, capability detection |
| 2.3 | Implement `emu_sysctl.c` — sysctl interface | NOT STARTED | | | | 2.2 | `sys/emulation/emu_sysctl.c` | `kern.emulation.*` sysctl tree |
| 2.4 | Implement `emu_instance.c` — instance registry | NOT STARTED | | | | 2.2 | `sys/emulation/emu_instance.c` | Multi-instance management |
| 2.5 | Implement `emu_stack.c` — stack examination | NOT STARTED | | | | 2.2 | `sys/emulation/emu_stack.c` | Stack trace capture and formatting |
| 2.6 | Implement `emu_internal.h` — internal header | NOT STARTED | | | | 2.2 | `sys/emulation/emu_internal.h` | Internal data structures and APIs |
| 2.7 | Implement `emu.h` — public API header | NOT STARTED | | | | 2.2 | `sys/emulation/emu.h` | Public API for kernel consumers |
| 2.8 | Implement `emu_vmm.c` — VMM integration | NOT STARTED | | | | 2.2 | `sys/emulation/emu_vmm.c` | Interface to vmm.ko for bhyve mode |
| 2.9 | Implement `emu_cpu.c` — CPU emulation core | NOT STARTED | | | | 2.2 | `sys/emulation/emu_cpu.c` | Architecture-independent CPU emulation primitives |
| 2.10 | Implement `emu_mem.c` — memory emulation | NOT STARTED | | | | 2.9 | `sys/emulation/emu_mem.c` | Memory region management, MMIO dispatch |
| 2.11 | Implement `emu_intr.c` — interrupt controller emulation | NOT STARTED | | | | 2.9 | `sys/emulation/emu_intr.c` | Interrupt injection and delivery |
| 2.12 | Implement `emu_device.c` — device model framework | NOT STARTED | | | | 2.10 | `sys/emulation/emu_device.c` | Device registration, MMIO/PIO dispatch |
| 2.13 | Implement `emu_console.c` — console capture | NOT STARTED | | | | 2.2 | `sys/emulation/emu_console.c` | Capture guest console output |
| 2.14 | Implement `emu_crash.c` — crash detection | NOT STARTED | | | | 2.2 | `sys/emulation/emu_crash.c` | Detect panics, capture crash dumps |
| 2.15 | Implement `emu_module.c` — module state tracking | NOT STARTED | | | | 2.2 | `sys/emulation/emu_module.c` | Track loaded modules in emulated environment |
| 2.16 | Implement `emu_memmgmt.c` — memory management & tracking | NOT STARTED | | | | 2.3 | `sys/emulation/emu_memmgmt.c` | Memory policy sysctls (`memory_policy`, `memory_overcommit`, `memory_warn_percent`, `memory_balloon_min_pct`, `memory_balloon_interval`, `memory_system_reserve_percent`), per-instance `memory_used` tracking, host memory capacity detection (total physical minus system-wide used memory (OS + other processes) minus already-consumed by other instances minus safety margin), overcommit warning logic, per-instance balloon target interface |

### Phase 3: Architecture-Specific CPU Emulation

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 3.1 | Implement amd64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/amd64/emu_cpu_amd64.c` | x86-64 instruction decoder, register state |
| 3.2 | Implement amd64 MMU emulation | NOT STARTED | | | | 3.1 | `sys/emulation/amd64/emu_mmu_amd64.c` | Page table walk, TLB simulation |
| 3.3 | Implement amd64 interrupt model | NOT STARTED | | | | 3.1 | `sys/emulation/amd64/emu_intr_amd64.c` | IDT, APIC, exception handling |
| 3.4 | Implement arm64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/arm64/emu_cpu_arm64.c` | AArch64 instruction decoder |
| 3.5 | Implement arm64 MMU emulation | NOT STARTED | | | | 3.4 | `sys/emulation/arm64/emu_mmu_arm64.c` | Stage 1/2 page tables |
| 3.6 | Implement arm64 interrupt model | NOT STARTED | | | | 3.4 | `sys/emulation/arm64/emu_intr_arm64.c` | GIC emulation |
| 3.7 | Implement riscv64 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/riscv/emu_cpu_riscv.c` | RISC-V instruction decoder |
| 3.8 | Implement riscv64 MMU emulation | NOT STARTED | | | | 3.7 | `sys/emulation/riscv/emu_mmu_riscv.c` | Sv39/Sv48 page tables |
| 3.9 | Implement riscv64 interrupt model | NOT STARTED | | | | 3.7 | `sys/emulation/riscv/emu_intr_riscv.c` | CLINT/PLIC emulation |
| 3.10 | Implement i386 CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/i386/emu_cpu_i386.c` | x86-32 instruction decoder |
| 3.11 | Implement arm (32-bit) CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/arm/emu_cpu_arm.c` | ARMv7 instruction decoder |
| 3.12 | Implement powerpc CPU emulation | NOT STARTED | | | | 2.9 | `sys/emulation/powerpc/emu_cpu_ppc.c` | PowerISA instruction decoder |

### Phase 4: bhyve/VMM Integration (Native Path)

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 4.1 | Implement VMM capability detection | NOT STARTED | | | | 2.8 | `sys/emulation/emu_vmm.c` | Check `/dev/vmm`, CPU features, vmm.ko loaded |
| 4.2 | Implement bhyve VM creation via ioctl | NOT STARTED | | | | 4.1 | `usr.sbin/emu/emu_bhyve.c` | `vm_create()`, `vm_setup_memory()` |
| 4.3 | Implement bhyve VM boot (bhyveload) | NOT STARTED | | | | 4.2 | `usr.sbin/emu/emu_bhyve.c` | Load kernel/module into VM |
| 4.4 | Implement bhyve console capture | NOT STARTED | | | | 4.3 | `usr.sbin/emu/emu_bhyve.c` | Capture serial output |
| 4.5 | Implement bhyve crash detection | NOT STARTED | | | | 4.4 | `usr.sbin/emu/emu_bhyve.c` | Detect panics via VMM snapshot |
| 4.6 | Implement bhyve stack capture | NOT STARTED | | | | 4.5 | `usr.sbin/emu/emu_bhyve.c` | Use VMM snapshot for register/stack state |
| 4.7 | Implement bhyve VM stop/destroy | NOT STARTED | | | | 4.6 | `usr.sbin/emu/emu_bhyve.c` | Clean shutdown and resource release |
| 4.8 | Implement bhyve snapshot/restore | NOT STARTED | | | | 4.7 | `usr.sbin/emu/emu_bhyve.c` | Fast test iteration via snapshots |
| 4.9 | Implement virtio-balloon device for bhyve path | NOT STARTED | | | | 4.2 | `usr.sbin/bhyve/pci_virtio_balloon.c` | virtio-balloon driver for memory reclaim. Inflate/deflate via guest cooperation. Balloon target set via sysctl `kern.emulation.instance.<name>.balloon_target`. Minimum floor via `memory_balloon_min_pct`. Periodic adjustment via `memory_balloon_interval` timer. |

### Phase 5: Custom Emulator Engine (Pure Emulation Path)

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 5.1 | Design emulator architecture document | NOT STARTED | | | | 2.9 | `docs/emulator-arch.md` | Document the emulator design |
| 5.2 | Implement emulator main loop | NOT STARTED | | | | 5.1 | `usr.sbin/emu/emu_engine.c` | Fetch-decode-execute loop |
| 5.3 | Implement memory bus | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Memory read/write dispatch |
| 5.4 | Implement MMIO dispatch | NOT STARTED | | | | 5.3 | `usr.sbin/emu/emu_engine.c` | Device MMIO region routing |
| 5.5 | Implement boot ROM loader | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_boot.c` | Load FreeBSD kernel into emulated memory |
| 5.6 | Implement serial console device | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_uart.c` | NS16550-compatible UART |
| 5.7 | Implement timer device (HPET/PIT) | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_timer.c` | Timer interrupts for scheduling |
| 5.8 | Implement interrupt controller | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_intr.c` | Interrupt routing |
| 5.9 | Implement virtio-style storage device | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_dev_storage.c` | Block device for root filesystem |
| 5.10 | Implement amd64 emulation frontend | NOT STARTED | | | | 3.1, 5.2 | `usr.sbin/emu/emu_arch_amd64.c` | amd64-specific emulator setup |
| 5.11 | Implement arm64 emulation frontend | NOT STARTED | | | | 3.4, 5.2 | `usr.sbin/emu/emu_arch_arm64.c` | arm64-specific emulator setup |
| 5.12 | Implement riscv64 emulation frontend | NOT STARTED | | | | 3.7, 5.2 | `usr.sbin/emu/emu_arch_riscv.c` | riscv64-specific emulator setup |
| 5.13 | Implement i386 emulation frontend | NOT STARTED | | | | 3.10, 5.2 | `usr.sbin/emu/emu_arch_i386.c` | i386-specific emulator setup |
| 5.14 | Implement arm emulation frontend | NOT STARTED | | | | 3.11, 5.2 | `usr.sbin/emu/emu_arch_arm.c` | arm-specific emulator setup |
| 5.15 | Implement powerpc emulation frontend | NOT STARTED | | | | 3.12, 5.2 | `usr.sbin/emu/emu_arch_ppc.c` | powerpc-specific emulator setup |
| 5.16 | Implement GDB stub for debugging | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_gdb.c` | Remote GDB protocol for stack examination |
| 5.17 | Implement snapshot/restore | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_snapshot.c` | Save/restore emulator state |
| 5.18 | Implement demand-paged guest memory allocation | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Use `mmap(MAP_ANON | MAP_NORESERVE)` instead of `malloc()` for guest memory. Pages are faulted in by the OS on first access. Track `memory_used` via `mincore()` or periodic RSS sampling. Support `memory_policy` values: "prealloc" (traditional `malloc`), "demand" (`mmap` with `MAP_NORESERVE`), "balloon" (prealloc + balloon device). Validate `memory_overcommit` setting against host memory capacity on instance start. |

### Phase 6: Userland Tooling (`usr.sbin/emu/`)

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 6.1 | Create `usr.sbin/emu/` directory | NOT STARTED | | | | | | New directory for emu tool |
| 6.2 | Implement `emu.c` — main CLI entry point | NOT STARTED | | | | 6.1 | `usr.sbin/emu/emu.c` | Command dispatch, option parsing. Global flags: `--arch`, `--name`, `--memory`, `--memory-policy` (prealloc/demand/balloon), `--memory-overcommit`, `--cpus`, `--image`, `--kernel`, `--mode`, `--output-format` (json/tap/junit). |
| 6.3 | Implement `emu.h` — main header | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu.h` | Shared definitions and APIs |
| 6.4 | Implement `emu_init.c` — init command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_init.c` | Download/cache VM images, check deps |
| 6.5 | Implement `emu_start.c` — start command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_start.c` | Start emulated instance |
| 6.6 | Implement `emu_stop.c` — stop command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_stop.c` | Stop emulated instance |
| 6.7 | Implement `emu_status.c` — status command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_status.c` | Show status of instances. Displays: name, arch, mode, pid, status, configured memory, actual memory used, memory policy, cpus, uptime, console size. Supports `--output-format json` for AI-agent consumption. |
| 6.8 | Implement `emu_list.c` — list command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_list.c` | List all instances |
| 6.9 | Implement `emu_load.c` — load command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_load.c` | Load kernel module into instance |
| 6.10 | Implement `emu_unload.c` — unload command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_unload.c` | Unload kernel module from instance |
| 6.11 | Implement `emu_stack.c` — stack command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_stack.c` | Capture/display kernel stacks |
| 6.12 | Implement `emu_test.c` — test command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_test.c` | Run test suite against instance |
| 6.13 | Implement `emu_console.c` — console command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_console.c` | Attach to serial console |
| 6.14 | Implement `emu_destroy.c` — destroy command | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_destroy.c` | Destroy emulated instance |
| 6.15 | Implement `emu_snapshot.c` — snapshot/restore | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_snapshot.c` | Save/restore emulator state |
| 6.16 | Implement `emu_config.c` — configuration | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_config.c` | Config file parsing (`emu.conf`). Reads `/usr/local/etc/emu.conf` and `~/.config/emu/emu.conf`. Supports: default_arch, default_memory, default_cpus, default_memory_policy, default_memory_overcommit, image_cache_dir, instance_dir, output_format. Uses XDG Base Directory spec. |
| 6.17 | Implement `emu_output.c` — output formatting | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_output.c` | JSON, TAP, JUnit XML output |
| 6.18 | Implement `Makefile` for emu tool | NOT STARTED | | | | 6.2 | `usr.sbin/emu/Makefile` | Build system integration |
| 6.19 | Add `emu` to `usr.sbin/Makefile` | NOT STARTED | | | | 6.18 | `usr.sbin/Makefile` | Add emu to SUBDIR |
| 6.20 | Add `emu` to per-arch `usr.sbin/Makefile.*` | NOT STARTED | | | | 6.19 | `usr.sbin/Makefile.amd64`, etc. | Per-arch build integration |

### Phase 7: Stack Examination & Debugging

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 7.1 | Implement kernel-side stack capture interface | NOT STARTED | | | | 2.5 | `sys/emulation/emu_stack.c` | DDB/KDB integration for stack traces |
| 7.2 | Implement bhyve stack capture via VMM snapshot | NOT STARTED | | | | 4.6 | `usr.sbin/emu/emu_bhyve.c` | Capture register state via VMM |
| 7.3 | Implement custom emulator stack capture | NOT STARTED | | | | 5.16 | `usr.sbin/emu/emu_engine.c` | Capture guest register/stack state |
| 7.4 | Implement symbol resolution | NOT STARTED | | | | 7.1 | `usr.sbin/emu/emu_sym.c` | Resolve kernel symbols from stack |
| 7.5 | Implement frame pointer unwinding | NOT STARTED | | | | 7.4 | `usr.sbin/emu/emu_stack.c` | Walk stack frames |
| 7.6 | Implement structured stack output (JSON) | NOT STARTED | | | | 7.5 | `usr.sbin/emu/emu_output.c` | AI-agent friendly output |
| 7.7 | Implement module state inspection | NOT STARTED | | | | 2.15 | `usr.sbin/emu/emu_load.c` | List loaded modules, check status |
| 7.8 | Implement crash dump analysis | NOT STARTED | | | | 7.1 | `usr.sbin/emu/emu_crash.c` | Parse and analyze vmcores |

### Phase 8: Testing & Verification

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 8.1 | Write unit tests for kernel module loading | NOT STARTED | | | | 1.8 | `tests/sys/emulation/module_test.c` | Verify `kldload emu` loads all sub-modules, `kldload emu_amd64` loads emu_core automatically, MOD_UNLOAD with active instances returns EBUSY |
| 8.2 | Write unit tests for capability detection | NOT STARTED | | | | 2.2 | `tests/sys/emulation/caps_test.c` | Mock VMM availability |
| 8.3 | Write unit tests for instance registry | NOT STARTED | | | | 2.4 | `tests/sys/emulation/instance_test.c` | Test create/destroy/find/list |
| 8.4 | Write unit tests for stack capture | NOT STARTED | | | | 2.5 | `tests/sys/emulation/stack_test.c` | Verify stack trace formatting |
| 8.5 | Write unit tests for mode selection | NOT STARTED | | | | 6.2 | `tests/usr.sbin/emu/mode_test.c` | Verify bhyve vs emulator selection |
| 8.6 | Write integration test for bhyve lifecycle | NOT STARTED | | | | 4.7 | `tests/usr.sbin/emu/bhyve_lifecycle_test.sh` | Start, load module, stop, destroy |
| 8.7 | Write integration test for emulator lifecycle | NOT STARTED | | | | 5.2 | `tests/usr.sbin/emu/emulator_lifecycle_test.sh` | Start, load module, stop, destroy |
| 8.8 | Write integration test for crash detection | NOT STARTED | | | | 2.14 | `tests/usr.sbin/emu/crash_test.sh` | Load crashing module, verify detection |
| 8.9 | Write integration test for stack examination | NOT STARTED | | | | 7.6 | `tests/usr.sbin/emu/stack_test.sh` | Capture and verify stack output |
| 8.10 | Write integration test for cross-arch module loading | NOT STARTED | | | | 5.10–5.15 | `tests/usr.sbin/emu/cross_arch_test.sh` | Load module on non-native arch |
| 8.11 | Write integration test for multi-instance management | NOT STARTED | | | | 6.7 | `tests/usr.sbin/emu/multi_instance_test.sh` | Run 3 instances of different archs |
| 8.12 | Write performance benchmark | NOT STARTED | | | | 8.6 | `tests/usr.sbin/emu/benchmark.sh` | Measure emulation overhead |
| 8.13 | Write memory management tests | NOT STARTED | | | | 2.16, 5.18 | `tests/usr.sbin/emu/memory_test.sh` | Verify demand paging reduces actual memory usage below configured. Verify balloon inflate/deflate. Verify overcommit warning at threshold. Verify prealloc mode allocates full memory. Verify `memory_used` tracking accuracy. |

### Phase 9: Documentation & Release

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| 9.1 | Write `emu.8` man page | NOT STARTED | | | | 6.18 | `usr.sbin/emu/emu.8` | User-facing documentation |
| 9.2 | Write `emu.conf.5` man page | NOT STARTED | | | | 6.16 | `usr.sbin/emu/emu.conf.5` | Configuration file documentation |
| 9.3 | Write `emulation.4` man page | NOT STARTED | | | | 2.2 | `sys/emulation/emulation.4` | Kernel subsystem documentation |
| 9.4 | Write developer documentation | NOT STARTED | | | | 9.3 | `share/doc/emulation/` | Architecture, API, porting guide |
| 9.5 | Update `RELNOTES` | NOT STARTED | | | | 9.1 | `RELNOTES` | Summarize feature for release |
| 9.6 | Update `UPDATING` | NOT STARTED | | | | 9.1 | `UPDATING` | Admin-visible changes |
| 9.7 | Final code review | NOT STARTED | | | | 9.6 | | All phases complete |
| 9.8 | Commit to GitHub | NOT STARTED | | | | 9.7 | | Push to repository |

---

## 11. Future Enhancements

1. **JIT compilation**: Add dynamic binary translation (like QEMU's TCG) to improve emulator performance for hot code paths.
2. **Per-instance CPU affinity**: Pin emulator processes to specific CPU cores for performance isolation.
3. **NUMA awareness**: Optimize memory allocation for multi-socket systems when running multiple instances.
4. **Snapshot diffing**: Compare snapshots before/after module load to detect memory leaks or unexpected modifications.
5. **Network emulation**: Add virtio-net device for testing network-related kernel modules.
6. **PCIe emulation**: Add PCIe bus model for testing PCIe device drivers.
7. **ACPI table generation**: Generate ACPI tables for emulated environments to support more complex kernel features.
8. **Kernel debugger integration**: Allow attaching `kgdb` to emulated instances for interactive debugging.
9. **Fuzz testing integration**: Use the emulator as a target for kernel module fuzzing with structured input generation.
10. **CI/CD integration templates**: Provide GitHub Actions and Jenkins pipeline templates for automated emulation testing.

---

## 12. Task Completion Checklist

> **Note for agents:** When picking up a task, fill in the **Assigned To** column with your agent name/ID. When completing a task, update the **Status** column to `COMPLETED` and add your name/ID to the **Assigned To** column if not already filled. This ensures traceability across sessions.

| # | Item | Category | Status | Assigned To | Dependencies | Files | Notes |
|---|------|----------|--------|------------|--------------|-------|-------|
| C.1 | `.plan/` directory created with all plan files | Planning | NOT STARTED | | | `.plan/` | Foundation for all tracking |
| C.2 | `sys/modules/emu_core/Makefile` created | Build System | NOT STARTED | | C.1 | `sys/modules/emu_core/Makefile` | Builds `emu_core.ko` with `MODULE_VERSION(emu_core, 1)` |
| C.3 | Per-arch module Makefiles created (`emu_amd64`, `emu_aarch64`, etc.) | Build System | NOT STARTED | | C.2 | `sys/modules/emu_*/Makefile` | Each declares `MODULE_DEPEND` on `emu_core` |
| C.4 | `sys/modules/emu/Makefile` (master module) created | Build System | NOT STARTED | | C.3 | `sys/modules/emu/Makefile` | `kldload emu` loads all sub-modules via `MODULE_DEPEND` |
| C.5 | Emulation modules added to `sys/modules/Makefile` SUBDIR | Build System | NOT STARTED | | C.4 | `sys/modules/Makefile` | Conditional on `MACHINE_CPUARCH` |
| C.6 | `MK_EMULATION` build option added to `share/mk/bsd.opts.mk` | Build System | NOT STARTED | | C.5 | `share/mk/bsd.opts.mk` | `__DEFAULT_NO_OPTIONS` entry |
| C.7 | `sys/emulation/` directory created with core framework | Kernel | NOT STARTED | | C.2 | `sys/emulation/` | Core kernel module source files |
| C.8 | Architecture-specific CPU emulation implemented | Emulator | NOT STARTED | | C.7 | Phase 3 files | amd64, arm64, riscv64, i386, arm, powerpc |
| C.9 | bhyve/VMM integration implemented | bhyve | NOT STARTED | | C.7 | Phase 4 files | Native-speed execution path |
| C.10 | Custom emulator engine implemented | Emulator | NOT STARTED | | C.8 | Phase 5 files | Cross-architecture execution path |
| C.11 | `usr.sbin/emu/` directory created with CLI tool | Userland | NOT STARTED | | C.9, C.10 | Phase 6 files | `emu` command-line interface |
| C.12 | Multi-instance management implemented | Userland | NOT STARTED | | C.11 | `usr.sbin/emu/emu_list.c`, `emu_status.c` | Different archs simultaneously |
| C.13 | Stack examination and debugging implemented | Debugging | NOT STARTED | | C.11 | Phase 7 files | Stack capture, symbol resolution, GDB stub |
| C.14 | Memory management sysctls implemented | Memory | NOT STARTED | | C.7 | `sys/emulation/emu_memmgmt.c` | `memory_policy`, `memory_overcommit`, `memory_warn_percent`, `memory_balloon_min_pct`, `memory_balloon_interval`, `memory_system_reserve_percent` |
| C.15 | Demand-paged guest memory (`mmap MAP_NORESERVE`) implemented | Memory | NOT STARTED | | C.10 | `usr.sbin/emu/emu_engine.c` | Custom emulator demand paging |
| C.16 | virtio-balloon device implemented for bhyve path | Memory | NOT STARTED | | C.9 | `usr.sbin/bhyve/pci_virtio_balloon.c` | bhyve memory reclaim |
| C.17 | Per-instance `memory_used` tracking implemented | Memory | NOT STARTED | | C.14 | `sys/emulation/emu_memmgmt.c` | Actual memory usage monitoring |
| C.18 | Memory overcommit safeguards and warnings implemented | Memory | NOT STARTED | | C.14 | `sys/emulation/emu_memmgmt.c` | Host capacity check (total physical minus system-wide used (OS + other processes) minus already-consumed by other instances minus safety margin), threshold warning |
| C.19 | Memory management tests written and passing | Testing | NOT STARTED | | C.14–C.18 | `tests/usr.sbin/emu/memory_test.sh` | Demand paging, balloon, overcommit, system-wide memory awareness |
| C.20 | Test suite written and passing | Testing | NOT STARTED | | C.19 | Phase 8 files | Unit, integration, and performance tests |
| C.21 | Documentation and man pages written | Documentation | NOT STARTED | | C.20 | Phase 9 files | Man pages, developer docs, RELNOTES |
| C.22 | Committed to GitHub | Release | NOT STARTED | | C.21 | | Push to repository |

---

## 13. Conclusion

The recommended approach provides:

- **No external dependencies**: Everything is built from source in the FreeBSD tree
- **Dual-mode operation**: bhyve for native speed, custom emulator for cross-architecture
- **Loadable kernel modules**: `emu.ko` master module loads all sub-modules; individual arch modules (`emu_amd64.ko`, `emu_aarch64.ko`, etc.) can be loaded independently
- **One module to load them all**: `kldload emu` loads the entire emulation framework via `MODULE_DEPEND` chain
- **Per-architecture granularity**: Load only the architectures you need with `kldload emu_amd64`
- **Off by default**: No modules loaded at boot; explicit `kldload` required
- **Multi-instance support**: Run emulated environments of different architectures simultaneously
- **Stack examination**: First-class support for debugging and AI-agent consumption
- **Dynamic memory management**: Demand-paged memory allocation reduces host memory consumption to only actively used pages; balloon driver for bhyve path; configurable memory policy (prealloc/demand/balloon) with overcommit safeguards and warnings; system-wide memory awareness (OS + other processes) for host capacity detection
- **Capsicum sandboxing**: Both custom emulator and bhyve paths enter capability mode after initialization, restricting file descriptor rights and preventing filesystem escape, process injection, and lateral movement
- **Incremental deployment**: Start with amd64 (bhyve), add architectures over time

This approach aligns with FreeBSD's design philosophy, reuses existing VMM/bhyve infrastructure where appropriate, and builds a lightweight custom emulator for cross-architecture testing — all without external dependencies.
