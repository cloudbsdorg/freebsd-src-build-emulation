# Emulation Framework Security, Filesystem & Device Integration — Implementation Plan

## 1. Executive Summary

This document provides the security architecture, threat model, and filesystem strategy for the kernel emulation framework described in `001-Emulation-Overview.md`. It covers both execution paths:

- **bhyve/VMM path**: When running amd64-on-amd64 with hardware virtualization
- **Custom emulator path**: When running any architecture on any host via software emulation

The core security principle is **host safety**: kernel module testing must never run on the development or CI host directly. All testing occurs inside isolated emulated environments that cannot affect the host system.

**Key design decisions:**
- Emulated instances run as **unprivileged userland processes** (custom emulator) or under **VMM hardware isolation** (bhyve path)
- **No direct hardware access** from emulated environments — all I/O goes through emulated devices
- **Filesystem sharing** uses controlled bind mounts, never full host filesystem access
- **Default-off** for all emulation features — explicit opt-in required
- **Multi-instance isolation** — each instance is independent and cannot affect others

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

### 2.3 Attack Surface Comparison

| Component | bhyve Path | Custom Emulator Path |
|-----------|-----------|---------------------|
| Kernel component | `vmm.ko` (mature, well-audited) | `emulation.ko` (new, smaller surface) |
| Userland process | `bhyve` process (mature) | `emu` process (new) |
| CPU emulation | Hardware (VT-x/AMD-V) | Software instruction decoder |
| Memory isolation | EPT/NPT (hardware) | Process address space (software) |
| Device emulation | In bhyve userland process | In emu userland process |
| IOMMU protection | Yes (VT-d/AMD-Vi) | N/A (no passthrough) |
| Attack surface | Large (full device models) | Small (minimal device models) |

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
│  │  │  vmm.ko (kernel module)   │   │  │  └────────────────────┘  │  │
│  │  │  - VMCS/VMCB management   │   │  │                          │  │
│  │  │  - EPT/NPT page tables    │   │  │  No kernel component     │  │
│  │  │  - VM exit dispatch       │   │  │  for pure emulation      │  │
│  │  │  - IOMMU protection       │   │  │                          │  │
│  │  └───────────────────────────┘   │  └──────────────────────────┘  │
│  └─────────────────────────────────┘                                │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 Process-Level Isolation (Custom Emulator)

The custom emulator runs as a **regular userland process** with no special privileges:

- **No root required**: The emulator process runs as the invoking user
- **No kernel component needed**: Pure emulation requires no kernel module
- **Standard process isolation**: The OS enforces process boundaries via virtual memory, file descriptors, and process credentials
- **No /dev/vmm access**: The custom emulator does not use the VMM interface
- **Capsicum sandboxing** (future): The emulator process can be further restricted using FreeBSD's Capsicum capability mode (`cap_enter()`) to drop privileges after initialization

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

**Instance lifecycle security:**
```
Create → Start → [Running] → Stop → Destroy
  │        │         │         │       │
  │        │    Crash detection │       └── Cleanup: remove /var/emu/<name>/
  │        │         │         │            close /dev/vmm/<name> (bhyve)
  │        │         └── Capture dump     free all memory
  │        │                              kill process if still running
  │        └── Fork child process
  └── Validate config, check resources
```

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

/* Future: Capsicum sandboxing */
#ifdef CAPABILITIES
    cap_rights_t rights;
    cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_MMAP);
    if (cap_rights_limit(vm_fd, &rights) < 0)
        err(1, "cap_rights_limit");
    if (cap_enter() < 0)
        err(1, "cap_enter");
#endif
```

---

## 5. Custom Emulator Security

### 5.1 Attack Surface Analysis

The custom emulator has a smaller but more critical attack surface than bhyve:

| Component | Attack Surface | Risk | Mitigation |
|-----------|---------------|------|------------|
| Instruction decoder | Parses arbitrary guest code | Critical — buffer overflow, infinite loop | Bounds checking, operand validation, instruction limit |
| Memory emulation | Translates guest virtual addresses | High — out-of-bounds access | Bounds checking on all memory operations |
| MMU emulation | Walks guest page tables | High — infinite loop, invalid entries | Page table depth limit, valid entry checks |
| Device emulation | Handles MMIO reads/writes | Medium — device state corruption | Input validation on all MMIO operations |
| ELF loader | Parses kernel/module ELF files | High — buffer overflow | ELF validation, section bounds checking |
| GDB stub | Accepts remote debugger connections | Medium — arbitrary memory read/write | Authentication, localhost-only binding |

### 5.2 Instruction Decoder Safety

The instruction decoder is the most security-critical component:

**Design principles:**
- **No dynamic code generation**: The emulator uses interpretive emulation (no JIT initially), avoiding the need for WX memory
- **Bounds-checked operands**: All operand reads verify they are within the instruction buffer
- **Instruction length limit**: Maximum instruction length is enforced (e.g., 15 bytes for x86-64)
- **Instruction count limit**: A configurable instruction limit per execution slice prevents infinite loops
- **Validated opcodes**: Unknown or invalid opcodes raise an illegal instruction exception rather than crashing the emulator

**Decoder safety pattern:**
```c
int
emu_decode_instruction(struct emu_cpu *cpu, uint8_t *bytes, int max_len)
{
    int consumed = 0;

    if (max_len > MAX_INSTRUCTION_LENGTH)
        max_len = MAX_INSTRUCTION_LENGTH;

    /* Check each byte before reading */
    while (consumed < max_len) {
        if (consumed >= max_len)
            return (EMU_ERR_INVALID_INSTRUCTION);

        uint8_t opcode = bytes[consumed++];
        /* ... decode ... */
    }

    return (consumed);
}
```

### 5.3 Memory Safety

Guest memory access is always bounds-checked:

```c
int
emu_mem_read(struct emu_instance *inst, uint64_t gaddr,
             void *buf, size_t size)
{
    /* Validate guest address range */
    if (gaddr + size > inst->mem_size || gaddr + size < gaddr) {
        /* Address outside guest memory — raise MMIO or fault */
        if (is_mmio_region(inst, gaddr)) {
            return (emu_mmio_dispatch(inst, gaddr, buf, size, EMU_MMIO_READ));
        }
        return (EMU_ERR_FAULT);
    }

    /* Safe: bounds-checked copy from guest memory */
    memcpy(buf, inst->guest_mem + gaddr, size);
    return (0);
}
```

**Key memory safety properties:**
- Guest memory is a single contiguous allocation — no guest pointer can reference outside it
- MMIO regions are checked before guest RAM — prevents guest from mapping MMIO over RAM
- All memory operations check `gaddr + size` against `inst->mem_size`
- No guest code can execute on the host CPU — all instructions are interpreted

### 5.4 ELF Loader Safety

Loading a kernel or module into the emulator involves parsing ELF files:

```c
int
emu_elf_load(struct emu_instance *inst, const char *path)
{
    /* Validate ELF header */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return (EMU_ERR_NOT_ELF);
    }

    /* Validate architecture matches instance target */
    if (ehdr->e_machine != inst->arch_elf_machine) {
        return (EMU_ERR_ARCH_MISMATCH);
    }

    /* Validate each program header before loading */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        /* Check segment fits in guest memory */
        if (phdr[i].p_vaddr + phdr[i].p_memsz > inst->mem_size) {
            return (EMU_ERR_SEGMENT_OVERFLOW);
        }
        /* Check segment doesn't overlap with emulator private regions */
        if (overlaps_emulator_region(inst, phdr[i].p_vaddr, phdr[i].p_memsz)) {
            return (EMU_ERR_SEGMENT_CONFLICT);
        }
    }

    /* Load validated segments */
    /* ... */
}
```

### 5.5 Capsicum Sandboxing (Future Enhancement)

FreeBSD's Capsicum framework can further restrict the emulator process:

```c
/* After initialization, enter capability mode */
if (cap_enter() < 0) {
    warn("cap_enter failed — continuing without sandbox");
} else {
    /* In capability mode: no new rights, no /proc, no sysctl, etc. */
    /* Only previously acquired capabilities are available */
}
```

Capsicum would prevent the emulator from:
- Opening new files (after initialization)
- Accessing `/proc` or `/dev`
- Making arbitrary sysctl calls
- Creating new network sockets (if not needed)
- Forking new processes

---

## 6. Filesystem Strategy

### 6.1 Architecture for Kernel Module Development

The filesystem strategy supports rapid kernel module iteration while maintaining host safety:

```
Host                           Emulated Instance
────                           ─────────────────
                                ┌─────────────────────┐
                                │  Guest OS           │
                                │  (emulated kernel)  │
                                │                     │
                                │  / (root)           │
                                │    ├── boot/        │
                                │    ├── dev/         │
                                │    ├── etc/         │
                                │    ├── mnt/         │
                                │    │   └── src/  ◄──┤── Shared source
                                │    ├── tmp/         │
                                │    └── usr/         │
                                └─────────────────────┘
                                       ▲
                                       │ bind mount
                                       │
┌─────────────────────┐     ┌─────────────────────┐
│  Host Source Tree    │     │  Base Image          │
│  /home/user/module/  │     │  /var/emu/<name>/   │
│                      │     │  disk.img            │
│  Shared read-write   │     │  (read-only base)    │
│  for compilation     │     │                      │
└─────────────────────┘     └─────────────────────┘
```

### 6.2 Base Image Approach

Each emulated instance boots from a **base disk image** that contains a minimal FreeBSD installation:

**Image types:**

| Type | Description | Use Case | Security |
|------|-------------|----------|----------|
| Read-only base | Immutable root filesystem | Clean test environment | Cannot be permanently modified |
| Copy-on-write overlay | ZFS clone or file copy | Ephemeral test instances | Discarded after test |
| Writable snapshot | ZFS snapshot that can be rolled back | Iterative development | Rollback to clean state |

**Image management:**
- Base images are stored in `/var/emu/images/` with 0644 permissions
- Instance-specific overlays are stored in `/var/emu/<name>/` with 0700 permissions
- Images are validated (checksum, format) before use
- Only the `emu` tool and root can modify the image cache

### 6.3 Source Code Sharing

For kernel module development, the host source tree is shared into the emulated instance:

**bhyve path — virtio-9p (future):**
```
bhyve: -s <slot>,virtio-9p,host_path=/home/user/module,tag=src
Guest: mount -t 9p src /mnt/src
```

**Custom emulator path — direct file mapping:**
```
emu start --name test --share /home/user/module:/mnt/src
```

The custom emulator implements file sharing by intercepting guest filesystem calls and translating them to host file operations. This is simpler than full 9p protocol support:

```c
/* In emulator's syscall handler */
int
emu_handle_open(struct emu_instance *inst, uint64_t path_gaddr, int flags)
{
    char guest_path[PATH_MAX];

    /* Read guest path string from guest memory */
    emu_mem_read(inst, path_gaddr, guest_path, sizeof(guest_path));

    /* Resolve against share mappings */
    for (int i = 0; i < inst->nshares; i++) {
        if (path_prefix_match(guest_path, inst->shares[i].guest_prefix)) {
            /* Translate to host path */
            snprintf(host_path, sizeof(host_path), "%s%s",
                     inst->shares[i].host_path,
                     guest_path + strlen(inst->shares[i].guest_prefix));

            /* Open with restricted flags */
            return (open(host_path, translate_flags(flags), 0644));
        }
    }

    /* Path not in any share — deny */
    return (EMU_ERR_EPERM);
}
```

### 6.4 Filesystem Security Rules

| Rule | Enforcement | Rationale |
|------|-------------|-----------|
| No host `/dev` access | Share paths must not start with `/dev` | Prevents device node access |
| No host `/proc` access | Share paths must not start with `/proc` | Prevents process information leakage |
| No host `/sys` access | Share paths must not start with `/sys` | Prevents kernel parameter access |
| No host root `/` access | Share paths must be subdirectories | Prevents full filesystem access |
| Symlink escape prevention | All shared paths are resolved with `realpath()` before use | Prevents symlink-based escape |
| Read-only by default | Shares are read-only unless `--writable` flag is given | Prevents accidental host modification |
| UID/GID mapping | File ownership is mapped to guest UID/GID space | Prevents privilege escalation via file ownership |

**Path validation:**
```c
int
emu_validate_share_path(const char *host_path)
{
    char resolved[PATH_MAX];

    /* Resolve symlinks and relative paths */
    if (realpath(host_path, resolved) == NULL)
        return (EMU_ERR_INVALID_PATH);

    /* Block dangerous paths */
    const char *blocked_prefixes[] = {
        "/dev", "/proc", "/sys", "/etc", "/var/emu",
    };
    for (int i = 0; i < nitems(blocked_prefixes); i++) {
        if (strncmp(resolved, blocked_prefixes[i],
                    strlen(blocked_prefixes[i])) == 0) {
            return (EMU_ERR_PATH_BLOCKED);
        }
    }

    /* Must be a subdirectory, not root */
    if (strcmp(resolved, "/") == 0)
        return (EMU_ERR_PATH_BLOCKED);

    return (0);
}
```

### 6.5 ZFS Integration for Clean Test State

When ZFS is available, snapshots provide fast rollback to clean state:

**Workflow:**
```
# Create a dataset for the test VM
zfs create -o mountpoint=/var/emu/test-vm zroot/emu/test-vm

# Install base system into the dataset
emu init --arch amd64 --name test-vm --zfs-dataset zroot/emu/test-vm

# Take a clean snapshot
emu snapshot --name test-vm --snapshot clean

# Run tests (modifies the dataset)
emu start --name test-vm --kernel /path/to/test.ko
emu test --name test-vm

# Roll back to clean state
emu rollback --name test-vm --snapshot clean

# Iterate
```

**Security:**
- ZFS snapshots are read-only and cannot be modified by the guest
- Rollback is atomic and guaranteed to produce a clean state
- Snapshot names are validated to prevent path traversal
- Only the `emu` tool and root can create/destroy snapshots

---

## 7. Device Emulation Security

### 7.1 Device Model Principles

All devices in the emulation framework follow these security principles:

1. **No direct hardware access**: All devices are purely software emulations
2. **Input validation**: All MMIO/PIO accesses validate addresses and data
3. **No DMA to host memory**: Device DMA targets guest memory only
4. **Minimal implementation**: Only implement the minimum functionality needed
5. **No passthrough**: Never pass through real host devices to the emulated environment

### 7.2 Device Attack Surface

| Device | Registers | Attack Surface | Risk | Mitigation |
|--------|-----------|---------------|------|------------|
| NS16550 UART | ~12 registers | Very low — simple shift register | Low | Well-understood, minimal state |
| HPET timer | ~32 registers | Low — fixed function timers | Low | Bounds-checked comparator values |
| i8254 PIT | ~4 registers | Very low — simple counter/timer | Low | Well-understood |
| virtio-blk | Queue registers + virtqueues | Medium — descriptor chains | Medium | Validate descriptor chain length and addresses |
| virtio-net | Queue registers + virtqueues | Medium — network packets | Medium | Validate packet size, no raw socket access |
| Floppy controller (FDC) | ~8 registers | Low — simple register interface | Low | Well-understood, limited DMA |
| AHCI (SATA) | ~100+ registers | High — complex register set | Medium | Validate all register writes, limit PRDT entries |

### 7.3 Device Implementation Guidelines

**UART (NS16550):**
```c
int
emu_uart_mmio_write(struct emu_device *dev, uint64_t offset,
                    uint64_t val, int size)
{
    /* Only byte accesses are valid for UART */
    if (size != 1)
        return (EMU_ERR_INVALID_ACCESS_SIZE);

    switch (offset & 0x7) {
    case UART_REG_THR:  /* Transmit holding register */
        /* Write to console buffer — no host file access */
        emu_console_write(dev->instance, (uint8_t)val);
        break;
    case UART_REG_IER:  /* Interrupt enable register */
        /* Only valid bits: 0x0F */
        dev->state.ier = val & 0x0F;
        break;
    /* ... */
    }
    return (0);
}
```

**virtio-blk:**
```c
int
emu_virtio_blk_handle_request(struct emu_device *dev,
                              struct virtio_blk_req *req)
{
    /* Validate request type */
    if (req->type > VIRTIO_BLK_T_FLUSH)
        return (EMU_ERR_INVALID_REQUEST);

    /* Validate sector number */
    if (req->sector >= dev->state->num_sectors)
        return (EMU_ERR_INVALID_SECTOR);

    /* Validate data descriptor chain length */
    if (req->data_len > MAX_IO_SIZE)
        return (EMU_ERR_INVALID_SIZE);

    /* Perform I/O on disk image file (not host block device) */
    /* ... */
}
```

### 7.4 Network Security

Network emulation introduces the most significant attack surface because it connects the emulated environment to external systems.

**Network isolation modes:**

| Mode | Description | Security | Use Case |
|------|-------------|----------|----------|
| **None** | No network device | Maximum isolation | Kernel module testing without network |
| **Host-only** | Virtual network between host and instance | High — no external access | Development, debugging |
| **NAT** | Instance shares host's network via NAT | Medium — outbound only | Package downloads, updates |
| **Bridged** | Instance gets IP on physical network | Low — full network access | Production-like testing |

**Recommended default: Host-only** for kernel module development. NAT can be enabled for package downloads when needed.

**Network security measures:**
- MAC address filtering — only allow traffic from the instance's assigned MAC
- No promiscuous mode — instance cannot see other instances' traffic
- No raw socket access — instance cannot craft arbitrary packets
- Rate limiting — prevent network-based DoS from the instance
- Localhost-only GDB stub — debug interface binds to 127.0.0.1 only

---

## 8. Crash Safety & Recovery

### 8.1 Crash Containment

When an emulated kernel panics or crashes, the crash must be contained:

```
Guest kernel panic
        │
        ▼
Emulator detects crash
  (triple fault, HLT with no interrupt,
   invalid instruction, watchdog timeout)
        │
        ├── Capture register state
        ├── Capture stack trace
        ├── Capture console output
        ├── Save crash dump to /var/emu/<name>/crash/
        └── Terminate emulator process
              │
              ▼
        Instance status → CRASHED
        User can inspect crash dump
        User can destroy and restart
```

**Crash detection mechanisms:**

| Mechanism | Custom Emulator | bhyve Path |
|-----------|----------------|------------|
| Triple fault | Detect repeated #DF → shutdown | VMM reports TRIPLE_FAULT exit reason |
| HLT with no interrupts | Detect HLT with no pending interrupts | VMM reports HLT exit |
| Watchdog timeout | Timer-based watchdog (configurable) | Timer-based watchdog |
| Invalid instruction | Decoder returns error → #UD exception | VMM reports invalid instruction exit |
| Guest shutdown | Detect SHUTDOWN instruction | VMM reports SHUTDOWN exit |

### 8.2 Host Safety During Crash

The host must never be affected by a guest crash:

- **No host kernel panic**: Guest crash cannot trigger host panic
- **No host memory corruption**: Guest memory is isolated in emulator process
- **No filesystem corruption**: Disk image is a regular file, not a host block device
- **Clean recovery**: Emulator process exits cleanly, all resources freed
- **Crash dump isolation**: Crash dumps are stored in instance-specific directory

---

## 9. Security Recommendations Summary

### 9.1 By Component

| Component | Recommendation | Priority |
|-----------|---------------|----------|
| Custom emulator | Run as unprivileged user, no root required | P0 |
| Custom emulator | Bounds-check all guest memory accesses | P0 |
| Custom emulator | Validate all ELF segments before loading | P0 |
| Custom emulator | Instruction decoder must handle all inputs safely | P0 |
| Custom emulator | No dynamic code generation (no JIT initially) | P0 |
| Custom emulator | Capsicum sandboxing after initialization | P1 |
| bhyve path | Drop privileges after VM creation | P0 |
| bhyve path | No passthrough devices for emulation instances | P0 |
| bhyve path | Close unnecessary file descriptors | P1 |
| Filesystem | Validate all share paths with realpath() | P0 |
| Filesystem | Block /dev, /proc, /sys, /etc shares | P0 |
| Filesystem | Read-only shares by default | P0 |
| Filesystem | ZFS snapshots for clean test state | P1 |
| Network | Host-only mode by default | P0 |
| Network | MAC filtering on virtual NICs | P1 |
| Network | GDB stub on localhost only | P0 |
| Devices | Validate all MMIO register writes | P0 |
| Devices | No DMA to host memory | P0 |
| Devices | Minimal device implementations | P0 |
| Instance mgmt | Mutex-protected instance registry | P0 |
| Instance mgmt | Resource limits (memory, CPU time) | P1 |
| Instance mgmt | Clean up all resources on destroy | P0 |

### 9.2 Security Checklist

- [ ] Emulator runs as unprivileged user (no root)
- [ ] All guest memory accesses are bounds-checked
- [ ] ELF loader validates all headers and segments before loading
- [ ] Instruction decoder handles all inputs without crashing
- [ ] No JIT compilation (no WX memory)
- [ ] bhyve process drops privileges after VM creation
- [ ] No passthrough devices for emulation instances
- [ ] Share paths are validated with realpath()
- [ ] Dangerous paths (/dev, /proc, /sys) are blocked
- [ ] Shares are read-only by default
- [ ] Network defaults to host-only mode
- [ ] GDB stub binds to localhost only
- [ ] All MMIO accesses validate offsets and data sizes
- [ ] Device DMA targets guest memory only
- [ ] Instance registry is mutex-protected
- [ ] Instance destroy cleans up all resources
- [ ] Crash detection captures state without affecting host
- [ ] Console buffer is per-instance and size-limited
- [ ] Snapshot files have restricted permissions (0600)
- [ ] Multi-instance isolation is enforced by process boundaries

---

## 10. Implementation Phases — Security & Filesystem

### Phase S1: Core Security Infrastructure

**Objective:** Implement the fundamental security mechanisms for both execution paths.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| S1.1 | Implement bounds-checked memory access in custom emulator | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | All guest memory reads/writes check against `inst->mem_size` |
| S1.2 | Implement ELF loader with validation | NOT STARTED | | | | 5.5 | `usr.sbin/emu/emu_boot.c` | Validate ELF header, program headers, segment bounds |
| S1.3 | Implement safe instruction decoder framework | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Bounds-checked operand reads, instruction length limits |
| S1.4 | Implement bhyve privilege dropping | NOT STARTED | | | | 4.2 | `usr.sbin/emu/emu_bhyve.c` | `setuid()`/`setgid()` after VM creation |
| S1.5 | Implement instance resource limits | NOT STARTED | | | | 2.4 | `sys/emulation/emu_instance.c` | Memory caps, CPU time limits, max instances |
| S1.6 | Implement crash detection and containment | NOT STARTED | | | | 2.14 | `sys/emulation/emu_crash.c` | Detect panics, capture state, clean termination |
| S1.7 | Write security unit tests | NOT STARTED | | | | S1.1–S1.6 | `tests/sys/emulation/security_test.c` | Bounds checking, ELF validation, crash containment |

### Phase S2: Filesystem Sharing

**Objective:** Implement controlled filesystem sharing between host and emulated instances.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| S2.1 | Implement share path validation | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_start.c` | `realpath()`, blocked prefix check, subdirectory check |
| S2.2 | Implement custom emulator file sharing | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Intercept guest open/read/write, translate paths |
| S2.3 | Implement virtio-9p for bhyve path | NOT STARTED | | | | 4.2 | `usr.sbin/bhyve/pci_virtio_9p.c` | PCI transport, 9p protocol handler |
| S2.4 | Add `--share` flag to `emu` CLI | NOT STARTED | | | | S2.1 | `usr.sbin/emu/emu_start.c` | `--share host_path:guest_path:ro` |
| S2.5 | Implement base image management | NOT STARTED | | | | 6.2 | `usr.sbin/emu/emu_init.c` | Download, cache, validate base images |
| S2.6 | Implement ZFS snapshot integration | NOT STARTED | | | | S2.5 | `usr.sbin/emu/emu_zfs.c` | `emu_zfs_snapshot()`, `emu_zfs_rollback()` |
| S2.7 | Write filesystem security tests | NOT STARTED | | | | S2.1–S2.6 | `tests/usr.sbin/emu/fs_security_test.sh` | Path traversal, symlink escape, permission tests |

### Phase S3: Device & Network Security

**Objective:** Implement secure device emulation and network isolation.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| S3.1 | Implement MMIO validation framework | NOT STARTED | | | | 5.4 | `usr.sbin/emu/emu_engine.c` | Validate offset, size, alignment for all MMIO accesses |
| S3.2 | Implement UART with console-only output | NOT STARTED | | | | 5.6 | `usr.sbin/emu/emu_dev_uart.c` | No host file access, output to instance console buffer |
| S3.3 | Implement virtio-blk with file-backed storage | NOT STARTED | | | | 5.9 | `usr.sbin/emu/emu_dev_storage.c` | I/O to disk image file, not host block device |
| S3.4 | Implement host-only networking | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_dev_net.c` | Internal virtual network, no external access |
| S3.5 | Implement NAT networking mode | NOT STARTED | | | | S3.4 | `usr.sbin/emu/emu_dev_net.c` | Outbound-only network access via host NAT |
| S3.6 | Implement GDB stub on localhost only | NOT STARTED | | | | 5.16 | `usr.sbin/emu/emu_gdb.c` | Bind to 127.0.0.1, no external connections |
| S3.7 | Write device security tests | NOT STARTED | | | | S3.1–S3.6 | `tests/usr.sbin/emu/device_security_test.sh` | MMIO bounds, device state corruption, network isolation |

### Phase S4: Hardening & Audit

**Objective:** Harden the emulation framework and perform security audit.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| S4.1 | Implement Capsicum sandboxing for custom emulator | NOT STARTED | | | | S1.1 | `usr.sbin/emu/emu_engine.c` | `cap_enter()` after initialization |
| S4.2 | Implement Capsicum sandboxing for bhyve process | NOT STARTED | | | | S1.4 | `usr.sbin/emu/emu_bhyve.c` | `cap_enter()` after privilege drop |
| S4.3 | Close unnecessary file descriptors in both paths | NOT STARTED | | | | S4.1, S4.2 | `usr.sbin/emu/emu_engine.c`, `emu_bhyve.c` | Keep only essential FDs open |
| S4.4 | Add instruction count limits per execution slice | NOT STARTED | | | | S1.3 | `usr.sbin/emu/emu_engine.c` | Prevent infinite loops in guest code |
| S4.5 | Add watchdog timer for crash detection | NOT STARTED | | | | S1.6 | `usr.sbin/emu/emu_engine.c` | Configurable timeout, trigger crash capture |
| S4.6 | Security audit of all MMIO handlers | NOT STARTED | | | | S3.1 | All device files | Verify bounds checking, input validation |
| S4.7 | Fuzz testing of instruction decoder | NOT STARTED | | | | S1.3 | `tests/sys/emulation/fuzz_test.c` | Random instruction sequences, edge cases |
| S4.8 | Write security documentation | NOT STARTED | | | | S4.1–S4.7 | `share/doc/emulation/security.md` | Threat model, security guidelines, incident response |

---

## 11. Key Data Structures

### 11.1 Filesystem Share Configuration

```c
/* Filesystem share entry */
struct emu_fs_share {
    char host_path[PATH_MAX];   /* Resolved host path */
    char guest_prefix[64];      /* Guest mount point prefix */
    bool readonly;              /* Read-only mount */
    uid_t map_uid;              /* UID mapping (optional) */
    gid_t map_gid;              /* GID mapping (optional) */
    LIST_ENTRY(emu_fs_share) entries;
};

/* Per-instance share list */
struct emu_instance {
    /* ... existing fields ... */
    struct emu_fs_share_list shares; /* List of filesystem shares */
    int nshares;                     /* Number of shares */
};
```

### 11.2 Security Policy

```c
/* Per-instance security policy */
struct emu_security_policy {
    bool allow_network;          /* Enable network device */
    int network_mode;            /* EMU_NET_NONE, _HOSTONLY, _NAT, _BRIDGED */
    bool allow_gdb;              /* Enable GDB stub */
    uint16_t gdb_port;           /* GDB stub port (default: 0 = random) */
    int max_instructions;        /* Max instructions per slice (0 = unlimited) */
    int watchdog_seconds;        /* Watchdog timeout (0 = disabled) */
    bool sandbox_capsicum;       /* Enable Capsicum sandboxing */
    bool drop_privileges;        /* Drop root after setup */
};
```

### 11.3 Crash Dump

```c
/* Crash dump header */
struct emu_crash_dump {
    char magic[8];               /* "EMUCRASH" */
    uint32_t version;            /* Dump format version */
    uint64_t timestamp;          /* Time of crash */
    char arch[32];               /* Target architecture */
    char panic_string[256];      /* Panic message (if available) */
    int nregisters;              /* Number of saved registers */
    int nframes;                 /* Number of stack frames */
    int console_size;            /* Captured console output size */
    /* Followed by: register state, stack frames, console output */
};
```

---

## 12. Sysctl Interface Additions

New sysctl nodes for security configuration:

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.max_instances` | CTLTYPE_INT | 16 | Maximum concurrent emulated instances |
| `kern.emulation.max_memory_per_instance` | CTLTYPE_INT | 4096 | Max MB per instance (0 = unlimited) |
| `kern.emulation.sandbox_capsicum` | CTLTYPE_INT | 1 | Enable Capsicum sandboxing (if available) |
| `kern.emulation.drop_privileges` | CTLTYPE_INT | 1 | Drop root privileges after setup |
| `kern.emulation.watchdog_seconds` | CTLTYPE_INT | 30 | Default watchdog timeout |
| `kern.emulation.blocked_share_paths` | CTLTYPE_STRING | "/dev,/proc,/sys,/etc" | Comma-separated blocked share prefixes |

---

## 13. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Instruction decoder bug allows arbitrary code execution in emulator process | Critical | Bounds checking, no JIT (no WX memory), Capsicum sandboxing |
| ELF loader vulnerability allows buffer overflow | Critical | Validate all headers before loading, bounds-check all segments |
| Symlink in shared directory allows host filesystem access | High | `realpath()` resolution, blocked path prefixes, read-only by default |
| Guest consumes all host memory via emulator | High | Per-instance memory limits, `max_memory_per_instance` sysctl |
| bhyve VMM vulnerability allows guest escape | Critical | Use proven codebase, no passthrough devices, drop privileges |
| Network from emulated instance used for attacks | Medium | Host-only mode by default, MAC filtering, rate limiting |
| Snapshot file contains sensitive guest data | Medium | 0600 permissions, instance-specific directories, cleanup on destroy |
| Race condition in instance registry | Medium | Mutex protection, atomic operations |
| GDB stub allows arbitrary memory access | High | Localhost-only binding, authentication (future) |
| Crash dump contains host memory data | Low | Crash dumps only contain guest state, not emulator state |

---

## 14. Future Security Enhancements

1. **Capsicum sandboxing**: Full capability mode for both emulator and bhyve processes
2. **Seccomp-like syscall filtering**: Restrict syscalls available to the emulator process
3. **Address space layout randomization (ASLR)**: Randomize emulator memory layout
4. **Instruction decoder fuzzing**: Automated fuzz testing of all instruction decoders
5. **Kernel module signing**: Require signed modules before loading into emulated environment
6. **Audit logging**: Log all security-relevant events (instance create/destroy, share mounts, crashes)
7. **Network traffic inspection**: Inspect guest network traffic for malicious patterns
8. **Resource accounting**: Track CPU time, memory, disk I/O per instance
9. **Secure snapshot encryption**: Encrypt snapshot files at rest
10. **Multi-tenant isolation**: Stronger isolation for CI/CD environments with untrusted workloads

---

## 15. Task Completion Checklist

- [ ] Bounds-checked memory access implemented in custom emulator
- [ ] ELF loader with full validation implemented
- [ ] Safe instruction decoder with bounds checking implemented
- [ ] bhyve privilege dropping implemented
- [ ] Instance resource limits implemented
- [ ] Crash detection and containment implemented
- [ ] Share path validation with realpath() implemented
- [ ] Dangerous paths (/dev, /proc, /sys) blocked
- [ ] Read-only shares by default
- [ ] Custom emulator file sharing implemented
- [ ] virtio-9p for bhyve path implemented
- [ ] Base image management implemented
- [ ] ZFS snapshot integration implemented
- [ ] MMIO validation framework implemented
- [ ] Host-only networking implemented
- [ ] GDB stub on localhost only
- [ ] Capsicum sandboxing implemented
- [ ] File descriptor cleanup implemented
- [ ] Instruction count limits implemented
- [ ] Watchdog timer implemented
- [ ] Security unit tests written and passing
- [ ] Filesystem security tests written and passing
- [ ] Device security tests written and passing
- [ ] Fuzz testing of instruction decoder
- [ ] Security documentation written

---

## 16. Conclusion

The emulation framework's security architecture is built on three layers of defense:

1. **OS-level isolation**: Process boundaries, user privileges, file permissions, and (optionally) Capsicum sandboxing ensure that even if the emulator is compromised, the attacker gains only the privileges of an unprivileged user.

2. **Emulator-level isolation**: Bounds-checked memory access, validated ELF loading, safe instruction decoding, and input-validated device emulation prevent most attacks from succeeding within the emulator itself.

3. **Filesystem and network controls**: Controlled sharing with path validation, read-only defaults, blocked dangerous paths, and host-only networking prevent the emulated environment from accessing sensitive host resources.

The key insight is that **the custom emulator path is actually more secure than bhyve for untrusted workloads** because:
- It requires no kernel module
- It runs entirely in userland with no special privileges
- It has a much smaller codebase than bhyve
- It can be Capsicum-sandboxed
- It has no hardware passthrough capability

For trusted workloads where performance matters, the bhyve path provides hardware-enforced isolation with EPT/NPT and IOMMU protection, backed by a mature, well-audited codebase.
