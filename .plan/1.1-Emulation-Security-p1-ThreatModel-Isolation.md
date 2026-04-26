# Emulation Framework Security — Chapter 1: Threat Model & Isolation Architecture

> **Part of:** Security chapter series (002a through 002f)
> **See also:** `000-Emulation-TOC.md` for master index, `001-Emulation-Overview.md` for main plan

---

## 1. Executive Summary

This document provides the security architecture, threat model, and isolation model for the kernel emulation framework described in `001-Emulation-Overview.md`. It covers both execution paths:

- **bhyve/VMM path**: When running amd64-on-amd64 with hardware virtualization
- **Custom emulator path**: When running any architecture on any host via software emulation

The core security principle is **host safety**: kernel module testing must never run on the development or CI host directly. All testing occurs inside isolated emulated environments that cannot affect the host system.

**Key design decisions:**
- Emulated instances run as **unprivileged userland processes** (custom emulator) or under **VMM hardware isolation** (bhyve path)
- **No direct hardware access** from emulated environments — all I/O goes through emulated devices
- **Filesystem sharing** uses controlled bind mounts, never full host filesystem access
- **Default-off** for all emulation features — explicit opt-in required
- **Root-only by default** — non-root access requires explicit sysctl enablement and group membership
- **Ownership-based access control** — each instance has an owner, with granular permissions per operation
- **Multi-instance isolation** — each instance is independent and cannot affect others
- **Layered defense**: Access control → OS isolation → Emulator isolation → Filesystem/network controls

**Security chapter series:**
| File | Title | Content |
|------|-------|---------|
| `002a` | Threat Model & Isolation | Executive summary, threat model, trust model, isolation architecture, bhyve path security |
| `002b` | Access Control & Authorization | Group config, ownership, permissions, privilege definitions, jail integration, resource limits |
| `002c` | Custom Emulator Deep-Dive | All custom emulator security analysis (37 subsections) |
| `002d` | Filesystem, Devices & Crash Safety | Filesystem strategy, device security, network security, crash containment |
| `002e` | Additional Security Analysis | Audit logging, MAC, securelevel, memory scrubbing, core dumps, ptrace, TOCTOU, signals, OOM, entropy, supply chain, firmware |
| `002f` | Implementation Phases & Checklists | All implementation phases (S0-S18), recommendations, data structures, sysctls, risks, checklists, conclusion |

---

## 2. Threat Model

### 2.1 Assets to Protect

| Asset | Protection Requirement | Severity if Compromised |
|-------|----------------------|------------------------|
| Host kernel memory | Must not be accessible from emulated environment | Critical — full host compromise |
| Host userspace processes | Must not be readable/writable from emulated environment | High — data leakage, process manipulation |
| Host filesystem | Only explicitly shared directories are accessible | High — data leakage, tampering |
| Host network | Only explicitly configured network access | Medium — lateral movement |
| Other emulated instances | Must be fully isolated from each other | High — cross-instance attack |
| Emulator process itself | Must not be exploitable to gain host access | Critical — escape vector |
| Emulation control interface | Must require authentication for privileged operations | High — unauthorized instance management |
| Emulation kernel modules | Must not be loadable by unauthorized users | Critical — kernel-level access |
| Firmware blobs | Must be verified before loading | High — malicious firmware compromise |
| Snapshot/state files | Must have restricted access and be cleaned up | Medium — data leakage |
| Audit logs | Must be tamper-proof and complete | Medium — forensic evidence |

### 2.2 Threat Categories

| Threat | Description | Path | Severity | Likelihood |
|--------|-------------|------|----------|------------|
| **Emulator escape** | Malicious kernel module exploits bug in custom emulator to execute code on host | Custom emulator | Critical | Low (with mitigations) |
| **VMM escape** | Malicious guest exploits bug in bhyve/VMM to break out of VM | bhyve path | Critical | Very low (mature codebase) |
| **Instruction decoder exploit** | Crafted instruction sequence triggers buffer overflow in decoder | Custom emulator | Critical | Low (with sandboxing) |
| **Memory corruption** | Guest writes to out-of-bounds memory in emulator's address space | Custom emulator | High | Low (with bounds checking) |
| **Filesystem escape** | Guest uses shared directory symlinks to access host files | Both | High | Medium (with path validation) |
| **Resource exhaustion** | Guest consumes all host CPU/memory via emulator | Both | Medium | Medium (with limits) |
| **Side-channel leakage** | Guest observes host activity via timing or cache effects | bhyve path | Low | Low |
| **Device emulation bug** | Malicious guest exploits bug in emulated device | Both | High | Low (simple devices) |
| **Network lateral movement** | Guest uses network access to attack other hosts | Both | Medium | Medium (with network controls) |
| **Snapshot data leakage** | Saved emulator state contains sensitive data | Both | Medium | Low (with file permissions) |
| **Unauthorized instance access** | Non-owner user accesses or modifies another user's instance | Both | High | Medium (with access controls) |
| **Privilege escalation via emu group** | User in emu group escalates to root via emulator bug | Both | Critical | Low (with privilege dropping) |
| **Instance resource theft** | User creates excessive instances to deny service to others | Both | Medium | Medium (with per-user limits) |
| **Malicious module loading** | Attacker loads a malicious emulation kernel module (`emu_*.ko`) to gain kernel-level access | Kernel | Critical | Low (root-only kldload, signed modules) |
| **Module unloading crash** | Unloading an emulation module while instances are active causes kernel panic | Kernel | High | Low (refcount tracking, busy check) |
| **Module version mismatch** | Incompatible module versions loaded together cause undefined behavior | Kernel | Medium | Low (MODULE_VERSION/MODULE_DEPEND checks) |
| **Audit log tampering** | Attacker modifies or deletes audit logs to cover tracks | Both | Medium | Low (append-only logs) |
| **MAC policy bypass** | Emulator operates outside MAC framework restrictions | Both | High | Low (with MAC integration) |
| **Securelevel bypass** | Emulator allows operations restricted by securelevel | Both | High | Low (with securelevel checks) |
| **Memory scrubbing failure** | Instance memory contains sensitive data from previous instance | Both | High | Low (with scrubbing) |
| **Core dump leakage** | Emulator core dump contains guest secrets | Both | Medium | Low (with coredump restrictions) |
| **ptrace attack** | Debugger attaches to emulator process and reads guest memory | Both | Critical | Low (with ptrace restrictions) |
| **TOCTOU race** | Permission check passes but instance state changes before operation | Both | Medium | Low (with atomic operations) |
| **Signal injection** | Malicious signal triggers unexpected behavior in emulator | Both | Medium | Low (with signal handlers) |
| **OOM killer** | Emulator process killed during critical operation | Both | High | Medium (with OOM protection) |
| **Entropy exhaustion** | Guest has insufficient entropy for cryptographic operations | Both | Medium | Low (with virtio-rng) |
| **Supply chain attack** | Compromised emulator binaries injected into build/release | Both | Critical | Low (with signed builds) |
| **Firmware backdoor** | Malicious firmware blob loaded into emulated environment | Both | High | Low (with GPG verification) |

### 2.3 Attack Surface Comparison

| Component | bhyve Path | Custom Emulator Path |
|-----------|-----------|---------------------|
| Kernel component | `vmm.ko` (mature, well-audited) | `emu_core.ko` + `emu_<arch>.ko` (new, modular, smaller per-module surface) |
| Userland process | `bhyve` process (mature) | `emu` process (new) |
| CPU emulation | Hardware (VT-x/AMD-V) | Software instruction decoder (in `emu_<arch>.ko`) |
| Memory isolation | EPT/NPT (hardware) | Process address space (software) |
| Device emulation | In bhyve userland process | In emu userland process |
| IOMMU protection | Yes (VT-d/AMD-Vi) | N/A (no passthrough) |
| Attack surface | Large (full device models) | Small (minimal device models) |
| Access control | devfs permissions + privilege checks | devfs permissions + privilege checks + granular per-op ACL |
| Module loading | Single `vmm.ko` | Hierarchical: `emu.ko` (master) → `emu_core.ko` + `emu_<arch>.ko` |
| Sandboxing | Capsicum after privilege drop | Capsicum after initialization |
| Code maturity | Years of security auditing | New code, needs thorough review |

### 2.4 Trust Model

The emulation framework operates under these trust assumptions:

| Trust Level | Entities | Privileges | Restrictions |
|-------------|----------|------------|--------------|
| **Fully trusted** | Root user, kernel | All operations | Must authenticate as root |
| **Partially trusted** | `emu` group members | Instance management (configurable) | Cannot bypass kernel security |
| **Minimally trusted** | Regular users | List own instances only | Cannot create/modify instances |
| **Untrusted** | Guest code in emulated instance | None on host | Fully isolated by emulator |
| **Untrusted** | Network peers | None | Firewalled, rate-limited |

---

## 3. Isolation Architecture

### 3.1 Two-Path Isolation Model

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HOST (FreeBSD)                                │
│                                                                      │
│  ┌─────────────────────────────────┐  ┌──────────────────────────┐  │
│  │  bhyve Path (native amd64)       │  │  Custom Emulator Path     │  │
│  │                                  │  │  (any arch)               │  │
│  │  ┌───────────────────────────┐   │  │  ┌────────────────────┐  │  │
│  │  │  bhyve (userland process) │   │  │  │  emu (userland     │  │  │
│  │  │  - Device emulation       │   │  │  │  process)          │  │  │
│  │  │  - Memory via /dev/vmm    │   │  │  │  - CPU emulation   │  │  │
│  │  │  - Runs as unprivileged   │   │  │  │  - Device emulation│  │  │
│  │  │    user                   │   │  │  │  - Memory in heap  │  │  │
│  │  └───────────┬───────────────┘   │  │  │  - Runs as         │  │  │
│  │              │                    │  │  │    unprivileged    │  │  │
│  │  ┌───────────▼───────────────┐   │  │  │    user            │  │  │
│  │  │  vmm.ko (kernel module)   │   │  │  └────────┬───────────┘  │  │
│  │  │  - VMCS/VMCB management   │   │  │           │              │  │
│  │  │  - EPT/NPT page tables    │   │  │  ┌────────▼───────────┐  │  │
│  │  │  - VM exit dispatch       │   │  │  │  Kernel Modules:   │  │  │
│  │  │  - IOMMU protection       │   │  │  │  emu_core.ko       │  │  │
│  │  └───────────────────────────┘   │  │  │  emu_amd64.ko      │  │  │
│  └─────────────────────────────────┘  │  │  emu_aarch64.ko    │  │  │
│                                       │  │  emu_riscv.ko      │  │  │
│                                       │  │  (loaded via kldload)│  │
│                                       │  └────────────────────┘  │
│                                       └──────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 Process-Level Isolation (Custom Emulator)

The custom emulator runs as a **regular userland process** with no special privileges:

- **No root required**: The emulator process runs as the invoking user
- **Kernel modules provide CPU emulation**: `emu_core.ko` and `emu_<arch>.ko` provide the CPU emulation logic in kernel space, while the userland `emu` process handles device emulation and orchestration
- **Standard process isolation**: The OS enforces process boundaries via virtual memory, file descriptors, and process credentials
- **No /dev/vmm access**: The custom emulator does not use the VMM interface
- **Capsicum sandboxing**: The emulator process is further restricted using FreeBSD's Capsicum capability mode (`cap_enter()`) to drop privileges after initialization. See `002c-Emulation-Security-p3-CustomEmulator.md` Section 6.5 for full implementation details.

**Memory isolation:**
- Guest memory is allocated via `malloc()` or `mmap()` in the emulator's heap
- Guest memory is never mapped into any other process
- The emulator's own code and data are in separate memory regions
- Bounds checking on all guest memory accesses prevents out-of-range reads/writes

**File descriptor isolation:**
- The emulator only opens: the disk image file, the kernel/module file, and the console pipe
- No access to `/dev`, `/proc`, or other host special files
- All other file descriptors are closed after initialization

### 3.3 VMM-Level Isolation (bhyve Path)

The bhyve path leverages FreeBSD's existing VMM infrastructure:

- **Hardware-enforced isolation**: EPT/NPT (2nd-level page tables) prevent the guest from accessing host memory
- **IOMMU protection**: VT-d/AMD-Vi prevents DMA attacks from passthrough devices
- **Kernel-mediated**: All VMM operations go through `vmm.ko` which validates all ioctls
- **Userland device emulation**: Device models run in the bhyve userland process, not in the kernel
- **Proven codebase**: bhyve/VMM has years of security auditing

**Key security properties of bhyve path:**
- Guest cannot access host memory (EPT/NPT enforcement)
- Guest cannot perform DMA attacks (IOMMU enforcement)
- Guest cannot directly access host devices (all devices are emulated)
- Guest cannot affect other VMs (separate EPT/NPT hierarchies)
- Guest cannot persist across VM destruction (all memory is freed)

### 3.4 Multi-Instance Isolation

Multiple emulated instances run independently and cannot interfere with each other:

- **Separate processes**: Each instance is a separate OS process with its own PID
- **Separate address spaces**: Process isolation prevents cross-instance memory access
- **Separate file descriptors**: Each instance has its own set of open files
- **Separate network namespaces** (future): Instances can be isolated at the network level
- **Instance registry locking**: The kernel-side instance registry uses mutex-protected operations
- **Ownership isolation**: Each instance is owned by a specific user; `cr_cansee()` prevents cross-user visibility

**Instance lifecycle security:**
```
Create → Start → [Running] → Stop → Destroy
  │        │         │         │       │
  │        │    Crash detection │       └── Cleanup: remove /var/emu/<name>/
  │        │         │         │            close /dev/vmm/<name> (bhyve)
  │        │         └── Capture dump     free all memory
  │        │                              kill process if still running
  │        └── Fork child process
  └── Validate config, check resources, check permissions
```

### 3.5 Kernel Module Security

The emulation framework is implemented as a hierarchy of loadable kernel modules. This introduces specific security considerations:

**Module hierarchy:**
```
emu.ko (master, no code, only MODULE_DEPEND declarations)
  └── emu_core.ko (core framework: instance registry, sysctl, VMM interface)
        ├── emu_amd64.ko (amd64 CPU emulation)
        ├── emu_aarch64.ko (arm64 CPU emulation)
        ├── emu_arm.ko (arm 32-bit CPU emulation)
        ├── emu_i386.ko (i386 CPU emulation)
        ├── emu_powerpc.ko (powerpc CPU emulation)
        └── emu_riscv.ko (riscv CPU emulation)
```

**Module loading security:**

| Concern | Mitigation |
|---------|------------|
| Unauthorized module loading | `kldload` requires root privilege by default. `kern.module.allow_nonroot` sysctl (default 0) controls non-root loading. |
| Malicious module injection | FreeBSD supports signed kernel modules via `MODULE_VERIFICATION` (KLD verification with public-key crypto). All `emu_*.ko` modules should be signed. |
| Module unloading while in use | Each module tracks active instances via reference counting. `emu_core.ko` refuses to unload if any instances exist. Arch modules refuse to unload if instances of that architecture are active. |
| Module version mismatch | `MODULE_VERSION` and `MODULE_DEPEND` macros enforce version compatibility. `emu_amd64.ko` declares `MODULE_DEPEND(emu_amd64, emu_core, 1, 1, 1)` requiring `emu_core.ko` version 1 exactly. |
| Module loading order | `MODULE_DEPEND` ensures correct loading order. Loading `emu_amd64.ko` automatically loads `emu_core.ko` first. |
| Module unloading order | The kernel's module dependency system prevents unloading `emu_core.ko` while `emu_amd64.ko` is still loaded. |

**Module initialization security:**
```c
/* Each module's modevent handler validates state before initialization */
static int
emu_core_modevent(module_t mod, int type, void *unused)
{
    int error = 0;

    switch (type) {
    case MOD_LOAD:
        /* Validate no conflicting modules are loaded */
        if (emu_conflicting_modules()) {
            printf("emu_core: conflicting module detected\n");
            return (EINVAL);
        }
        /* Initialize instance registry with mutex */
        emu_instance_init();
        /* Create sysctl tree under kern.emulation */
        emu_sysctl_init();
        /* Register devfs device /dev/emuctl */
        emu_devfs_init();
        break;
    case MOD_UNLOAD:
        /* Refuse unload if any instances exist */
        if (emu_instance_count() > 0) {
            printf("emu_core: %d instances still active, refusing unload\n",
                emu_instance_count());
            return (EBUSY);
        }
        /* Clean up devfs, sysctl, instance registry */
        emu_devfs_cleanup();
        emu_sysctl_cleanup();
        emu_instance_cleanup();
        break;
    }
    return (error);
}
```

**Per-architecture module security:**
```c
/* Arch module modevent — validates arch support before loading */
static int
emu_amd64_modevent(module_t mod, int type, void *unused)
{
    int error = 0;

    switch (type) {
    case MOD_LOAD:
        /* Verify host CPU supports required features */
        if (!cpu_feature & CPUID_EMULATION_AMD64) {
            printf("emu_amd64: host CPU does not support required features\n");
            return (ENODEV);
        }
        /* Register amd64 CPU emulation handlers with emu_core */
        emu_arch_register(EMU_ARCH_AMD64, &amd64_emu_ops);
        break;
    case MOD_UNLOAD:
        /* Refuse unload if amd64 instances are active */
        if (emu_arch_instance_count(EMU_ARCH_AMD64) > 0) {
            printf("emu_amd64: %d amd64 instances active, refusing unload\n",
                emu_arch_instance_count(EMU_ARCH_AMD64));
            return (EBUSY);
        }
        /* Unregister handlers */
        emu_arch_unregister(EMU_ARCH_AMD64);
        break;
    }
    return (error);
}
```

**Module visibility and introspection:**
- `kldstat` shows all loaded emulation modules with their versions
- `kern.emulation.modules_loaded` sysctl provides a comma-separated list
- `kern.emulation.module.<name>.version` sysctl shows individual module versions
- `kern.emulation.module.<name>.refcount` sysctl shows how many instances depend on each module

**Module unloading safety:**
- The master `emu.ko` module has no code and no MOD_UNLOAD handler — it can only be unloaded after all dependent modules are unloaded
- `emu_core.ko` refuses unload if any instances exist (checked via `emu_instance_count()`)
- Arch modules refuse unload if instances of that architecture are active
- The kernel's module dependency system prevents unloading modules that have dependents

---

## 4. bhyve/VMM Path Security

### 4.1 Existing Security Mechanisms

The bhyve/VMM codebase already provides strong isolation:

| Mechanism | Description | Status |
|-----------|-------------|--------|
| EPT/NPT | 2nd-level page tables prevent guest host memory access | Already present |
| IOMMU (VT-d/AMD-Vi) | Prevents DMA attacks from passthrough devices | Already present |
| MSR bitmaps | Controls which MSRs the guest can access | Already present |
| CPUID masking | Controls which CPU features are exposed to guest | Already present |
| `/dev/vmm` permissions | Only root and `vm` group can create VMs | Already present |
| Userland device models | Devices run in userland, not kernel | Already present |
| Per-VM ucred | Each VM device tracks creating user's credentials | Already present |
| `cr_cansee()` | Cross-user VM visibility check | Already present |
| `PRIV_VMM_CREATE/DESTROY` | Privilege checks for VM lifecycle | Already present |
| `vm_maxvmms` sysctl | Per-user instance limit | Already present |

### 4.2 Additional Security for Emulation Framework

When using bhyve as the backend for the emulation framework, additional security measures apply:

**Restricted device model:**
- Only essential devices are emulated (UART, HPET, virtio-blk, virtio-net)
- No passthrough devices allowed
- No legacy device emulation unless explicitly requested
- Device configuration is validated before VM start

**Snapshot security:**
- Snapshot files contain guest memory and register state
- Snapshots are stored with 0600 permissions
- Snapshot files are cleaned up on instance destroy
- Snapshot data is never shared between instances

**Console isolation:**
- Serial console output is captured via pipe, not shared PTY
- Console buffer is per-instance and not accessible from other instances
- Console data is truncated to configurable maximum size (default 64KB)

### 4.3 bhyve Process Hardening

The bhyve process used by the emulation framework should be hardened:

```c
/* After VM creation and device setup, drop privileges */
if (setgid(gid) != 0 || setuid(uid) != 0) {
    warn("Failed to drop privileges");
    exit(1);
}

/* Close unnecessary file descriptors */
for (int fd = 3; fd < getdtablesize(); fd++) {
    if (!is_essential_fd(fd))
        close(fd);
}

/* Capsicum sandboxing — see 002c Section 6.5 for full implementation */
emu_bhyve_enter_sandbox(inst);
```
