# Emulation Framework Security, Filesystem & Device Integration — Implementation Plan

## 1. Executive Summary

This document provides the security architecture, threat model, access control model, filesystem strategy, and device security for the kernel emulation framework described in `001-Emulation-Overview.md`. It covers both execution paths:

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
| Access control | devfs permissions + privilege checks | devfs permissions + privilege checks + granular per-op ACL |

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
- **Capsicum sandboxing**: The emulator process is further restricted using FreeBSD's Capsicum capability mode (`cap_enter()`) to drop privileges after initialization. See Section 6.5 for full implementation details.

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

/* Capsicum sandboxing — see Section 6.5 for full implementation */
    emu_bhyve_enter_sandbox(inst);
```

---

## 5. Access Control & Authorization

### 5.1 Design Principles

The emulation framework's access control model follows these principles:

1. **Root-only by default**: Only root can create or manage emulation instances unless explicitly enabled
2. **Group-based delegation**: The `emu` group (GID_EMU) provides controlled delegation to non-root users
3. **Sysctl-gated enablement**: A master sysctl (`kern.emulation.allow_nonroot`) controls whether non-root access is permitted
4. **Ownership model**: Every instance has an owning user (ucred), tracked at creation time
5. **Granular permissions**: Different operations require different permission levels — a user may start/stop an instance they don't own but cannot destroy it
6. **Resource quotas**: Per-user and per-group limits prevent resource exhaustion
7. **Jail-aware**: The framework integrates with FreeBSD's jail system, requiring explicit jail permission (`pr_allow_emu_flag`)

### 5.2 Reference: bhyve/VMM Permission Model

The bhyve/VMM codebase provides the reference pattern for our access control:

| Mechanism | bhyve/VMM | Emulation Framework |
|-----------|-----------|---------------------|
| Group | `GID_VMM` (978) | `GID_EMU` (979) |
| Control device | `/dev/vmmctl` (0660, root:vm) | `/dev/emuctl` (0660, root:emu) |
| Per-instance device | `/dev/vmm/<name>` (0600, owner:vm) | `/dev/emu/<name>` (0600, owner:emu) |
| Create privilege | `PRIV_VMM_CREATE` (711) | `PRIV_EMU_CREATE` (720) |
| Destroy privilege | `PRIV_VMM_DESTROY` (712) | `PRIV_EMU_DESTROY` (721) |
| Passthrough privilege | `PRIV_VMM_PPTDEV` (710) | N/A (no passthrough) |
| Per-user limit | `hw.vmm.maxvmms` sysctl | `kern.emulation.max_instances_per_user` sysctl |
| Jail integration | `pr_allow_vmm_flag` | `pr_allow_emu_flag` |
| Cross-user visibility | `cr_cansee()` check in `vmmdev_lookup()` | `cr_cansee()` check in instance lookup |
| Credential tracking | `struct ucred *ucred` in `vmmdev_softc` | `struct ucred *owner` in `emu_instance` |

### 5.3 Group Configuration

A new system group `emu` is required:

```c
/* In sys/sys/conf.h */
#define	GID_EMU		979	/* Emulation framework group */
```

**Group membership effects:**
- Members of the `emu` group can create and manage emulation instances (when `kern.emulation.allow_nonroot=1`)
- Non-members cannot access the emulation framework at all (unless root)
- The group provides a clean delegation boundary: add users to `emu` to grant emulation access

**Device node permissions:**
```
crw-rw----  root  emu  /dev/emuctl     (control device, 0660)
crw-------  owner emu  /dev/emu/<name>  (per-instance, 0600)
```

### 5.4 Ownership Model

Every emulation instance has an owning user, tracked at creation time:

```c
struct emu_instance {
    /* ... existing fields ... */
    struct ucred *owner;        /* Credentials of creating user */
    uid_t owner_uid;            /* Owner UID (cached for fast checks) */
    gid_t owner_gid;            /* Owner GID (cached) */
    int permissions;            /* Custom permission mask (optional) */
};
```

**Ownership rules:**
- The creating user becomes the instance owner
- Ownership is recorded in the instance's ucred at creation time
- Ownership cannot be transferred (initially — future enhancement)
- Root can operate on any instance regardless of ownership
- The `cr_cansee()` function controls cross-user instance visibility

### 5.5 Granular Permission Levels

The framework supports two tiers of permissions:

#### Macro Levels

| Level | Description | Who |
|-------|-------------|-----|
| **admin** | Full control: create, destroy, modify any instance | root only |
| **operator** | Manage all instances: start, stop, console, stack, snapshot | root, emu group members |
| **user** | Manage own instances only | instance owner |

#### Micro Permissions (Per-Operation)

Each operation on an instance requires a specific permission:

| Permission | Description | Default Grant |
|------------|-------------|---------------|
| `EMU_PERM_CREATE` | Create new instances | root, emu group (if allowed) |
| `EMU_PERM_DESTROY` | Destroy instances | root, instance owner |
| `EMU_PERM_START` | Start an instance | root, emu group, instance owner |
| `EMU_PERM_STOP` | Stop an instance | root, emu group, instance owner |
| `EMU_PERM_MODIFY` | Modify instance configuration | root, instance owner |
| `EMU_PERM_CONSOLE` | Read console output | root, emu group, instance owner |
| `EMU_PERM_STACK` | Capture/read stack traces | root, emu group, instance owner |
| `EMU_PERM_SNAPSHOT` | Create/restore snapshots | root, instance owner |
| `EMU_PERM_SHARE` | Configure filesystem shares | root, instance owner |
| `EMU_PERM_NETWORK` | Configure network access | root, instance owner |
| `EMU_PERM_LIST` | List all instances | root, emu group |

### 5.6 Permission Matrix

| Operation | Root | emu Group (owner) | emu Group (non-owner) | Regular User (owner) | Regular User (non-owner) |
|-----------|------|-------------------|----------------------|---------------------|-------------------------|
| Create instance | ✅ | ✅ (if allowed) | ✅ (if allowed) | ❌ | ❌ |
| Destroy own instance | ✅ | ✅ | ❌ | ✅ | ❌ |
| Destroy any instance | ✅ | ❌ | ❌ | ❌ | ❌ |
| Start own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Start any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Stop own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Stop any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Console own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Console any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Stack own instance | ✅ | ✅ | ✅ | ✅ | ❌ |
| Stack any instance | ✅ | ✅ | ✅ | ❌ | ❌ |
| Modify own config | ✅ | ✅ | ❌ | ✅ | ❌ |
| Snapshot own instance | ✅ | ✅ | ❌ | ✅ | ❌ |
| List all instances | ✅ | ✅ | ✅ | ❌ | ❌ |
| List own instances | ✅ | ✅ | ✅ | ✅ | ✅ |

### 5.7 Privilege Definitions

New kernel privileges for the emulation framework:

```c
/* In sys/sys/priv.h */

/*
 * Emulation framework privileges.
 */
#define	PRIV_EMU_CREATE		720	/* Can create emulation instances. */
#define	PRIV_EMU_DESTROY	721	/* Can destroy other users' instances. */
#define	PRIV_EMU_MODIFY		722	/* Can modify other users' instance config. */
#define	PRIV_EMU_ADMIN		723	/* Full emulation administrative access. */
```

**Privilege check flow:**
```
Operation requested
    │
    ├── Is the caller root? → GRANT
    ├── Is the caller the instance owner?
    │       ├── Does the operation require owner? → GRANT
    │       └── Does the operation allow emu group? → check group membership
    ├── Is the caller in the emu group?
    │       ├── Is kern.emulation.allow_nonroot enabled? → check per-op permission
    │       └── DENY
    └── DENY
```

### 5.8 Sysctl Interface for Access Control

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.allow_nonroot` | CTLTYPE_INT | 0 | Allow non-root users to create/manage instances |
| `kern.emulation.required_group` | CTLTYPE_INT | 979 (GID_EMU) | GID required for non-root access (0 = any group) |
| `kern.emulation.max_instances_per_user` | CTLTYPE_INT | 4 | Maximum instances per non-root user |
| `kern.emulation.max_instances_per_group` | CTLTYPE_INT | 16 | Maximum instances per group |
| `kern.emulation.max_memory_per_user` | CTLTYPE_INT | 4096 | Max total MB per non-root user (0 = unlimited) |
| `kern.emulation.destroy_others` | CTLTYPE_INT | 0 | Allow emu group members to destroy any instance |
| `kern.emulation.jail_allow_vmm` | CTLTYPE_INT | 0 | Allow emulation in jails (requires jail permission) |

### 5.9 Jail Integration

Following the bhyve pattern, the emulation framework integrates with FreeBSD's jail system:

```c
/* Jail allow flag for emulation */
pr_allow_emu_flag = prison_add_allow(NULL, "emu", NULL, 0);
```

**Jail permission check:**
```c
static int
emu_jail_priv_check(struct ucred *ucred)
{
    if (jailed(ucred) &&
        (ucred->cr_prison->pr_allow & pr_allow_emu_flag) == 0)
        return (EPERM);

    return (0);
}
```

**Jail configuration:**
```
# In jail.conf:
allow.emu;
```

### 5.10 Resource Limits

Per-user and per-group resource limits prevent abuse:

```c
/* Per-user resource tracking */
struct emu_user_limits {
    uid_t uid;                          /* User ID */
    int instance_count;                 /* Current instance count */
    uint64_t total_memory_mb;           /* Current total memory usage */
    uint64_t total_cpu_seconds;         /* Accumulated CPU time */
    LIST_ENTRY(emu_user_limits) entries; /* Hash table linkage */
};

/* Per-group resource tracking */
struct emu_group_limits {
    gid_t gid;                          /* Group ID */
    int instance_count;                 /* Current instance count */
    LIST_ENTRY(emu_group_limits) entries;
};
```

**Enforcement points:**
- On `create`: check `instance_count < max_instances_per_user` and `total_memory_mb + new_memory < max_memory_per_user`
- On `start`: check group instance count
- On `modify` (increase memory): check new total against limit
- On `destroy`/`stop`: decrement counters

### 5.11 Permission Check Implementation

```c
/* Unified permission check for all operations */
static int
emu_check_perm(struct emu_instance *inst, struct ucred *cred, int operation)
{
    int error;

    /* Root can do anything */
    if (priv_check_cred(cred, PRIV_EMU_ADMIN) == 0)
        return (0);

    /* Check jail permissions */
    error = emu_jail_priv_check(cred);
    if (error != 0)
        return (error);

    /* Check if non-root access is allowed at all */
    if (allow_nonroot == 0)
        return (EPERM);

    /* Check group membership */
    if (required_group > 0 && !group_member(cred, required_group))
        return (EPERM);

    /* Check per-user instance limit (for create) */
    if (operation == EMU_PERM_CREATE) {
        if (emu_user_instance_count(cred->cr_uid) >= max_instances_per_user)
            return (EMU_ERR_MAX_INSTANCES);
    }

    /* If instance exists, check ownership */
    if (inst != NULL) {
        /* Owner can do most operations */
        if (cred->cr_uid == inst->owner_uid)
            return (0);

        /* Non-owner in emu group: check specific operation */
        switch (operation) {
        case EMU_PERM_START:
        case EMU_PERM_STOP:
        case EMU_PERM_CONSOLE:
        case EMU_PERM_STACK:
            /* emu group members can start/stop/console/stack any instance */
            return (0);
        case EMU_PERM_DESTROY:
            /* Only if sysctl allows */
            if (destroy_others)
                return (0);
            return (EPERM);
        default:
            return (EPERM);
        }
    }

    return (0);
}
```

### 5.12 Data Structures

```c
/* Per-instance security context */
struct emu_security_ctx {
    struct ucred *owner;            /* Creating user's credentials */
    uid_t owner_uid;                /* Cached owner UID */
    gid_t owner_gid;                /* Cached owner GID */
    int perm_mask;                  /* Custom permission mask (future) */
};

/* Global access control configuration */
struct emu_access_config {
    int allow_nonroot;              /* Master switch for non-root access */
    gid_t required_group;           /* Required group GID (0 = any) */
    int max_instances_per_user;     /* Per-user instance limit */
    int max_instances_per_group;    /* Per-group instance limit */
    int max_memory_per_user;        /* Per-user memory limit (MB) */
    int destroy_others;             /* Allow emu group to destroy any instance */
};
```

---

## 6. Custom Emulator Security

### 6.1 Attack Surface Analysis

The custom emulator has a smaller but more critical attack surface than bhyve:

| Component | Attack Surface | Risk | Mitigation |
|-----------|---------------|------|------------|
| Instruction decoder | Parses arbitrary guest code | Critical — buffer overflow, infinite loop | Bounds checking, operand validation, instruction limit |
| Memory emulation | Translates guest virtual addresses | High — out-of-bounds access | Bounds checking on all memory operations |
| MMU emulation | Walks guest page tables | High — infinite loop, invalid entries | Page table depth limit, valid entry checks |
| Device emulation | Handles MMIO reads/writes | Medium — device state corruption | Input validation on all MMIO operations |
| ELF loader | Parses kernel/module ELF files | High — buffer overflow | ELF validation, section bounds checking |
| GDB stub | Accepts remote debugger connections | Medium — arbitrary memory read/write | Authentication, localhost-only binding |

### 6.2 Instruction Decoder Safety

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

### 6.3 Memory Safety

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

### 6.4 ELF Loader Safety

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

### 6.5 Capsicum Sandboxing

FreeBSD's Capsicum capability framework provides fine-grained rights restriction on file descriptors and process capabilities. The emulation framework implements Capsicum sandboxing as a **first-class security feature**, not a future enhancement. Both the custom emulator and bhyve paths enter capability mode after initialization is complete.

#### 6.5.1 Architecture Overview

```
Process Startup
      │
      ├── Parse config, open files, allocate memory
      ├── Load kernel/module into guest memory
      ├── Set up devices, console, network
      ├── Open disk image, snapshot, log files
      ├── Bind GDB socket (if enabled)
      │
      ▼
  ┌─────────────────────────────────────────────┐
  │         CAPSICUM ENTER POINT                 │
  │                                              │
  │  1. Limit rights on all open file descriptors│
  │  2. Call cap_enter() to enter capability mode│
  │  3. After cap_enter(): no new capabilities,  │
  │     no new FDs, no /proc, no sysctl, no fork │
  └─────────────────────────────────────────────┘
      │
      ▼
  ┌─────────────────────────────────────────────┐
  │         EMULATION RUN LOOP                   │
  │  (fetch-decode-execute under Capsicum)       │
  │                                              │
  │  Allowed operations only:                    │
  │  - Read/write/seek on disk image FD          │
  │  - Read/write on console pipe FD             │
  │  - Read/write/accept on GDB socket FD        │
  │  - mmap/mprotect/munmap on existing mappings │
  │  - clock_gettime (for timers)                │
  └─────────────────────────────────────────────┘
```

#### 6.5.2 Custom Emulator Capsicum Implementation

The custom emulator (`usr.sbin/emu/emu_engine.c`) enters capability mode after all initialization is complete:

```c
#include <sys/capsicum.h>

/*
 * Enter Capsicum capability mode for the custom emulator process.
 * Called after all initialization is complete (config parsed,
 * kernel loaded, devices set up, files opened).
 *
 * Returns 0 on success, -1 on failure (process continues without
 * sandbox in degraded mode).
 */
int
emu_enter_sandbox(struct emu_instance *inst)
{
    cap_rights_t rights;

    /* ── Step 1: Limit rights on disk image FD ── */
    /* Allow: read, write, seek. Deny: exec, ioctl, fcntl, etc. */
    cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_SEEK);
    if (cap_rights_limit(inst->disk_fd, &rights) < 0 && errno != ENOSYS) {
        warn("cap_rights_limit(disk_fd) failed");
        return (-1);
    }

    /* ── Step 2: Limit rights on console pipe FD ── */
    cap_rights_init(&rights, CAP_READ, CAP_WRITE);
    if (cap_rights_limit(inst->console_fd, &rights) < 0 && errno != ENOSYS) {
        warn("cap_rights_limit(console_fd) failed");
        return (-1);
    }

    /* ── Step 3: Limit rights on GDB socket FD (if enabled) ── */
    if (inst->gdb_fd >= 0) {
        cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_ACCEPT);
        if (cap_rights_limit(inst->gdb_fd, &rights) < 0 && errno != ENOSYS) {
            warn("cap_rights_limit(gdb_fd) failed");
            return (-1);
        }
    }

    /* ── Step 4: Limit rights on snapshot/log FDs (if open) ── */
    if (inst->snapshot_fd >= 0) {
        cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_SEEK);
        if (cap_rights_limit(inst->snapshot_fd, &rights) < 0 && errno != ENOSYS) {
            warn("cap_rights_limit(snapshot_fd) failed");
            return (-1);
        }
    }

    /* ── Step 5: Limit ioctls on any control FDs ── */
    /* No ioctls should be needed after initialization */

    /* ── Step 6: Close all non-essential FDs ── */
    for (int fd = 3; fd < getdtablesize(); fd++) {
        if (!emu_is_essential_fd(inst, fd))
            close(fd);
    }

    /* ── Step 7: Enter capability mode ── */
    if (cap_enter() < 0) {
        /* ENOSYS means Capsicum not available in this kernel */
        if (errno != ENOSYS) {
            warn("cap_enter() failed");
            return (-1);
        }
        /* Capsicum not available — continue without sandbox */
        warnx("Capsicum not available — running without sandbox");
        return (0);
    }

    /*
     * After cap_enter():
     * - No new capabilities can be acquired
     * - No new file descriptors can be opened
     * - No access to /proc, /dev, or global namespaces
     * - No fork(), no sysctl()
     * - Only operations on already-open FDs with limited rights
     */

    inst->sandboxed = true;
    return (0);
}

/* Helper: determine if a file descriptor is essential for emulation */
static bool
emu_is_essential_fd(struct emu_instance *inst, int fd)
{
    return (fd == inst->disk_fd ||
            fd == inst->console_fd ||
            fd == inst->gdb_fd ||
            fd == inst->snapshot_fd ||
            fd == STDIN_FILENO ||
            fd == STDOUT_FILENO ||
            fd == STDERR_FILENO);
}
```

#### 6.5.3 bhyve Path Capsicum Implementation

The bhyve path (`usr.sbin/emu/emu_bhyve.c`) enters capability mode after VM creation, device setup, and privilege drop:

```c
int
emu_bhyve_enter_sandbox(struct emu_instance *inst)
{
    cap_rights_t rights;

    /* ── Step 1: Limit rights on /dev/vmm/<name> FD ── */
    /* Allow: read, write, ioctl (for VMM operations), mmap (for guest memory) */
    cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_IOCTL, CAP_MMAP);
    if (cap_rights_limit(inst->vmm_fd, &rights) < 0 && errno != ENOSYS) {
        warn("cap_rights_limit(vmm_fd) failed");
        return (-1);
    }

    /* Restrict ioctls to only VMM-related ones */
    unsigned long vmm_ioctls[] = {
        VM_RUN, VM_SUSPEND, VM_REINIT, VM_STATS,
        VM_SET_CAPABILITY, VM_GET_CAPABILITY,
        VM_SET_REGISTER_SET, VM_GET_REGISTER_SET,
        VM_SET_MEMSEG, VM_GET_MEMSEG,
        VM_IOMMU_MAP, VM_IOMMU_UNMAP,
        VM_PPTDEV_MSI, VM_PPTDEV_MSIX,
        VM_GET_VCPU_COUNT, VM_GET_DEVICE_COUNT,
        VM_GET_DEVICE_INFO, VM_GET_MEMORY_SIZE,
        VM_GET_TOPOLOGY, VM_SET_TOPOLOGY,
    };
    if (cap_ioctls_limit(inst->vmm_fd, vmm_ioctls,
                         nitems(vmm_ioctls)) < 0 && errno != ENOSYS) {
        warn("cap_ioctls_limit(vmm_fd) failed");
        return (-1);
    }

    /* ── Step 2: Limit rights on disk image FD ── */
    cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_SEEK);
    if (cap_rights_limit(inst->disk_fd, &rights) < 0 && errno != ENOSYS) {
        warn("cap_rights_limit(disk_fd) failed");
        return (-1);
    }

    /* ── Step 3: Limit rights on console pipe FD ── */
    cap_rights_init(&rights, CAP_READ, CAP_WRITE);
    if (cap_rights_limit(inst->console_fd, &rights) < 0 && errno != ENOSYS) {
        warn("cap_rights_limit(console_fd) failed");
        return (-1);
    }

    /* ── Step 4: Close all non-essential FDs ── */
    for (int fd = 3; fd < getdtablesize(); fd++) {
        if (!emu_bhyve_is_essential_fd(inst, fd))
            close(fd);
    }

    /* ── Step 5: Enter capability mode ── */
    if (cap_enter() < 0) {
        if (errno != ENOSYS) {
            warn("cap_enter() failed");
            return (-1);
        }
        warnx("Capsicum not available — running without sandbox");
        return (0);
    }

    inst->sandboxed = true;
    return (0);
}
```

#### 6.5.4 Rights Inventory

Each file descriptor type has a specific set of allowed capabilities:

| FD Type | Allowed Rights | Rationale |
|---------|---------------|-----------|
| Disk image | `CAP_READ`, `CAP_WRITE`, `CAP_SEEK` | Block-level I/O for guest storage |
| Console pipe | `CAP_READ`, `CAP_WRITE` | Serial console I/O |
| GDB socket | `CAP_READ`, `CAP_WRITE`, `CAP_ACCEPT` | Remote debugging connections |
| Snapshot file | `CAP_READ`, `CAP_WRITE`, `CAP_SEEK` | Save/restore emulator state |
| `/dev/vmm/<name>` | `CAP_READ`, `CAP_WRITE`, `CAP_IOCTL`, `CAP_MMAP` | VMM control (bhyve path only) |
| stdin/stdout/stderr | `CAP_READ`, `CAP_WRITE` | Process I/O |
| Log file | `CAP_READ`, `CAP_WRITE`, `CAP_SEEK` | Audit logging |

#### 6.5.5 Error Handling & Degraded Mode

Capsicum availability varies across FreeBSD versions and kernel configurations:

| Scenario | Behavior | Log Level |
|----------|----------|-----------|
| `cap_enter()` succeeds | Full sandbox active | INFO |
| `cap_enter()` returns ENOSYS | Capsicum not compiled into kernel — continue without sandbox | WARNING |
| `cap_rights_limit()` returns ENOSYS | Old kernel without Capsicum support — continue without sandbox | WARNING |
| `cap_rights_limit()` returns EINVAL | Invalid rights combination — log error, continue without sandbox for that FD | ERROR |
| `cap_enter()` returns EPERM | Already in capability mode or other restriction — log error, continue without sandbox | ERROR |

The emulator **always continues running** even if Capsicum setup fails, operating in degraded mode. This ensures the emulation framework works on all FreeBSD systems regardless of Capsicum support.

#### 6.5.6 Sysctl Controls

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.sandbox_capsicum` | CTLTYPE_INT | 1 | Enable Capsicum sandboxing (0=disable, 1=enable). When disabled, the emulator skips `cap_enter()` and `cap_rights_limit()` calls entirely. |
| `kern.emulation.sandbox_strict` | CTLTYPE_INT | 0 | Strict mode: if Capsicum setup fails, refuse to start the instance (0=degraded mode, 1=strict). In strict mode, any Capsicum failure prevents instance startup. |

#### 6.5.7 Testing Capsicum Sandboxing

```c
/* Test: Verify cap_enter() succeeds */
static int
test_capsicum_enter(void)
{
    /* Fork a child process to test Capsicum */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: attempt to enter capability mode */
        if (cap_enter() < 0)
            _exit(1);

        /* After cap_enter(), opening a new file should fail */
        int fd = open("/etc/passwd", O_RDONLY);
        if (fd >= 0)
            _exit(2);  /* Should have failed! */

        _exit(0);  /* Success */
    }

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* Test: Verify rights limitation works */
static int
test_capsicum_rights_limit(void)
{
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: limit rights on pipe write end to CAP_WRITE only */
        cap_rights_t rights;
        cap_rights_init(&rights, CAP_WRITE);
        cap_rights_limit(pipefd[1], &rights);

        /* Reading from the write-only FD should fail */
        char buf[16];
        ssize_t n = read(pipefd[1], buf, sizeof(buf));
        if (n >= 0)
            _exit(1);  /* Should have failed! */

        /* Writing should succeed */
        n = write(pipefd[1], "test", 4);
        if (n < 0)
            _exit(2);  /* Should have succeeded! */

        _exit(0);
    }

    int status;
    waitpid(pid, &status, 0);
    close(pipefd[0]);
    close(pipefd[1]);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* Test: Verify emulator runs correctly under Capsicum */
static int
test_emulator_runs_under_capsicum(void)
{
    /* Start an emulator instance with sandboxing enabled */
    /* Verify it enters capability mode and runs normally */
    /* Verify it can still read/write disk, console, etc. */
    /* Verify it cannot open new files */
    return (0);  /* Placeholder — full test in integration suite */
}
```

#### 6.5.8 What Capsicum Prevents

After entering capability mode, the emulator process is restricted from:

| Operation | Before Capsicum | After Capsicum | Impact |
|-----------|----------------|----------------|--------|
| Open new files | ✅ Allowed | ❌ Denied | Prevents filesystem escape via guest-triggered file open |
| Access `/proc` | ✅ Allowed | ❌ Denied | Prevents process information leakage |
| Access `/dev` | ✅ Allowed | ❌ Denied | Prevents device node access |
| `sysctl()` calls | ✅ Allowed | ❌ Denied | Prevents kernel parameter modification |
| `fork()` / `exec()` | ✅ Allowed | ❌ Denied | Prevents process injection |
| Network sockets (new) | ✅ Allowed | ❌ Denied | Prevents lateral movement (existing socket OK) |
| `mmap()` with new rights | ✅ Allowed | ❌ Denied | Prevents memory manipulation |
| `ioctl()` on restricted FD | ✅ Allowed | ❌ Denied | Only allowed ioctls pass through |
| Read/write disk image | ✅ Allowed | ✅ Allowed | Essential for emulation |
| Read/write console | ✅ Allowed | ✅ Allowed | Essential for console I/O |
| `clock_gettime()` | ✅ Allowed | ✅ Allowed | Essential for timer emulation |
| Signal handling | ✅ Allowed | ✅ Allowed | Essential for crash detection |

---

## 7. Filesystem Strategy

### 7.1 Architecture for Kernel Module Development

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

### 7.2 Base Image Approach

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

### 7.3 Source Code Sharing

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

### 7.4 Filesystem Security Rules

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

### 7.5 ZFS Integration for Clean Test State

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

## 8. Device Emulation Security

### 8.1 Device Model Principles

All devices in the emulation framework follow these security principles:

1. **No direct hardware access**: All devices are purely software emulations
2. **Input validation**: All MMIO/PIO accesses validate addresses and data
3. **No DMA to host memory**: Device DMA targets guest memory only
4. **Minimal implementation**: Only implement the minimum functionality needed
5. **No passthrough**: Never pass through real host devices to the emulated environment

### 8.2 Device Attack Surface

| Device | Registers | Attack Surface | Risk | Mitigation |
|--------|-----------|---------------|------|------------|
| NS16550 UART | ~12 registers | Very low — simple shift register | Low | Well-understood, minimal state |
| HPET timer | ~32 registers | Low — fixed function timers | Low | Bounds-checked comparator values |
| i8254 PIT | ~4 registers | Very low — simple counter/timer | Low | Well-understood |
| virtio-blk | Queue registers + virtqueues | Medium — descriptor chains | Medium | Validate descriptor chain length and addresses |
| virtio-net | Queue registers + virtqueues | Medium — network packets | Medium | Validate packet size, no raw socket access |
| Floppy controller (FDC) | ~8 registers | Low — simple register interface | Low | Well-understood, limited DMA |
| AHCI (SATA) | ~100+ registers | High — complex register set | Medium | Validate all register writes, limit PRDT entries |

### 8.3 Device Implementation Guidelines

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

### 8.4 Network Security

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

## 9. Crash Safety & Recovery

### 9.1 Crash Containment

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

### 9.2 Host Safety During Crash

The host must never be affected by a guest crash:

- **No host kernel panic**: Guest crash cannot trigger host panic
- **No host memory corruption**: Guest memory is isolated in emulator process
- **No filesystem corruption**: Disk image is a regular file, not a host block device
- **Clean recovery**: Emulator process exits cleanly, all resources freed
- **Crash dump isolation**: Crash dumps are stored in instance-specific directory

---

## 10. Security Recommendations Summary

### 10.1 By Component

| Component | Recommendation | Priority |
|-----------|---------------|----------|
| Access control | Root-only by default; sysctl-gated non-root access | P0 |
| Access control | `emu` system group (GID_EMU) for delegation | P0 |
| Access control | Per-instance ownership via ucred | P0 |
| Access control | Granular permissions per operation (create/destroy/start/stop/etc.) | P0 |
| Access control | Per-user instance and memory limits | P0 |
| Access control | Jail integration with `pr_allow_emu_flag` | P1 |
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
| Memory mgmt | Demand-paged guest memory (mmap MAP_NORESERVE) | P0 |
| Memory mgmt | `memory_policy` sysctl (prealloc/demand/balloon) | P0 |
| Memory mgmt | `memory_overcommit` sysctl with safeguards | P0 |
| Memory mgmt | `memory_warn_percent` threshold warning | P0 |
| Memory mgmt | Per-instance `memory_used` tracking | P0 |
| Memory mgmt | virtio-balloon device for bhyve path | P1 |
| Memory mgmt | `memory_balloon_min_pct` floor protection | P1 |
| Memory mgmt | Host memory capacity check on instance start (total minus system-wide used (OS + other processes) minus already-consumed by other instances minus safety margin) | P0 |
| Memory mgmt | Already-consumed memory tracking across all instances | P0 |
| Memory mgmt | System-wide memory consumption tracking via `vm.stats.vm.*` sysctls | P0 |
| Memory mgmt | `memory_system_reserve_percent` sysctl for OS/process reserve | P0 |

### 10.2 Security Checklist

> **Note for agents:** When picking up a task, fill in the **Assigned To** column with your agent name/ID. When completing a task, update the **Status** column to `COMPLETED` and add your name/ID to the **Assigned To** column if not already filled. This ensures traceability across sessions.

| # | Item | Category | Status | Assigned To | Dependencies | Files | Notes |
|---|------|----------|--------|------------|--------------|-------|-------|
| SC.1 | `kern.emulation.allow_nonroot` sysctl implemented (default 0) | Access Control | NOT STARTED | | S2.3 | `sys/emulation/emu_sysctl.c` | Master switch for non-root access |
| SC.2 | `GID_EMU` (979) added to `sys/sys/conf.h` | Access Control | NOT STARTED | | | `sys/sys/conf.h` | New group for emulation delegation |
| SC.3 | `PRIV_EMU_CREATE/DESTROY/MODIFY/ADMIN` added to `sys/sys/priv.h` | Access Control | NOT STARTED | | | `sys/sys/priv.h` | Kernel privilege definitions |
| SC.4 | Per-instance ownership (ucred) implemented | Access Control | NOT STARTED | | SC.1 | `sys/emulation/emu_instance.c` | Track creating user's credentials |
| SC.5 | Granular permission checks on all operations | Access Control | NOT STARTED | | SC.4 | `sys/emulation/emu_instance.c` | `emu_check_perm()` for all ops |
| SC.6 | Per-user instance limit enforced | Access Control | NOT STARTED | | SC.5 | `sys/emulation/emu_instance.c` | `max_instances_per_user` sysctl |
| SC.7 | Per-user memory limit enforced | Access Control | NOT STARTED | | SC.5 | `sys/emulation/emu_instance.c` | `max_memory_per_user` sysctl |
| SC.8 | Jail integration with `pr_allow_emu_flag` | Access Control | NOT STARTED | | SC.1 | `sys/emulation/emu_sysctl.c` | `emu_jail_priv_check()` |
| SC.9 | `cr_cansee()` check on instance lookup | Access Control | NOT STARTED | | SC.4 | `sys/emulation/emu_instance.c` | Cross-user instance visibility |
| SC.10 | Emulator runs as unprivileged user (no root) | Custom Emulator | NOT STARTED | | S1.1 | `usr.sbin/emu/emu_engine.c` | No special privileges needed |
| SC.11 | All guest memory accesses are bounds-checked | Custom Emulator | NOT STARTED | | S1.1 | `usr.sbin/emu/emu_engine.c` | Check against `inst->mem_size` |
| SC.12 | ELF loader validates all headers and segments before loading | Custom Emulator | NOT STARTED | | S1.2 | `usr.sbin/emu/emu_boot.c` | Validate ELF header, program headers |
| SC.13 | Instruction decoder handles all inputs without crashing | Custom Emulator | NOT STARTED | | S1.3 | `usr.sbin/emu/emu_engine.c` | Bounds-checked operand reads |
| SC.14 | No JIT compilation (no WX memory) | Custom Emulator | NOT STARTED | | S1.3 | `usr.sbin/emu/emu_engine.c` | No dynamic code generation |
| SC.15 | bhyve process drops privileges after VM creation | bhyve | NOT STARTED | | S1.4 | `usr.sbin/emu/emu_bhyve.c` | `setuid()`/`setgid()` after VM setup |
| SC.16 | No passthrough devices for emulation instances | bhyve | NOT STARTED | | S1.4 | `usr.sbin/emu/emu_bhyve.c` | All devices emulated |
| SC.17 | Share paths are validated with realpath() | Filesystem | NOT STARTED | | S3.1 | `usr.sbin/emu/emu_start.c` | Resolve symlinks, check prefixes |
| SC.18 | Dangerous paths (/dev, /proc, /sys) are blocked | Filesystem | NOT STARTED | | S3.1 | `usr.sbin/emu/emu_start.c` | Blocked prefix list |
| SC.19 | Shares are read-only by default | Filesystem | NOT STARTED | | S3.2 | `usr.sbin/emu/emu_engine.c` | `ro` flag default |
| SC.20 | Network defaults to host-only mode | Network | NOT STARTED | | S4.4 | `usr.sbin/emu/emu_dev_net.c` | Internal virtual network only |
| SC.21 | GDB stub binds to localhost only | Network | NOT STARTED | | S4.6 | `usr.sbin/emu/emu_gdb.c` | 127.0.0.1 binding |
| SC.22 | All MMIO accesses validate offsets and data sizes | Devices | NOT STARTED | | S4.1 | `usr.sbin/emu/emu_engine.c` | Validate offset, size, alignment |
| SC.23 | Device DMA targets guest memory only | Devices | NOT STARTED | | S4.1 | `usr.sbin/emu/emu_engine.c` | No host memory DMA |
| SC.24 | Instance registry is mutex-protected | Instance Mgmt | NOT STARTED | | S1.5 | `sys/emulation/emu_instance.c` | Thread-safe instance operations |
| SC.25 | Instance destroy cleans up all resources | Instance Mgmt | NOT STARTED | | S1.6 | `sys/emulation/emu_instance.c` | Memory, FDs, files, network |
| SC.26 | Crash detection captures state without affecting host | Instance Mgmt | NOT STARTED | | S1.6 | `sys/emulation/emu_crash.c` | Clean termination, dump isolation |
| SC.27 | Console buffer is per-instance and size-limited | Instance Mgmt | NOT STARTED | | 2.13 | `sys/emulation/emu_console.c` | Ring buffer, max size |
| SC.28 | Snapshot files have restricted permissions (0600) | Instance Mgmt | NOT STARTED | | 4.8 | `usr.sbin/emu/emu_snapshot.c` | Instance-specific directories |
| SC.29 | Multi-instance isolation enforced by process boundaries | Instance Mgmt | NOT STARTED | | S1.5 | `sys/emulation/emu_instance.c` | Separate process per instance |
| SC.30 | Access control unit tests written and passing | Testing | NOT STARTED | | SC.1–SC.9 | `tests/sys/emulation/acl_test.c` | Permission checks, ownership, limits |
| SC.31 | Access control integration tests written and passing | Testing | NOT STARTED | | SC.30 | `tests/usr.sbin/emu/acl_integration_test.sh` | End-to-end access control scenarios |
| SC.32 | Custom emulator Capsicum sandboxing implemented | Hardening | NOT STARTED | | S5.1 | `usr.sbin/emu/emu_engine.c` | `emu_enter_sandbox()`: `cap_rights_limit()` on all FDs, `cap_enter()`. See Section 6.5. |
| SC.33 | bhyve path Capsicum sandboxing implemented | Hardening | NOT STARTED | | S5.2 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_enter_sandbox()`: `cap_rights_limit()` + `cap_ioctls_limit()` on VMM FD, `cap_enter()`. See Section 6.5. |
| SC.34 | Essential FD helpers and FD cleanup implemented | Hardening | NOT STARTED | | S5.3 | `usr.sbin/emu/emu_engine.c`, `emu_bhyve.c` | `emu_is_essential_fd()` / `emu_bhyve_is_essential_fd()` |
| SC.35 | `kern.emulation.sandbox_capsicum` sysctl implemented | Hardening | NOT STARTED | | SC.32, SC.33 | `sys/emulation/emu_sysctl.c` | Enable/disable Capsicum sandboxing (default 1) |
| SC.36 | `kern.emulation.sandbox_strict` sysctl implemented | Hardening | NOT STARTED | | SC.35 | `sys/emulation/emu_sysctl.c` | Strict mode: fail on Capsicum error (default 0) |
| SC.37 | Capsicum sandboxing unit tests written and passing | Testing | NOT STARTED | | SC.32–SC.36 | `tests/sys/emulation/capsicum_test.c` | `test_capsicum_enter()`, `test_capsicum_rights_limit()`, `test_emulator_runs_under_capsicum()` |
| SC.38 | Capsicum sandboxing integration tests written and passing | Testing | NOT STARTED | | SC.37 | `tests/usr.sbin/emu/capsicum_integration_test.sh` | End-to-end Capsicum sandboxing scenarios |

---

## 11. Implementation Phases — Security, Access Control & Filesystem

### Phase S1: Core Security Infrastructure

**Objective:** Implement the fundamental security mechanisms for both execution paths.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S1.1 | Implement bounds-checked memory access in custom emulator | NOT STARTED | | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | All guest memory reads/writes check against `inst->mem_size` |
| S1.2 | Implement ELF loader with validation | NOT STARTED | | | | 5.5 | `usr.sbin/emu/emu_boot.c` | Validate ELF header, program headers, segment bounds |
| S1.3 | Implement safe instruction decoder framework | NOT STARTED | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Bounds-checked operand reads, instruction length limits |
| S1.4 | Implement bhyve privilege dropping | NOT STARTED | | | | 4.2 | `usr.sbin/emu/emu_bhyve.c` | `setuid()`/`setgid()` after VM creation |
| S1.5 | Implement instance resource limits | NOT STARTED | | | | 2.4 | `sys/emulation/emu_instance.c` | Memory caps, CPU time limits, max instances |
| S1.6 | Implement crash detection and containment | NOT STARTED | | | | 2.14 | `sys/emulation/emu_crash.c` | Detect panics, capture state, clean termination |
| S1.7 | Write security unit tests | NOT STARTED | | | | S1.1–S1.6 | `tests/sys/emulation/security_test.c` | Bounds checking, ELF validation, crash containment |

### Phase S2: Access Control & Authorization

**Objective:** Implement the access control model for multi-user emulation management.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S2.1 | Add `GID_EMU` (979) to `sys/sys/conf.h` | NOT STARTED | | | | | | `sys/sys/conf.h` | New group for emulation framework delegation |
| S2.2 | Add `PRIV_EMU_CREATE/DESTROY/MODIFY/ADMIN` to `sys/sys/priv.h` | NOT STARTED | | | | | | `sys/sys/priv.h` | Kernel privilege definitions for emulation operations |
| S2.3 | Implement `kern.emulation.allow_nonroot` sysctl | NOT STARTED | | | | | 2.3 | `sys/emulation/emu_sysctl.c` | Master switch for non-root access (default 0) |
| S2.4 | Implement per-instance ownership (ucred) | NOT STARTED | | | | | 2.4 | `sys/emulation/emu_instance.c` | Track creating user's credentials per instance |
| S2.5 | Implement granular permission checks | NOT STARTED | | | | | S2.4 | `sys/emulation/emu_instance.c` | `emu_check_perm()` for all operations |
| S2.6 | Implement per-user instance/memory limits | NOT STARTED | | | | | S2.5 | `sys/emulation/emu_instance.c` | Track per-user counts, enforce limits on create |
| S2.7 | Implement `cr_cansee()` for instance visibility | NOT STARTED | | | | | S2.4 | `sys/emulation/emu_instance.c` | Cross-user instance lookup filtering |
| S2.8 | Implement jail integration | NOT STARTED | | | | | S2.3 | `sys/emulation/emu_sysctl.c` | `pr_allow_emu_flag`, `emu_jail_priv_check()` |
| S2.9 | Add `--emu-group` flag to `emu` CLI | NOT STARTED | | | | | 6.2 | `usr.sbin/emu/emu.c` | Allow specifying group for non-root operation |
| S2.10 | Write access control unit tests | NOT STARTED | | | | | S2.1–S2.9 | `tests/sys/emulation/acl_test.c` | Permission checks, ownership, limits, jail checks |

### Phase S3: Filesystem Sharing

**Objective:** Implement controlled filesystem sharing between host and emulated instances.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S3.1 | Implement share path validation | NOT STARTED | | | | | 6.2 | `usr.sbin/emu/emu_start.c` | `realpath()`, blocked prefix check, subdirectory check |
| S3.2 | Implement custom emulator file sharing | NOT STARTED | | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | Intercept guest open/read/write, translate paths |
| S3.3 | Implement virtio-9p for bhyve path | NOT STARTED | | | | | 4.2 | `usr.sbin/bhyve/pci_virtio_9p.c` | PCI transport, 9p protocol handler |
| S3.4 | Add `--share` flag to `emu` CLI | NOT STARTED | | | | | S3.1 | `usr.sbin/emu/emu_start.c` | `--share host_path:guest_path:ro` |
| S3.5 | Implement base image management | NOT STARTED | | | | | 6.2 | `usr.sbin/emu/emu_init.c` | Download, cache, validate base images |
| S3.6 | Implement ZFS snapshot integration | NOT STARTED | | | | | S3.5 | `usr.sbin/emu/emu_zfs.c` | `emu_zfs_snapshot()`, `emu_zfs_rollback()` |
| S3.7 | Write filesystem security tests | NOT STARTED | | | | | S3.1–S3.6 | `tests/usr.sbin/emu/fs_security_test.sh` | Path traversal, symlink escape, permission tests |

### Phase S4: Device & Network Security

**Objective:** Implement secure device emulation and network isolation.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S4.1 | Implement MMIO validation framework | NOT STARTED | | | | | 5.4 | `usr.sbin/emu/emu_engine.c` | Validate offset, size, alignment for all MMIO accesses |
| S4.2 | Implement UART with console-only output | NOT STARTED | | | | | 5.6 | `usr.sbin/emu/emu_dev_uart.c` | No host file access, output to instance console buffer |
| S4.3 | Implement virtio-blk with file-backed storage | NOT STARTED | | | | | 5.9 | `usr.sbin/emu/emu_dev_storage.c` | I/O to disk image file, not host block device |
| S4.4 | Implement host-only networking | NOT STARTED | | | | | 5.2 | `usr.sbin/emu/emu_dev_net.c` | Internal virtual network, no external access |
| S4.5 | Implement NAT networking mode | NOT STARTED | | | | | S4.4 | `usr.sbin/emu/emu_dev_net.c` | Outbound-only network access via host NAT |
| S4.6 | Implement GDB stub on localhost only | NOT STARTED | | | | | 5.16 | `usr.sbin/emu/emu_gdb.c` | Bind to 127.0.0.1, no external connections |
| S4.7 | Write device security tests | NOT STARTED | | | | | S4.1–S4.6 | `tests/usr.sbin/emu/device_security_test.sh` | MMIO bounds, device state corruption, network isolation |

### Phase S5: Hardening & Audit

**Objective:** Harden the emulation framework and perform security audit.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S5.1 | Implement Capsicum sandboxing for custom emulator | NOT STARTED | | | | | S1.1 | `usr.sbin/emu/emu_engine.c` | `emu_enter_sandbox()`: limit rights on disk/console/GDB/snapshot FDs via `cap_rights_limit()`, close non-essential FDs, call `cap_enter()`. See Section 6.5 for full implementation specification. |
| S5.2 | Implement Capsicum sandboxing for bhyve process | NOT STARTED | | | | | S1.4 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_enter_sandbox()`: limit rights on `/dev/vmm/<name>` FD (CAP_READ, CAP_WRITE, CAP_IOCTL, CAP_MMAP), restrict ioctls via `cap_ioctls_limit()`, limit disk/console FDs, close non-essential FDs, call `cap_enter()`. See Section 6.5 for full implementation specification. |
| S5.3 | Close unnecessary file descriptors in both paths | NOT STARTED | | | | | S5.1, S5.2 | `usr.sbin/emu/emu_engine.c`, `emu_bhyve.c` | `emu_is_essential_fd()` / `emu_bhyve_is_essential_fd()` helpers. Close all FDs except disk, console, GDB, snapshot, VMM, and stdio. |
| S5.4 | Add instruction count limits per execution slice | NOT STARTED | | | | | S1.3 | `usr.sbin/emu/emu_engine.c` | Prevent infinite loops in guest code |
| S5.5 | Add watchdog timer for crash detection | NOT STARTED | | | | | S1.6 | `usr.sbin/emu/emu_engine.c` | Configurable timeout, trigger crash capture |
| S5.6 | Security audit of all MMIO handlers | NOT STARTED | | | | | S4.1 | All device files | Verify bounds checking, input validation |
| S5.7 | Fuzz testing of instruction decoder | NOT STARTED | | | | | S1.3 | `tests/sys/emulation/fuzz_test.c` | Random instruction sequences, edge cases |
| S5.8 | Write Capsicum sandboxing unit tests | NOT STARTED | | | | | S5.1, S5.2 | `tests/sys/emulation/capsicum_test.c` | `test_capsicum_enter()`, `test_capsicum_rights_limit()`, `test_emulator_runs_under_capsicum()`. See Section 6.5.7 for test specifications. |
| S5.9 | Write Capsicum sandboxing integration tests | NOT STARTED | | | | | S5.8 | `tests/usr.sbin/emu/capsicum_integration_test.sh` | Start instance with sandboxing enabled, verify it runs correctly, verify it cannot open new files or access /proc. |
| S5.10 | Write security documentation | NOT STARTED | | | | | S5.1–S5.9 | `share/doc/emulation/security.md` | Threat model, security guidelines, incident response |

### Phase S6: Memory Management & Overcommit Safety

**Objective:** Implement dynamic memory allocation, demand paging, balloon driver, and overcommit safeguards.

| # | Task | Status | Assigned To | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|------------|-------|-------|-----|--------------|-------|-------|
| S6.1 | Implement `emu_memmgmt.c` — memory policy sysctls | NOT STARTED | | | | | 2.3 | `sys/emulation/emu_memmgmt.c` | `memory_policy`, `memory_overcommit`, `memory_warn_percent`, `memory_balloon_min_pct`, `memory_balloon_interval` |
| S6.2 | Implement host memory capacity detection | NOT STARTED | | | | | S6.1 | `sys/emulation/emu_memmgmt.c` | Read `hw.physmem` or `vm.page_count` for total physical. Read `vm.stats.vm.v_active_count`, `vm.stats.vm.v_wire_count`, `vm.stats.vm.v_cache_count`, `vm.stats.vm.v_inactive_count` to calculate system-wide used memory (OS + all non-emulation processes). Subtract already-consumed memory from other emulation instances. Subtract configurable safety margin (`memory_system_reserve_percent`). Result is available capacity for new instances. |
| S6.3 | Implement overcommit warning logic | NOT STARTED | | | | | S6.2 | `sys/emulation/emu_memmgmt.c` | Compare total configured memory across all instances against available host capacity (total physical minus system-wide used memory (OS + other processes) minus already-consumed by other instances minus safety margin). Log warning when threshold (`memory_warn_percent`) is exceeded. Include breakdown: total physical, system-wide used, already consumed by instances, safety reserve, available, new instance request. |
| S6.4 | Implement per-instance `memory_used` tracking | NOT STARTED | | | | | S6.1 | `sys/emulation/emu_memmgmt.c` | Periodic RSS sampling via `procstat` or kernel `vmspace` |
| S6.5 | Implement demand-paged guest memory in custom emulator | NOT STARTED | | | | | 5.2 | `usr.sbin/emu/emu_engine.c` | `mmap(MAP_ANON | MAP_NORESERVE)` instead of `malloc()` |
| S6.6 | Implement prealloc memory mode | NOT STARTED | | | | | S6.5 | `usr.sbin/emu/emu_engine.c` | Traditional `malloc()` for full allocation |
| S6.7 | Implement virtio-balloon device for bhyve path | NOT STARTED | | | | | 4.2 | `usr.sbin/bhyve/pci_virtio_balloon.c` | PCI balloon device, inflate/deflate via guest |
| S6.8 | Implement balloon target sysctl interface | NOT STARTED | | | | | S6.7 | `sys/emulation/emu_memmgmt.c` | `kern.emulation.instance.<name>.balloon_target` |
| S6.9 | Implement balloon periodic adjustment timer | NOT STARTED | | | | | S6.8 | `usr.sbin/bhyve/pci_virtio_balloon.c` | `memory_balloon_interval` timer, min floor via `memory_balloon_min_pct` |
| S6.10 | Write memory management tests | NOT STARTED | | | | | S6.1–S6.9 | `tests/usr.sbin/emu/memory_test.sh` | Demand paging, balloon, overcommit, tracking, system-wide memory awareness |

---

## 12. Key Data Structures

### 12.1 Access Control Structures

```c
/* Per-instance security context */
struct emu_security_ctx {
    struct ucred *owner;            /* Creating user's credentials */
    uid_t owner_uid;                /* Cached owner UID */
    gid_t owner_gid;                /* Cached owner GID */
    int perm_mask;                  /* Custom permission mask (future) */
};

/* Global access control configuration */
struct emu_access_config {
    int allow_nonroot;              /* Master switch for non-root access */
    gid_t required_group;           /* Required group GID (0 = any) */
    int max_instances_per_user;     /* Per-user instance limit */
    int max_instances_per_group;    /* Per-group instance limit */
    int max_memory_per_user;        /* Per-user memory limit (MB) */
    int destroy_others;             /* Allow emu group to destroy any instance */
};

/* Per-user resource tracking */
struct emu_user_limits {
    uid_t uid;                      /* User ID */
    int instance_count;             /* Current instance count */
    uint64_t total_memory_mb;       /* Current total memory usage */
    LIST_ENTRY(emu_user_limits) entries;
};

/* Per-group resource tracking */
struct emu_group_limits {
    gid_t gid;                      /* Group ID */
    int instance_count;             /* Current instance count */
    LIST_ENTRY(emu_group_limits) entries;
};
```

### 12.2 Filesystem Share Configuration

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
    struct emu_security_ctx sec;         /* Security context */
    struct emu_fs_share_list shares;     /* List of filesystem shares */
    int nshares;                         /* Number of shares */
};
```

### 12.3 Security Policy

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
    char memory_policy[16];      /* "prealloc", "demand", "balloon" */
    bool memory_overcommit;      /* Allow memory overcommit for this instance */
    int balloon_min_pct;         /* Minimum balloon size as %% of configured */
};
```

### 12.4 Crash Dump

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

## 13. Sysctl Interface Additions

### 13.1 Access Control Sysctls

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.allow_nonroot` | CTLTYPE_INT | 0 | Allow non-root users to create/manage instances |
| `kern.emulation.required_group` | CTLTYPE_INT | 979 (GID_EMU) | GID required for non-root access (0 = any group) |
| `kern.emulation.max_instances_per_user` | CTLTYPE_INT | 4 | Maximum instances per non-root user |
| `kern.emulation.max_instances_per_group` | CTLTYPE_INT | 16 | Maximum instances per group |
| `kern.emulation.max_memory_per_user` | CTLTYPE_INT | 4096 | Max total MB per non-root user (0 = unlimited) |
| `kern.emulation.destroy_others` | CTLTYPE_INT | 0 | Allow emu group members to destroy any instance |

### 13.2 Security Sysctls

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.max_instances` | CTLTYPE_INT | 16 | Maximum concurrent emulated instances |
| `kern.emulation.max_memory_per_instance` | CTLTYPE_INT | 4096 | Max MB per instance (0 = unlimited) |
| `kern.emulation.sandbox_capsicum` | CTLTYPE_INT | 1 | Enable Capsicum sandboxing (if available) |
| `kern.emulation.drop_privileges` | CTLTYPE_INT | 1 | Drop root privileges after setup |
| `kern.emulation.watchdog_seconds` | CTLTYPE_INT | 30 | Default watchdog timeout |
| `kern.emulation.blocked_share_paths` | CTLTYPE_STRING | "/dev,/proc,/sys,/etc" | Comma-separated blocked share prefixes |
| `kern.emulation.memory_policy` | CTLTYPE_STRING | "demand" | Memory allocation policy: "prealloc", "demand", "balloon" |
| `kern.emulation.memory_overcommit` | CTLTYPE_INT | 0 | Allow memory overcommit (0=off, 1=warn, 2=silent) |
| `kern.emulation.memory_warn_percent` | CTLTYPE_INT | 80 | Warn when configured memory exceeds this % of available host RAM |
| `kern.emulation.memory_balloon_min_pct` | CTLTYPE_INT | 10 | Minimum balloon size as % of configured RAM |
| `kern.emulation.memory_balloon_interval` | CTLTYPE_INT | 5 | Balloon adjustment interval in seconds |
| `kern.emulation.memory_system_reserve_percent` | CTLTYPE_INT | 20 | Percentage of total physical memory reserved for OS and non-emulation processes |

---

## 14. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Non-root user abuses emulation to impact other users | High | Root-only default, per-user limits, ownership model, granular permissions |
| User in emu group escalates to root via emulator bug | Critical | Privilege dropping, Capsicum sandboxing, no kernel component for custom emulator |
| Instruction decoder bug allows arbitrary code execution in emulator process | Critical | Bounds checking, no JIT (no WX memory), Capsicum sandboxing |
| ELF loader vulnerability allows buffer overflow | Critical | Validate all headers before loading, bounds-check all segments |
| Symlink in shared directory allows host filesystem access | High | `realpath()` resolution, blocked path prefixes, read-only by default |
| Guest consumes all host memory via emulator | High | Per-instance memory limits, `max_memory_per_user` sysctl |
| bhyve VMM vulnerability allows guest escape | Critical | Use proven codebase, no passthrough devices, drop privileges |
| Network from emulated instance used for attacks | Medium | Host-only mode by default, MAC filtering, rate limiting |
| Snapshot file contains sensitive guest data | Medium | 0600 permissions, instance-specific directories, cleanup on destroy |
| Race condition in instance registry | Medium | Mutex protection, atomic operations |
| GDB stub allows arbitrary memory access | High | Localhost-only binding, authentication (future) |
| Crash dump contains host memory data | Low | Crash dumps only contain guest state, not emulator state |
| User creates excessive instances to exhaust resources | Medium | Per-user instance limits, per-group limits, `max_instances_per_user` sysctl |
| Memory overcommit causes host OOM kill | Critical | `memory_overcommit` sysctl (default 0), `memory_warn_percent` threshold, demand paging with `MAP_NORESERVE`, host memory capacity check on instance start (total physical minus system-wide used memory (OS + other processes) minus already-consumed by other instances minus safety margin) |
| Balloon driver failure causes guest memory pressure or crash | High | Minimum balloon floor via `memory_balloon_min_pct`, balloon target validation, guest cooperation required for inflation |
| Demand paging exposes host memory pressure to guest | Medium | Guest may experience unexpected latency when host is under memory pressure; mitigated by balloon driver that proactively releases memory |
| Memory tracking overhead impacts emulator performance | Low | `memory_used` sampled periodically (not on every access), configurable sampling interval |

---

## 15. Future Security Enhancements

1. **Seccomp-like syscall filtering**: Restrict syscalls available to the emulator process
2. **Address space layout randomization (ASLR)**: Randomize emulator memory layout
3. **Instruction decoder fuzzing**: Automated fuzz testing of all instruction decoders
4. **Kernel module signing**: Require signed modules before loading into emulated environment
5. **Audit logging**: Log all security-relevant events (instance create/destroy, share mounts, crashes, permission denials)
6. **Network traffic inspection**: Inspect guest network traffic for malicious patterns
7. **Resource accounting**: Track CPU time, memory, disk I/O per instance
8. **Secure snapshot encryption**: Encrypt snapshot files at rest
9. **Multi-tenant isolation**: Stronger isolation for CI/CD environments with untrusted workloads
10. **Ownership transfer**: Allow instance owner to transfer ownership to another user
11. **ACL-based permissions**: Fine-grained access control lists per instance (beyond group-based model)

---

## 16. Task Completion Checklist

> **Note for agents:** When picking up a task, fill in the **Assigned To** column with your agent name/ID. When completing a task, update the **Status** column to `COMPLETED` and add your name/ID to the **Assigned To** column if not already filled. This ensures traceability across sessions.

| # | Item | Category | Status | Assigned To | Dependencies | Files | Notes |
|---|------|----------|--------|------------|--------------|-------|-------|
| TC.1 | `GID_EMU` (979) added to `sys/sys/conf.h` | Access Control | NOT STARTED | | | `sys/sys/conf.h` | New group for emulation delegation |
| TC.2 | `PRIV_EMU_CREATE/DESTROY/MODIFY/ADMIN` added to `sys/sys/priv.h` | Access Control | NOT STARTED | | | `sys/sys/priv.h` | Kernel privilege definitions |
| TC.3 | `kern.emulation.allow_nonroot` sysctl implemented (default 0) | Access Control | NOT STARTED | | TC.1 | `sys/emulation/emu_sysctl.c` | Master switch for non-root access |
| TC.4 | `kern.emulation.required_group` sysctl implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | Required group for emulation |
| TC.5 | `kern.emulation.max_instances_per_user` sysctl implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | Per-user instance cap |
| TC.6 | `kern.emulation.max_instances_per_group` sysctl implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | Per-group instance cap |
| TC.7 | `kern.emulation.max_memory_per_user` sysctl implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | Per-user memory cap |
| TC.8 | `kern.emulation.destroy_others` sysctl implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | Allow group members to destroy any instance |
| TC.9 | Per-instance ownership (ucred) implemented | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_instance.c` | Track creating user's credentials |
| TC.10 | Granular permission checks on all operations | Access Control | NOT STARTED | | TC.9 | `sys/emulation/emu_instance.c` | `emu_check_perm()` for all ops |
| TC.11 | Per-user instance/memory limits enforced | Access Control | NOT STARTED | | TC.10 | `sys/emulation/emu_instance.c` | Enforce on create |
| TC.12 | Jail integration with `pr_allow_emu_flag` | Access Control | NOT STARTED | | TC.3 | `sys/emulation/emu_sysctl.c` | `emu_jail_priv_check()` |
| TC.13 | `cr_cansee()` check on instance lookup | Access Control | NOT STARTED | | TC.9 | `sys/emulation/emu_instance.c` | Cross-user instance visibility |
| TC.14 | Bounds-checked memory access in custom emulator | Custom Emulator | NOT STARTED | | S1.1 | `usr.sbin/emu/emu_engine.c` | All guest memory accesses checked |
| TC.15 | ELF loader with full validation | Custom Emulator | NOT STARTED | | S1.2 | `usr.sbin/emu/emu_boot.c` | Validate headers and segments |
| TC.16 | Safe instruction decoder with bounds checking | Custom Emulator | NOT STARTED | | S1.3 | `usr.sbin/emu/emu_engine.c` | Handle all inputs safely |
| TC.17 | bhyve privilege dropping | bhyve | NOT STARTED | | S1.4 | `usr.sbin/emu/emu_bhyve.c` | `setuid()`/`setgid()` after VM setup |
| TC.18 | Instance resource limits | Instance Mgmt | NOT STARTED | | S1.5 | `sys/emulation/emu_instance.c` | Memory caps, CPU time limits |
| TC.19 | Crash detection and containment | Instance Mgmt | NOT STARTED | | S1.6 | `sys/emulation/emu_crash.c` | Detect panics, clean termination |
| TC.20 | Share path validation with realpath() | Filesystem | NOT STARTED | | S3.1 | `usr.sbin/emu/emu_start.c` | Resolve symlinks, check prefixes |
| TC.21 | Dangerous paths (/dev, /proc, /sys) blocked | Filesystem | NOT STARTED | | S3.1 | `usr.sbin/emu/emu_start.c` | Blocked prefix list |
| TC.22 | Read-only shares by default | Filesystem | NOT STARTED | | S3.2 | `usr.sbin/emu/emu_engine.c` | `ro` flag default |
| TC.23 | Custom emulator file sharing | Filesystem | NOT STARTED | | S3.2 | `usr.sbin/emu/emu_engine.c` | Intercept guest file operations |
| TC.24 | virtio-9p for bhyve path | Filesystem | NOT STARTED | | S3.3 | `usr.sbin/bhyve/pci_virtio_9p.c` | PCI transport, 9p protocol |
| TC.25 | Base image management | Filesystem | NOT STARTED | | S3.5 | `usr.sbin/emu/emu_init.c` | Download, cache, validate |
| TC.26 | ZFS snapshot integration | Filesystem | NOT STARTED | | S3.6 | `usr.sbin/emu/emu_zfs.c` | `emu_zfs_snapshot()`, rollback |
| TC.27 | MMIO validation framework | Devices | NOT STARTED | | S4.1 | `usr.sbin/emu/emu_engine.c` | Validate offset, size, alignment |
| TC.28 | Host-only networking | Network | NOT STARTED | | S4.4 | `usr.sbin/emu/emu_dev_net.c` | Internal virtual network only |
| TC.29 | GDB stub on localhost only | Network | NOT STARTED | | S4.6 | `usr.sbin/emu/emu_gdb.c` | 127.0.0.1 binding |
| TC.30 | Capsicum sandboxing for custom emulator | Hardening | NOT STARTED | | S5.1 | `usr.sbin/emu/emu_engine.c` | `emu_enter_sandbox()`: `cap_rights_limit()` on disk/console/GDB/snapshot FDs, close non-essential FDs, `cap_enter()`. See Section 6.5. |
| TC.31 | Capsicum sandboxing for bhyve process | Hardening | NOT STARTED | | S5.2 | `usr.sbin/emu/emu_bhyve.c` | `emu_bhyve_enter_sandbox()`: `cap_rights_limit()` on VMM/disk/console FDs, `cap_ioctls_limit()` on VMM FD, close non-essential FDs, `cap_enter()`. See Section 6.5. |
| TC.32 | File descriptor cleanup (essential FD helpers) | Hardening | NOT STARTED | | S5.3 | `usr.sbin/emu/emu_engine.c`, `emu_bhyve.c` | `emu_is_essential_fd()` / `emu_bhyve_is_essential_fd()` helpers. Close all FDs except disk, console, GDB, snapshot, VMM, and stdio. |
| TC.33 | Instruction count limits per execution slice | Hardening | NOT STARTED | | S5.4 | `usr.sbin/emu/emu_engine.c` | Prevent infinite loops in guest code |
| TC.34 | Watchdog timer for crash detection | Hardening | NOT STARTED | | S5.5 | `usr.sbin/emu/emu_engine.c` | Configurable timeout, trigger crash capture |
| TC.35 | Memory management sysctls implemented | Memory | NOT STARTED | | 2.16 | `sys/emulation/emu_memmgmt.c` | `memory_policy`, `memory_overcommit`, `memory_warn_percent`, `memory_balloon_min_pct`, `memory_balloon_interval`, `memory_system_reserve_percent` |
| TC.36 | Demand-paged guest memory (`mmap MAP_NORESERVE`) | Memory | NOT STARTED | | 5.18 | `usr.sbin/emu/emu_engine.c` | Custom emulator demand paging |
| TC.37 | virtio-balloon device for bhyve path | Memory | NOT STARTED | | 4.9 | `usr.sbin/bhyve/pci_virtio_balloon.c` | bhyve memory reclaim |
| TC.38 | Per-instance `memory_used` tracking | Memory | NOT STARTED | | TC.35 | `sys/emulation/emu_memmgmt.c` | Actual memory usage monitoring via periodic RSS sampling |
| TC.39 | Host memory capacity detection with system-wide awareness | Memory | NOT STARTED | | TC.35 | `sys/emulation/emu_memmgmt.c` | Read `hw.physmem` for total. Read `vm.stats.vm.*` for system-wide used (OS + other processes). Subtract emulation instances. Subtract `memory_system_reserve_percent` safety margin. |
| TC.40 | Memory overcommit safeguards and warnings | Memory | NOT STARTED | | TC.39 | `sys/emulation/emu_memmgmt.c` | Compare total configured vs available capacity. Log warning at `memory_warn_percent` threshold. Include breakdown: total physical, system-wide used, instances, reserve, available. |
| TC.41 | Access control unit tests written and passing | Testing | NOT STARTED | | TC.1–TC.13 | `tests/sys/emulation/acl_test.c` | Permission checks, ownership, limits |
| TC.42 | Security unit tests written and passing | Testing | NOT STARTED | | TC.14–TC.19 | `tests/sys/emulation/security_test.c` | Bounds checking, ELF, crash |
| TC.43 | Capsicum sandboxing unit tests written and passing | Testing | NOT STARTED | | TC.30, TC.31 | `tests/sys/emulation/capsicum_test.c` | `test_capsicum_enter()`, `test_capsicum_rights_limit()`, `test_emulator_runs_under_capsicum()` |
| TC.44 | Capsicum sandboxing integration tests written and passing | Testing | NOT STARTED | | TC.43 | `tests/usr.sbin/emu/capsicum_integration_test.sh` | Start instance with sandboxing, verify correct operation under Capsicum |
| TC.45 | Filesystem security tests written and passing | Testing | NOT STARTED | | TC.20–TC.26 | `tests/usr.sbin/emu/fs_security_test.sh` | Path traversal, symlink escape |
| TC.46 | Device security tests written and passing | Testing | NOT STARTED | | TC.27–TC.29 | `tests/usr.sbin/emu/device_security_test.sh` | MMIO bounds, network isolation |
| TC.47 | Memory management tests written and passing | Testing | NOT STARTED | | TC.35–TC.40 | `tests/usr.sbin/emu/memory_test.sh` | Demand paging, balloon, overcommit, system-wide memory awareness |
| TC.48 | Fuzz testing of instruction decoder | Testing | NOT STARTED | | TC.16 | `tests/sys/emulation/fuzz_test.c` | Random instruction sequences |
| TC.49 | Security documentation written | Documentation | NOT STARTED | | TC.41–TC.48 | `share/doc/emulation/security.md` | Threat model, guidelines |

---

## 17. Conclusion

The emulation framework's security architecture is built on four layers of defense:

1. **Access control layer**: Root-only by default, group-based delegation via `emu` group, per-instance ownership, granular per-operation permissions, per-user resource limits, and jail integration ensure that only authorized users can create and manage emulated instances.

2. **OS-level isolation**: Process boundaries, user privileges, file permissions, and Capsicum sandboxing ensure that even if the emulator is compromised, the attacker gains only the privileges of an unprivileged user.

3. **Emulator-level isolation**: Bounds-checked memory access, validated ELF loading, safe instruction decoding, and input-validated device emulation prevent most attacks from succeeding within the emulator itself.

4. **Filesystem and network controls**: Controlled sharing with path validation, read-only defaults, blocked dangerous paths, and host-only networking prevent the emulated environment from accessing sensitive host resources.

The key insight is that **the custom emulator path is actually more secure than bhyve for untrusted workloads** because:
- It requires no kernel module
- It runs entirely in userland with no special privileges
- It has a much smaller codebase than bhyve
- It can be Capsicum-sandboxed
- It has no hardware passthrough capability

For trusted workloads where performance matters, the bhyve path provides hardware-enforced isolation with EPT/NPT and IOMMU protection, backed by a mature, well-audited codebase.

The access control model follows the proven bhyve/VMM pattern (GID_VMM, PRIV_VMM_*, per-VM ucred, jail integration) while extending it with granular per-operation permissions that allow fine-grained delegation — a user may start and stop an instance without being able to create or destroy it.
