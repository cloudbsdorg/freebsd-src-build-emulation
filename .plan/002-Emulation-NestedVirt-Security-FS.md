# Nested Virtualization, Security, Filesystem & Device Integration — Implementation Plan

## 1. Executive Summary

This document extends the base emulation framework plan (`001-Emulation-Overview.md`) with detailed designs for:

- **Nested virtualization**: Making VMX/SVM passthrough a switchable feature with system-wide and per-VM controls
- **Security architecture**: Preventing escape from nested VMs through hardware-enforced isolation, MSR filtering, and CPUID masking
- **Filesystem strategy**: Base image + bind mount workflow for rapid kernel module iteration
- **Device integration**: Floppy, CDROM/DVD/Blu-ray, and network card emulation for nested environments
- **Cross-architecture emulation**: Any architecture emulating any other architecture (e.g., riscv emulating arm64)

**Primary Recommendation:** A two-level nested virtualization control (sysctl + per-VM capability), virtio-9p for host→guest filesystem sharing, and a unified device model that works across all architecture combinations.

---

## 2. Nested Virtualization — Switchable Control

### 2.1 Current State

Nested virtualization is **explicitly disabled** in the FreeBSD bhyve codebase today:

- **Intel (VMX):** `CPUID2_VMX` is masked out in `sys/amd64/vmm/x86.c` — `regs[2] &= ~(CPUID2_VMX | CPUID2_EST | CPUID2_TM2);`
- **AMD (SVM):** `AMDID2_SVM` is masked out in `sys/amd64/vmm/x86.c` — `regs[2] &= ~AMDID2_SVM;`
- The AMD `MSR_VM_CR` handler in `usr.sbin/bhyve/amd64/xmsr.c` returns `VM_CR_SVMDIS` (bit 0 set = SVM disabled)
- There is **no sysctl** to toggle this — it's hardcoded off with no escape hatch

The existing capability framework (`enum vm_cap_type` in `sys/amd64/include/vmm.h`, `vm_set_capability()`/`vm_get_capability()` in `lib/libvmmapi/vmmapi.h`) provides the right pattern to follow.

### 2.2 Two-Level Control Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  System-Wide Control                         │
│  hw.vmm.nested_virt (sysctl, default: 0)                    │
│  Master gate: when 0, no VM can enable nested virt          │
│  When 1, per-VM capability is checked                       │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           v
┌─────────────────────────────────────────────────────────────┐
│                  Per-VM Control                              │
│  VM_CAP_NESTED_VIRT (capability, default: 0)                │
│  Set via bhyve --nested-virt flag or config option           │
│  Gated by system-wide sysctl                                │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           v
┌─────────────────────────────────────────────────────────────┐
│                  Hardware-Level Effects                      │
│  Intel: Expose CPUID2_VMX, stop intercepting VMX instructions│
│  AMD:   Expose AMDID2_SVM, stop intercepting SVM instructions│
│  Both:  Allow VMXON/VMXOFF/VMLAUNCH/VMRESUME in guest       │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 Implementation Details

#### 2.3.1 System-Wide Sysctl

Add `hw.vmm.nested_virt` to `sys/amd64/vmm/vmm.c`:

```c
/* In vmm.c */
static int vmm_nested_virt = 0;
SYSCTL_INT(_hw_vmm, OID_AUTO, nested_virt, CTLFLAG_RWTUN,
    &vmm_nested_virt, 0,
    "Enable nested virtualization (0=disabled, 1=enabled)");
```

- `CTLFLAG_RWTUN` = readable/writable at runtime, tunable via loader
- Default: 0 (disabled)
- When 0, all nested virt requests are denied regardless of per-VM capability
- When 1, per-VM capability is honored

#### 2.3.2 Per-VM Capability

Add `VM_CAP_NESTED_VIRT` to `enum vm_cap_type` in `sys/amd64/include/vmm.h`:

```c
enum vm_cap_type {
    VM_CAP_HALT_EXIT,        /* HLT instruction causes VM exit */
    VM_CAP_MTRAP_EXIT,       /* Monitor/MWAIT trap */
    VM_CAP_PAUSE_EXIT,       /* PAUSE instruction causes VM exit */
    VM_CAP_NESTED_VIRT,      /* Nested virtualization (VMX/SVM passthrough) */
    VM_CAP_MAX               /* Must be last */
};
```

#### 2.3.3 Intel VMX Implementation (`sys/amd64/vmm/intel/vmx.c`)

In `vmx_setcap()`:

```c
case VM_CAP_NESTED_VIRT:
    if (val && !vmm_nested_virt) {
        /* System-wide nested virt is disabled */
        return (ENXIO);
    }
    /* Toggle VMX instruction intercepts */
    vmx->caps.nested_virt = val;
    if (val) {
        /* Expose VMX CPUID bits */
        /* Stop intercepting: VMXON, VMXOFF, VMLAUNCH, VMRESUME,
         * VMPTRLD, VMPTRST, VMCLEAR, VMREAD, VMWRITE, VMCALL,
         * INVEPT, INVVPID */
    } else {
        /* Mask VMX CPUID bits */
        /* Start intercepting all VMX instructions */
    }
    break;
```

Key VMX controls to modify:
- **Pin-based VM-execution controls**: Clear "NMI exiting" and "virtual NMIs" when nested is enabled
- **Primary processor-based controls**: Clear "VMX" related intercepts
- **Secondary processor-based controls**: Enable "VMCS shadowing" for nested VMX
- **VM-exit controls**: Enable "VM-exit saving VMX-preemption timer value"
- **VM-entry controls**: Enable "VM-entry loading VMX-preemption timer value"

#### 2.3.4 AMD SVM Implementation (`sys/amd64/vmm/amd/svm.c`)

In `svm_setcap()`:

```c
case VM_CAP_NESTED_VIRT:
    if (val && !vmm_nested_virt) {
        return (ENXIO);
    }
    svm->caps.nested_virt = val;
    if (val) {
        /* Expose SVM CPUID bits */
        /* Clear SVMDIS in MSR_VM_CR */
        /* Stop intercepting: VMRUN, VMLOAD, VMSAVE, CLGI, STGI,
         * INVLPGA, SKINIT */
    } else {
        /* Mask SVM CPUID bits */
        /* Set SVMDIS in MSR_VM_CR */
        /* Start intercepting all SVM instructions */
    }
    break;
```

#### 2.3.5 CPUID Masking (`sys/amd64/vmm/x86.c`)

Modify the CPUID masking to be conditional:

```c
/* In x86_emulate_cpuid() */
case 0x01:
    regs[2] &= ~(CPUID2_EST | CPUID2_TM2);
    if (!nested_virt_enabled(vm, vcpuid)) {
        regs[2] &= ~CPUID2_VMX;  /* Intel */
    }
    break;

case 0x80000001:
    if (!nested_virt_enabled(vm, vcpuid)) {
        regs[2] &= ~AMDID2_SVM;  /* AMD */
    }
    break;
```

#### 2.3.6 MSR Handling (`usr.sbin/bhyve/amd64/xmsr.c`)

Modify the AMD MSR_VM_CR handler:

```c
/* In xmsr.c MSR_VM_CR handling */
if (nested_virt_enabled(vm, vcpuid)) {
    /* Return the real MSR_VM_CR value (SVM enabled) */
    *val = native_msr_vm_cr;
} else {
    /* Return SVMDIS set (SVM disabled) */
    *val = VM_CR_SVMDIS;
}
```

#### 2.3.7 bhyve Userland Flag (`usr.sbin/bhyve/bhyverun.c`)

Add `--nested-virt` flag:

```c
static int nested_virt = 0;

/* Option parsing */
{ "nested-virt",      no_argument,       &nested_virt, 1 },

/* VM creation */
if (nested_virt) {
    error = vm_set_capability(vm, VM_CAP_NESTED_VIRT, 0, 1);
    if (error) {
        warnx("Failed to enable nested virtualization: %s",
              strerror(error));
    }
}
```

### 2.4 Nested Depth Limiting

Add a sysctl for maximum nested depth:

```c
static int vmm_nested_max_depth = 1;
SYSCTL_INT(_hw_vmm, OID_AUTO, nested_max_depth, CTLFLAG_RWTUN,
    &vmm_nested_max_depth, 1,
    "Maximum nested virtualization depth (default: 1)");
```

- Default: 1 (L0 → L1 → L2, one level of nesting)
- The VMM tracks current nesting depth per-vCPU
- Requests to nest beyond the limit are denied
- This prevents resource exhaustion attacks

---

## 3. Security Architecture — Preventing Escape

### 3.1 Threat Model

| Threat | Description | Severity | Vector |
|--------|-------------|----------|--------|
| L1 hypervisor escape | Guest hypervisor (L1) exploits a bug in bhyve (L0) to break out | Critical | VMX/SVM instruction emulation bugs, MSR handling bugs |
| L2 guest escape | Guest OS (L2) exploits L1 hypervisor bug to break into L1, then L0 | Critical | Chained exploitation across nesting levels |
| Resource starvation | L1 guest consumes excessive VMX/SVM resources, starving other VMs | High | Infinite VM-exit loops, memory exhaustion |
| MSR/Cache side-channels | L2 guest observes L1 or L0 state via timing or MSR leaks | Medium | Performance counters, cache timing, MSR-based probing |
| Device escape | Malicious guest exploits device emulation bugs | High | DMA attacks, MMIO bugs, firmware exploits |

### 3.2 Hardware-Enforced Isolation Layers

```
┌─────────────────────────────────────────────────────────────┐
│  L0 (Host / bhyve)                                          │
│  Controls: EPT/NPT page tables, IOMMU, MSR bitmaps          │
│  Cannot be touched by L1 or L2                              │
├─────────────────────────────────────────────────────────────┤
│  EPT/NPT (2nd-level page tables) ←── Hardware enforced      │
├─────────────────────────────────────────────────────────────┤
│  L1 (Guest Hypervisor)                                      │
│  Controls: Guest page tables for L2                         │
│  Can only map memory that L0 gave it                        │
├─────────────────────────────────────────────────────────────┤
│  Guest page tables (1st-level) ←── Hardware enforced        │
├─────────────────────────────────────────────────────────────┤
│  L2 (Nested Guest)                                          │
│  Runs inside L1's VMX/SVM guest mode                        │
│  All exits go to L1 first, then potentially to L0           │
└─────────────────────────────────────────────────────────────┘
```

**Key insight:** EPT/NPT provides hardware-enforced isolation. Even a compromised L1 hypervisor cannot access L0 memory because the hardware page tables are controlled by L0. The hardware does a **3-level page table walk** (L2 guest → L1 guest → L0 host), and L0 controls the outermost level.

### 3.3 Mitigation Strategies

#### 3.3.1 IOMMU Isolation (Already Present)

- bhyve already uses the AMD-Vi/Intel VT-d IOMMU for passthrough devices
- Nested VMs should **never** get direct device assignment — all I/O goes through the L1 hypervisor's emulated devices
- The IOMMU remains under L0 control, so even if L1 is compromised, DMA attacks are blocked
- **Enforcement:** Add a check in the passthrough device assignment path that rejects assignment if the VM has nested virt enabled

#### 3.3.2 MSR Filtering

Use VMX MSR bitmaps (already supported) to selectively pass through safe MSRs and intercept dangerous ones:

**Safe to pass through to L1 (needed for nested virt):**
- `MSR_VMX_BASIC`
- `MSR_VMX_PINBASED_CTLS`
- `MSR_VMX_PROCBASED_CTLS`
- `MSR_VMX_EXIT_CTLS`
- `MSR_VMX_ENTRY_CTLS`
- `MSR_VMX_MISC`
- `MSR_VMX_CR0_FIXED0`
- `MSR_VMX_CR0_FIXED1`
- `MSR_VMX_CR4_FIXED0`
- `MSR_VMX_CR4_FIXED1`
- `MSR_VMX_VMCS_ENUM`
- `MSR_VMX_PROCBASED_CTLS2`
- `MSR_VMX_EPT_VPID_CAP`
- `MSR_VMX_TRUE_PINBASED_CTLS`
- `MSR_VMX_TRUE_PROCBASED_CTLS`
- `MSR_VMX_TRUE_EXIT_CTLS`
- `MSR_VMX_TRUE_ENTRY_CTLS`

**Must intercept (security-critical):**
- `MSR_SYSENTER_EIP_MSR` / `MSR_SYSENTER_ESP_MSR`
- `MSR_EFER` (partial — SF bit must be controlled)
- `MSR_STAR` / `MSR_LSTAR` / `MSR_CSTAR` (syscall targets)
- `MSR_FSBASE` / `MSR_GSBASE` / `MSR_KERNELGSBASE`
- `MSR_MTRR*` (memory type range registers)
- `MSR_PAT` (page attribute table)
- `MSR_PERF_EVNTSEL*` / `MSR_PERF_CTR*` (performance counters — side channel risk)
- `MSR_DEBUGCTL` (debug control — side channel risk)
- `MSR_LASTBRANCH*` (LBR — side channel risk)

**Implementation in `vmx.c`:**

```c
static const int nested_safe_msrs[] = {
    MSR_VMX_BASIC, MSR_VMX_PINBASED_CTLS, MSR_VMX_PROCBASED_CTLS,
    MSR_VMX_EXIT_CTLS, MSR_VMX_ENTRY_CTLS, MSR_VMX_MISC,
    MSR_VMX_CR0_FIXED0, MSR_VMX_CR0_FIXED1,
    MSR_VMX_CR4_FIXED0, MSR_VMX_CR4_FIXED1,
    MSR_VMX_VMCS_ENUM, MSR_VMX_PROCBASED_CTLS2,
    MSR_VMX_EPT_VPID_CAP,
    MSR_VMX_TRUE_PINBASED_CTLS, MSR_VMX_TRUE_PROCBASED_CTLS,
    MSR_VMX_TRUE_EXIT_CTLS, MSR_VMX_TRUE_ENTRY_CTLS,
};

void
vmx_nested_setup_msr_bitmap(struct vmx *vmx, int vcpuid)
{
    /* Pass through safe MSRs */
    for (int i = 0; i < nitems(nested_safe_msrs); i++) {
        vmx_msr_bitmap_allow(vmx, vcpuid, nested_safe_msrs[i], true);
        vmx_msr_bitmap_allow(vmx, vcpuid, nested_safe_msrs[i], false);
    }
    /* All other MSRs are intercepted by L0 */
}
```

#### 3.3.3 CPUID Masking

The CPUID leaves exposed to L1 must be carefully curated. Don't expose features that could be used for side-channel attacks:

```c
void
x86_nested_mask_cpuid(struct vm *vm, int vcpuid, u_int *eax, u_int *ebx,
    u_int *ecx, u_int *edx)
{
    switch (*eax) {
    case 0x01:
        /* Mask: Intel PT, PDCM, PCID, INVPCID, TM, TM2 */
        *ecx &= ~(CPUID2_DTES64 | CPUID2_DSCPL | CPUID2_EST |
                  CPUID2_TM2 | CPUID2_PDCM);
        *edx &= ~(CPUID1_TM | CPUID1_PBE);
        break;
    case 0x07:
        /* Mask: Intel PT, HLE, RTM, RDCL_NO */
        *ebx &= ~(CPUID7_EBX_HLE | CPUID7_EBX_RTM |
                  CPUID7_EBX_RDCL_NO);
        *edx &= ~(CPUID7_EDX_ARCH_CAP);
        break;
    case 0x80000001:
        /* Mask: SVM (AMD) — controlled by nested_virt cap */
        if (!nested_virt_enabled(vm, vcpuid))
            *edx &= ~AMDID2_SVM;
        break;
    }
}
```

#### 3.3.4 Resource Limits

Add per-VM resource limits for nested virtualization:

```c
struct vmm_nested_limits {
    int max_nested_depth;       /* Max nesting depth (default: 1) */
    int max_vmexit_rate;        /* Max VM exits/sec before throttling */
    int max_vmcs_pages;         /* Max VMCS pages L1 can allocate */
};

/* Sysctl-adjustable defaults */
static struct vmm_nested_limits vmm_nested_limits = {
    .max_nested_depth = 1,
    .max_vmexit_rate = 100000,  /* 100K exits/sec */
    .max_vmcs_pages = 64,
};
```

#### 3.3.5 Security Checklist for Nested Virt Enablement

- [ ] IOMMU remains under L0 control — no passthrough devices to nested VMs
- [ ] EPT/NPT controlled by L0 — L1 cannot map memory it doesn't own
- [ ] MSR bitmaps filter all security-critical MSRs
- [ ] CPUID masking prevents side-channel feature exposure
- [ ] VM-exit rate limiting prevents resource starvation
- [ ] Nested depth limited to 1 (L0→L1→L2) by default
- [ ] System-wide sysctl provides master kill switch
- [ ] Per-VM capability defaults to off
- [ ] All device emulation stays in L0 userland (bhyve process)
- [ ] No direct hardware access from nested VMs

---

## 4. Filesystem Strategy — Base Image + Bind Mounts

### 4.1 Architecture for Kernel Module Development

```
Host (L0)                    L1 Guest (bhyve VM)           L2 Guest (nested VM)
─────────                    ──────────────────            ────────────────────
                              ┌─────────────────┐
                              │  Base image     │          ┌─────────────────┐
                              │  (ZFS snapshot) │          │  Test VM        │
                              │                 │          │  (kernel module │
                              │  /mnt/host_src  │◄────────►│   under dev)    │
                              │  (9p/virtio-fs) │  bind    │                 │
                              │                 │  mount   └─────────────────┘
                              └─────────────────┘
```

### 4.2 Implementation Options

#### Option A: virtio-9p (Plan 9 filesystem) — RECOMMENDED

**Pros:**
- File-level sharing (not block-level)
- Supports concurrent access from host and guest
- No block layer overhead
- Well-understood protocol

**Cons:**
- Not yet in bhyve upstream (needs implementation)
- Performance is moderate (not as fast as virtio-fs)

**Implementation plan:**

1. Add virtio-9p device model to `usr.sbin/bhyve/`:
   - `usr.sbin/bhyve/pci_virtio_9p.c` — PCI transport for 9p
   - `usr.sbin/bhyve/virtio_9p.c` — 9p protocol handler
   - `usr.sbin/bhyve/virtio_9p.h` — 9p protocol definitions

2. 9p protocol operations to implement:
   - `Tversion/Tversion` — protocol version negotiation
   - `Tattach/Tattach` — attach to filesystem
   - `Twalk/Twalk` — traverse directory hierarchy
   - `Topen/Topen` — open file
   - `Tread/Tread` — read file
   - `Twrite/Twrite` — write file
   - `Tclunk/Tclunk` — close file handle
   - `Tstat/Tstat` — get file attributes
   - `Tcreate/Tcreate` — create file
   - `Tremove/Tremove` — remove file

3. Security: Use the `map=` option to map UID/GID:
   ```
   bhyve -s <slot>,virtio-9p,sharename=/host/src,map=1000:1000
   ```

#### Option B: virtio-fs (vhost-user-fs)

**Pros:**
- Very high performance
- DAX support for direct memory mapping
- Cache coherent

**Cons:**
- Requires FUSE in L1 guest
- More complex setup
- Not yet in FreeBSD base

**Implementation plan:**
- Add FUSE kernel module to L1 guest
- Implement vhost-user protocol in bhyve
- Add virtio-fs device model

#### Option C: ZFS + Block-Level Snapshots

**Pros:**
- Clean state every time
- ZFS is already on FreeBSD
- No special filesystem drivers needed

**Cons:**
- Slower (full reboot or at least block device reset)
- No live sharing of source code

**Workflow:**
```
1. Create ZFS dataset for L1 VM root
2. Before each test: zfs snapshot <dataset>@clean
3. Boot L1 from snapshot
4. After test: zfs rollback <dataset>@clean
5. Iterate
```

### 4.3 Recommended Workflow for Kernel Module Iteration

```
Step 1: Host has the source tree (e.g., /home/user/kernel-module/)
Step 2: L1 VM boots from a base ZFS volume/image
Step 3: L1 mounts the host source tree via virtio-9p at /mnt/host_src
Step 4: L1 compiles the kernel module inside the VM
Step 5: L1 boots L2 (nested VM) with the newly compiled module
Step 6: L1 bind-mounts /mnt/host_src into L2 for any runtime data
Step 7: Test in L2, crash L2, iterate — L1 and host remain stable
```

### 4.4 Security Considerations for Filesystem Sharing

- virtio-9p should use the `map=` option to map the shared directory's UID/GID
- Never share the host's `/dev`, `/proc`, or `/sys` into a VM
- Use ZFS datasets with `jail`-style permissions for the shared directory
- Consider read-only mounts for the base image
- The shared directory should be a dedicated dataset, not the host root
- Use `nodev`, `noexec`, `nosuid` mount flags on the shared directory

### 4.5 Integration with Emulation Framework

Add filesystem sharing support to the `emu` CLI tool:

```
emu start --name test-vm \
    --share /home/user/kernel-module:/mnt/host_src \
    --share /home/user/build:/mnt/build:ro
```

The `--share` flag syntax: `<host_path>:<guest_mount_point>[:ro]`

---

## 5. Device Integration

### 5.1 Device Model Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Device Bus Model                          │
│                                                              │
│  PCI Bus (pci_bus.c)                                        │
│  ├── virtio-blk (pci_virtio_block.c)    — Storage           │
│  ├── virtio-net (pci_virtio_net.c)      — Network           │
│  ├── virtio-9p (pci_virtio_9p.c)        — Shared FS         │
│  ├── AHCI (pci_ahci.c)                  — SATA (CDROM/DVD)  │
│  ├── Floppy (pci_lpc.c → ISA FDC)       — Floppy            │
│  └── Intel 82545 EM (pci_e82545.c)      — Legacy NIC        │
│                                                              │
│  ISA/LPC Bus (lpc.c)                                        │
│  ├── NS16550 UART (uart.c)              — Serial console    │
│  ├── i8254 PIT (pit.c)                  — Timer             │
│  └── Floppy Controller (fdc.c)          — Floppy (ISA)      │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 Floppy Drive Emulation

**Use case:** Booting legacy OSes in nested VMs, firmware updates, small data transfer.

**Implementation:**
- Add floppy disk controller (FDC) emulation based on Intel 82072AA or NEC 765
- Support 1.44MB and 720KB floppy images
- Attach via ISA/LPC bus (existing `pci_lpc.c` infrastructure)
- Image file format: raw `.img` or `.flp`

**Files:**
- `usr.sbin/bhyve/isa_fdc.c` — Floppy disk controller
- `usr.sbin/bhyve/isa_fdc.h` — FDC register definitions

**Security:** Low risk — floppy emulation is simple and well-tested. No DMA to host memory.

### 5.3 CDROM/DVD/Blu-ray Emulation

**Use case:** Installing OSes in nested VMs, live CDs, large software distributions.

**Implementation:**
- Reuse existing AHCI emulation (`pci_ahci.c`) which supports ATAPI devices
- ISO files attached as virtual ATAPI devices
- Blu-ray uses the same AHCI/ATAPI path — the difference is media size and UDF filesystem
- bhyve doesn't need special Blu-ray emulation — just present a large virtual ATAPI device

**bhyve command-line syntax:**
```
bhyve -s <slot>,ahci-cd,/path/to/image.iso
```

**Files:**
- `usr.sbin/bhyve/pci_ahci.c` — Already exists, extend ATAPI support
- `usr.sbin/bhyve/ahci.h` — Already exists

**Security:** Low risk — AHCI emulation is mature. No direct host block device access.

### 5.4 Network Card Emulation

**Use case:** Network access for nested VMs, package downloads, inter-VM communication.

**Supported NIC models:**

| NIC | Type | Speed | Use Case |
|-----|------|-------|----------|
| virtio-net | Paravirtual | 10 Gbps | Default, best performance |
| Intel 82545 EM | PCIe | 1 Gbps | Legacy OS compatibility |
| e1000 | PCI | 100 Mbps | Very old OS compatibility |

**Nested networking topologies:**

```
NAT Mode (simplest, most secure):
  L2 → L1 (NAT) → Host (NAT) → Internet
  L2 gets private IP from L1's DHCP
  L1 masquerades L2 traffic

Bridged Mode (L2 gets physical network access):
  L2 → L1 bridge → Host bridge → Physical network
  L2 gets its own IP on the physical network
  Needs L1 to bridge its virtual NIC to a physical NIC

Host-Only (safest for kernel module dev):
  L2 → L1 internal bridge
  L2 can only talk to L1
  No external network access
```

**Recommended for kernel module development:** Host-only mode with optional NAT for package downloads.

**Security:**
- Use virtio-net with MAC filtering
- Consider limiting L2's network access to host-only during development
- Never bridge L2 directly to a physical NIC without L1 filtering

### 5.5 Recommended Device Configuration for Kernel Module Development

```
L1 VM:
  - 1-2 vCPUs
  - 2-4 GB RAM
  - virtio-blk: base ZFS volume (read-only base image)
  - virtio-9p: host source tree (shared, writable)
  - virtio-net: host-only or NAT (for package downloads)
  - AHCI: ISO for L2 guest OS installation media

L2 VM (nested):
  - 1 vCPU
  - 512 MB - 1 GB RAM
  - virtio-blk: ephemeral disk (discard on shutdown)
  - virtio-net: host-only (connected to L1's internal bridge)
  - No direct host device access
```

---

## 6. Cross-Architecture Emulation — Any Architecture on Any Architecture

### 6.1 Design Principle

The emulation framework must support **any host architecture emulating any target architecture**. This means:

- amd64 host can emulate: amd64, i386, arm64, arm, riscv64, powerpc
- arm64 host can emulate: amd64, i386, arm64, arm, riscv64, powerpc
- riscv64 host can emulate: amd64, i386, arm64, arm, riscv64, powerpc
- etc.

### 6.2 Architecture Support Matrix

| Host \ Target | amd64 | i386 | arm64 | arm | riscv64 | powerpc |
|---------------|-------|------|-------|-----|---------|---------|
| amd64         | bhyve+emu | emu | emu | emu | emu | emu |
| arm64         | emu   | emu  | bhyve+emu | emu | emu | emu |
| riscv64       | emu   | emu  | emu   | emu | bhyve+emu | emu |
| arm (32-bit)  | emu   | emu  | emu   | emu | emu | emu |
| powerpc       | emu   | emu  | emu   | emu | emu | emu |

- **bhyve**: Only available when host == target architecture AND VMM hardware is present
- **emu**: Custom emulator always available (software emulation)

### 6.3 Implementation Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Emulation Engine                          │
│  (usr.sbin/emu/emu_engine.c)                                │
│                                                              │
│  Architecture-independent core:                              │
│  - Fetch-decode-execute loop                                 │
│  - Memory bus (RAM + MMIO dispatch)                          │
│  - Interrupt controller framework                            │
│  - Device model (timers, UART, storage, network)             │
│  - GDB stub for debugging                                   │
│  - Snapshot/restore                                          │
└──────────┬──────────────────────────────────────┬───────────┘
           │                                      │
           v                                      v
┌──────────────────────┐           ┌──────────────────────────┐
│  CPU Emulation Core   │           │  Device Models           │
│  (sys/emulation/)     │           │  (usr.sbin/emu/)         │
│                       │           │                          │
│  amd64/emu_cpu.c      │           │  UART (NS16550)          │
│  arm64/emu_cpu.c      │           │  Timer (HPET/PIT)        │
│  riscv/emu_cpu.c      │           │  virtio-blk              │
│  i386/emu_cpu.c       │           │  virtio-net              │
│  arm/emu_cpu.c        │           │  virtio-9p               │
│  powerpc/emu_cpu.c    │           │  AHCI (CDROM/DVD)        │
│                       │           │  Floppy (FDC)            │
│  + MMU emulation      │           │  Interrupt Controller    │
│  + Interrupt model    │           │                          │
└──────────────────────┘           └──────────────────────────┘
```

### 6.4 Key Design Decisions for Cross-Architecture Support

#### 6.4.1 Endianness Handling

Different architectures have different endianness:
- **Little-endian**: amd64, i386, arm64, arm, riscv64
- **Big-endian**: powerpc (some modes support both)

The emulator must handle endianness conversion when the host and target differ:

```c
/* In emu_mem.c */
uint64_t
emu_mem_read(struct emu_instance *instance, uint64_t addr, int size)
{
    uint64_t val = ...; /* Read from memory */
    
    if (instance->config.target_endian != instance->config.host_endian) {
        val = emu_endian_swap(val, size);
    }
    return val;
}
```

#### 6.4.2 Register File Abstraction

Each architecture has a different register file layout. The emulator uses a unified interface:

```c
/* Architecture-independent register interface */
struct emu_cpu_ops {
    const char *name;
    int nregs;
    const char *reg_names[64];
    int (*get_reg)(struct emu_cpu_state *cpu, int reg, uint64_t *val);
    int (*set_reg)(struct emu_cpu_state *cpu, int reg, uint64_t val);
    int (*get_pc)(struct emu_cpu_state *cpu, uint64_t *pc);
    int (*set_pc)(struct emu_cpu_state *cpu, uint64_t pc);
    int (*get_sp)(struct emu_cpu_state *cpu, uint64_t *sp);
    int (*get_fp)(struct emu_cpu_state *cpu, uint64_t *fp);
    int (*step)(struct emu_cpu_state *cpu);
    int (*translate)(struct emu_cpu_state *cpu, uint64_t vaddr, uint64_t *paddr);
};
```

#### 6.4.3 Page Size Abstraction

Different architectures use different page sizes:
- amd64: 4KB (default), 2MB, 1GB
- arm64: 4KB, 16KB, 64KB
- riscv64: 4KB (Sv39), 4KB (Sv48)
- powerpc: 4KB, 64KB, 256KB, 16MB, 16GB

The MMU emulation must handle the target's page size, not the host's:

```c
struct emu_mmu_ops {
    int page_size_bits;       /* e.g., 12 for 4KB */
    int page_table_levels;    /* e.g., 4 for amd64 PML4 */
    int (*walk)(struct emu_cpu_state *cpu, uint64_t vaddr,
                uint64_t *paddr, int *prot);
    int (*tlb_lookup)(struct emu_cpu_state *cpu, uint64_t vaddr,
                      uint64_t *paddr);
    void (*tlb_flush)(struct emu_cpu_state *cpu);
};
```

#### 6.4.4 Instruction Decoder Portability

Each architecture's instruction decoder is self-contained in its own directory:

```
sys/emulation/
├── amd64/
│   ├── emu_cpu_amd64.c      # x86-64 decoder + executor
│   ├── emu_mmu_amd64.c      # 4-level paging
│   └── emu_intr_amd64.c     # IDT, LAPIC, I/O APIC
├── arm64/
│   ├── emu_cpu_arm64.c      # AArch64 decoder + executor
│   ├── emu_mmu_arm64.c      # VMSAv8-64 page tables
│   └── emu_intr_arm64.c     # GICv3 emulation
├── riscv/
│   ├── emu_cpu_riscv.c      # RV64I decoder + executor
│   ├── emu_mmu_riscv.c      # Sv39/Sv48 page tables
│   └── emu_intr_riscv.c     # CLINT/PLIC emulation
├── i386/
│   ├── emu_cpu_i386.c       # x86-32 decoder + executor
│   ├── emu_mmu_i386.c       # 2-level paging (PAE/NX)
│   └── emu_intr_i386.c      # IDT, PIC, I/O APIC
├── arm/
│   ├── emu_cpu_arm.c        # ARMv7 decoder + executor
│   ├── emu_mmu_arm.c        # VMSAv7 page tables
│   └── emu_intr_arm.c       # GIC emulation
└── powerpc/
    ├── emu_cpu_ppc.c        # PowerISA decoder + executor
    ├── emu_mmu_ppc.c        # PowerPC page tables (hash/page)
    └── emu_intr_ppc.c       # MPIC/XICS emulation
```

#### 6.4.5 Boot Protocol Abstraction

Each architecture has a different boot protocol. The emulator must handle this:

```c
struct emu_boot_ops {
    const char *name;
    int (*load_kernel)(struct emu_instance *inst, const char *path);
    int (*setup_page_tables)(struct emu_instance *inst);
    int (*setup_interrupts)(struct emu_instance *inst);
    int (*jump_to_entry)(struct emu_instance *inst, uint64_t entry);
};
```

**Architecture boot protocols:**

| Architecture | Boot Protocol | Entry Point | Initial State |
|-------------|---------------|-------------|---------------|
| amd64 | Multiboot, EFI | 0x100000 (or EFI) | A20 gate on, GDT loaded, protected mode |
| i386 | Multiboot | 0x100000 | A20 gate on, GDT loaded, protected mode |
| arm64 | EFI, FDT | _start (EL2 or EL1) | MMU off, FDT pointer in x0 |
| arm | FDT, ATAG | _start (SVC mode) | MMU off, FDT/ATAG pointer in r2 |
| riscv64 | OpenSBI, BBL | _start (M-mode or S-mode) | a0 = hart ID, a1 = FDT pointer |
| powerpc | OF, FDT | _start (real mode) | r3 = FDT pointer, MSR = 0 |

### 6.5 Performance Considerations

| Host → Target | Mode | Relative Performance | Notes |
|---------------|------|---------------------|-------|
| amd64 → amd64 | bhyve | ~95% of native | Hardware virtualization |
| amd64 → i386 | bhyve | ~95% of native | VMX with 32-bit guest mode |
| amd64 → arm64 | emu | ~1-5% of native | Full software emulation |
| amd64 → riscv64 | emu | ~1-5% of native | Full software emulation |
| arm64 → amd64 | emu | ~1-5% of native | Full software emulation |
| arm64 → arm64 | bhyve | ~95% of native | Hardware virtualization (if available) |

For cross-architecture emulation, performance will be significantly slower than native. This is acceptable for kernel module testing where the focus is on correctness, not throughput.

### 6.6 Cross-Architecture Testing Workflow

```
# Example: Testing an arm64 kernel module on an amd64 host
emu init --arch arm64 --name test-arm64
emu start --name test-arm64 --kernel /path/to/arm64/kernel \
    --image /path/to/arm64/rootfs.img \
    --share /home/user/module:/mnt/src
emu load --name test-arm64 --module /mnt/src/test_module.ko
emu stack --name test-arm64
emu stop --name test-arm64

# Example: Testing a riscv64 kernel module on an arm64 host
emu init --arch riscv64 --name test-riscv
emu start --name test-riscv --kernel /path/to/riscv/kernel \
    --image /path/to/riscv/rootfs.img
emu load --name test-riscv --module /path/to/module.ko
emu test --name test-riscv --test /path/to/test-script.sh
emu destroy --name test-riscv
```

---

## 7. Implementation Phases

### Phase NV1: Nested Virtualization — System-Wide Control

**Objective:** Add the `hw.vmm.nested_virt` sysctl and basic infrastructure.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| NV1.1 | Add `hw.vmm.nested_virt` sysctl to `vmm.c` | NOT STARTED | | | | | `sys/amd64/vmm/vmm.c` | CTLFLAG_RWTUN, default 0 |
| NV1.2 | Add `VM_CAP_NESTED_VIRT` to `enum vm_cap_type` in `vmm.h` | NOT STARTED | | | | NV1.1 | `sys/amd64/include/vmm.h` | New capability enum value |
| NV1.3 | Implement `vmx_setcap()` for `VM_CAP_NESTED_VIRT` | NOT STARTED | | | | NV1.2 | `sys/amd64/vmm/intel/vmx.c` | Toggle VMX instruction intercepts |
| NV1.4 | Implement `svm_setcap()` for `VM_CAP_NESTED_VIRT` | NOT STARTED | | | | NV1.2 | `sys/amd64/vmm/amd/svm.c` | Toggle SVM instruction intercepts |
| NV1.5 | Conditional CPUID masking in `x86.c` | NOT STARTED | | | | NV1.3, NV1.4 | `sys/amd64/vmm/x86.c` | Conditionally expose VMX/SVM CPUID bits |
| NV1.6 | MSR_VM_CR handling for AMD nested virt | NOT STARTED | | | | NV1.4 | `usr.sbin/bhyve/amd64/xmsr.c` | Return real MSR_VM_CR when nested enabled |
| NV1.7 | Add `--nested-virt` flag to bhyve | NOT STARTED | | | | NV1.2 | `usr.sbin/bhyve/bhyverun.c` | CLI flag to enable per-VM nested virt |
| NV1.8 | Add nested depth limiting sysctl | NOT STARTED | | | | NV1.1 | `sys/amd64/vmm/vmm.c` | `hw.vmm.nested_max_depth`, default 1 |

### Phase NV2: Nested Virtualization — MSR Filtering & Security

**Objective:** Implement MSR bitmaps, CPUID masking, and resource limits for nested VMs.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| NV2.1 | Define safe MSR list for nested virt | NOT STARTED | | | | NV1.3 | `sys/amd64/vmm/intel/vmx.c` | List of MSRs to pass through |
| NV2.2 | Implement MSR bitmap setup for nested VMs | NOT STARTED | | | | NV2.1 | `sys/amd64/vmm/intel/vmx.c` | `vmx_nested_setup_msr_bitmap()` |
| NV2.3 | Implement CPUID masking for nested VMs | NOT STARTED | | | | NV1.5 | `sys/amd64/vmm/x86.c` | `x86_nested_mask_cpuid()` |
| NV2.4 | Add VM-exit rate limiting | NOT STARTED | | | | NV1.3 | `sys/amd64/vmm/vmm.c` | Per-VM exit rate counter + throttle |
| NV2.5 | Add passthrough device assignment guard | NOT STARTED | | | | NV1.2 | `sys/amd64/vmm/vmm.c` | Reject passthrough if nested virt enabled |
| NV2.6 | Write security verification tests | NOT STARTED | | | | NV2.1–NV2.5 | `tests/sys/vmm/nested_security_test.c` | Verify MSR filtering, CPUID masking, etc. |

### Phase FS1: Filesystem Sharing — virtio-9p

**Objective:** Implement virtio-9p device model for host→guest filesystem sharing.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| FS1.1 | Design virtio-9p transport for bhyve | NOT STARTED | | | | | `docs/virtio-9p-design.md` | PCI transport, virtqueue layout |
| FS1.2 | Implement 9p protocol definitions | NOT STARTED | | | | FS1.1 | `usr.sbin/bhyve/virtio_9p.h` | Protocol constants, message structures |
| FS1.3 | Implement 9p protocol handler | NOT STARTED | | | | FS1.2 | `usr.sbin/bhyve/virtio_9p.c` | Tversion, Tattach, Twalk, Topen, Tread, Twrite, Tclunk, Tstat |
| FS1.4 | Implement PCI virtio-9p device | NOT STARTED | | | | FS1.3 | `usr.sbin/bhyve/pci_virtio_9p.c` | PCI transport layer, virtqueue handling |
| FS1.5 | Add `--share` flag to bhyve | NOT STARTED | | | | FS1.4 | `usr.sbin/bhyve/bhyverun.c` | CLI syntax: `--share host_path:guest_tag[:ro]` |
| FS1.6 | Add `--share` support to `emu` CLI | NOT STARTED | | | | FS1.5 | `usr.sbin/emu/emu_start.c` | Forward share config to bhyve |
| FS1.7 | Write virtio-9p integration tests | NOT STARTED | | | | FS1.6 | `tests/usr.sbin/bhyve/virtio_9p_test.sh` | Mount, read, write, unmount |

### Phase FS2: Filesystem Sharing — ZFS Snapshot Integration

**Objective:** Integrate ZFS snapshots for clean test state management.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| FS2.1 | Implement ZFS snapshot helper in `emu` | NOT STARTED | | | | | `usr.sbin/emu/emu_zfs.c` | `emu_zfs_snapshot()`, `emu_zfs_rollback()`, `emu_zfs_clone()` |
| FS2.2 | Add `--zfs-dataset` flag to `emu init` | NOT STARTED | | | | FS2.1 | `usr.sbin/emu/emu_init.c` | Specify ZFS dataset for VM root |
| FS2.3 | Add `emu snapshot` subcommand for ZFS | NOT STARTED | | | | FS2.2 | `usr.sbin/emu/emu_snapshot.c` | `emu snapshot --name <vm> --zfs` |
| FS2.4 | Add `emu rollback` subcommand | NOT STARTED | | | | FS2.3 | `usr.sbin/emu/emu_rollback.c` | `emu rollback --name <vm> --snapshot <name>` |
| FS2.5 | Write ZFS integration tests | NOT STARTED | | | | FS2.4 | `tests/usr.sbin/emu/zfs_snapshot_test.sh` | Create, rollback, verify clean state |

### Phase D1: Device Emulation — Floppy, CDROM, Network

**Objective:** Add floppy, CDROM/DVD/Blu-ray, and enhanced network emulation.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| D1.1 | Implement floppy disk controller (FDC) | NOT STARTED | | | | | `usr.sbin/bhyve/isa_fdc.c` | Intel 82072AA compatible |
| D1.2 | Add floppy image support to bhyve | NOT STARTED | | | | D1.1 | `usr.sbin/bhyve/bhyverun.c` | `-s <slot>,fdc,<image>` |
| D1.3 | Extend AHCI ATAPI support for CDROM/DVD | NOT STARTED | | | | | `usr.sbin/bhyve/pci_ahci.c` | Ensure ATAPI/MMC commands work |
| D1.4 | Add Blu-ray media size support to AHCI | NOT STARTED | | | | D1.3 | `usr.sbin/bhyve/pci_ahci.c` | Handle >4.7GB media sizes |
| D1.5 | Implement host-only networking mode | NOT STARTED | | | | | `usr.sbin/bhyve/net_backend.c` | Internal bridge without physical NIC |
| D1.6 | Add MAC filtering to virtio-net | NOT STARTED | | | | D1.5 | `usr.sbin/bhyve/pci_virtio_net.c` | Per-VM MAC address whitelist |
| D1.7 | Write device integration tests | NOT STARTED | | | | D1.1–D1.6 | `tests/usr.sbin/bhyve/device_test.sh` | Floppy, CDROM, network tests |

### Phase CA1: Cross-Architecture Emulation Infrastructure

**Objective:** Ensure the emulator can run any architecture on any host architecture.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| CA1.1 | Add host architecture detection to emu framework | NOT STARTED | | | | 2.2 | `sys/emulation/emu_main.c` | Detect host arch at module init |
| CA1.2 | Implement endianness conversion layer | NOT STARTED | | | | | `sys/emulation/emu_endian.c` | `emu_endian_swap()` for cross-endian |
| CA1.3 | Implement architecture-independent register interface | NOT STARTED | | | | 2.9 | `sys/emulation/emu_cpu.c` | `struct emu_cpu_ops` dispatch table |
| CA1.4 | Implement architecture-independent MMU interface | NOT STARTED | | | | 2.10 | `sys/emulation/emu_mem.c` | `struct emu_mmu_ops` dispatch table |
| CA1.5 | Implement architecture-independent boot protocol | NOT STARTED | | | | 5.5 | `usr.sbin/emu/emu_boot.c` | `struct emu_boot_ops` dispatch table |
| CA1.6 | Add cross-arch validation in `emu start` | NOT STARTED | | | | CA1.1 | `usr.sbin/emu/emu_start.c` | Warn when host != target (emulator mode) |
| CA1.7 | Write cross-architecture integration tests | NOT STARTED | | | | CA1.1–CA1.6 | `tests/usr.sbin/emu/cross_arch_test.sh` | Test all host→target combinations |

### Phase CA2: Architecture-Specific Emulation Completion

**Objective:** Complete CPU emulation for all target architectures to support cross-arch testing.

| # | Task | Status | Owner | Start | End | Dependencies | Files | Notes |
|---|------|--------|-------|-------|-----|--------------|-------|-------|
| CA2.1 | Complete amd64 CPU emulation (all common instructions) | NOT STARTED | | | | 3.1 | `sys/emulation/amd64/emu_cpu_amd64.c` | ~200 instructions minimum |
| CA2.2 | Complete arm64 CPU emulation (all common instructions) | NOT STARTED | | | | 3.4 | `sys/emulation/arm64/emu_cpu_arm64.c` | ~150 instructions minimum |
| CA2.3 | Complete riscv64 CPU emulation (RV64IMAFD) | NOT STARTED | | | | 3.7 | `sys/emulation/riscv/emu_cpu_riscv.c` | ~100 instructions minimum |
| CA2.4 | Complete i386 CPU emulation | NOT STARTED | | | | 3.10 | `sys/emulation/i386/emu_cpu_i386.c` | ~150 instructions minimum |
| CA2.5 | Complete arm (32-bit) CPU emulation | NOT STARTED | | | | 3.11 | `sys/emulation/arm/emu_cpu_arm.c` | ARM + Thumb, ~200 instructions |
| CA2.6 | Complete powerpc CPU emulation | NOT STARTED | | | | 3.12 | `sys/emulation/powerpc/emu_cpu_ppc.c` | ~150 instructions minimum |
| CA2.7 | Write per-arch instruction test suites | NOT STARTED | | | | CA2.1–CA2.6 | `tests/sys/emulation/arch_*_test.c` | Verify each instruction produces correct results |

---

## 8. Key Data Structures

### 8.1 Nested Virtualization State

```c
/* Per-VM nested virtualization state */
struct vmm_nested_state {
    int max_depth;              /* Maximum nesting depth */
    int current_depth;          /* Current nesting depth */
    bool enabled;               /* Nested virt enabled for this VM */
    struct vmm_msr_bitmap msr_bitmap; /* MSR pass-through bitmap */
};

/* Per-vCPU nested virtualization state */
struct vcpu_nested_state {
    int nesting_level;          /* 0 = L0, 1 = L1, 2 = L2, etc. */
    uint64_t host_rsp;          /* Host RSP at time of nested entry */
    uint64_t host_rip;          /* Host RIP at time of nested entry */
    struct vmcs_shadow vmcs;    /* VMCS shadow for nested VMX (Intel) */
};
```

### 8.2 Filesystem Share Configuration

```c
/* Filesystem share configuration */
struct emu_fs_share {
    char host_path[PATH_MAX];   /* Path on host */
    char guest_tag[64];         /* 9p tag name in guest */
    bool readonly;              /* Mount read-only */
    uid_t map_uid;              /* UID mapping (optional) */
    gid_t map_gid;              /* GID mapping (optional) */
    LIST_ENTRY(emu_fs_share) entries;
};

/* Per-instance share list */
struct emu_instance {
    ...
    struct emu_fs_share_list shares; /* List of filesystem shares */
    ...
};
```

### 8.3 Cross-Architecture Dispatch Tables

```c
/* Architecture dispatch table */
struct emu_arch_ops {
    const char *name;                    /* Architecture name */
    int endian;                          /* EMU_ENDIAN_LITTLE / _BIG */
    int page_size;                       /* Default page size */
    struct emu_cpu_ops *cpu;             /* CPU emulation ops */
    struct emu_mmu_ops *mmu;             /* MMU emulation ops */
    struct emu_intr_ops *intr;           /* Interrupt controller ops */
    struct emu_boot_ops *boot;           /* Boot protocol ops */
};

/* Registry of all supported architectures */
extern struct emu_arch_ops *emu_arch_registry[];
extern int emu_arch_registry_count;

/* Lookup function */
struct emu_arch_ops *emu_arch_lookup(const char *arch);
```

---

## 9. Sysctl Interface Additions

New sysctl nodes for nested virtualization:

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `hw.vmm.nested_virt` | CTLTYPE_INT | 0 | Enable nested virtualization (master switch) |
| `hw.vmm.nested_max_depth` | CTLTYPE_INT | 1 | Maximum nesting depth |
| `hw.vmm.nested_max_vmexit_rate` | CTLTYPE_INT | 100000 | Max VM exits/sec before throttling |
| `hw.vmm.nested_max_vmcs_pages` | CTLTYPE_INT | 64 | Max VMCS pages L1 can allocate |

---

## 10. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Nested virt exposes new attack surface in bhyve | Critical | Default-off, MSR filtering, CPUID masking, IOMMU isolation |
| Cross-architecture emulation is too slow for practical use | Medium | Use bhyve for native path; optimize hot paths; add JIT later |
| virtio-9p implementation is complex | Medium | Start with basic read/write; add features incrementally |
| Floppy/CDROM emulation has low priority vs. core features | Low | Implement as Phase D1 after core emulation is stable |
| Endianness bugs in cross-arch emulation | Medium | Comprehensive test suite; automated endianness testing |
| ZFS snapshot integration may not work on non-FreeBSD hosts | Low | Document as FreeBSD-only feature; fall back to file copies |
| Nested virt depth >1 causes exponential complexity | High | Limit to depth 1 initially; document as future work |

---

## 11. Future Enhancements

1. **Nested depth >1**: Allow L2 to host L3 (requires significant VMM changes)
2. **JIT compilation for cross-arch emulation**: Dynamic binary translation (like QEMU TCG) to improve performance
3. **virtio-fs support**: Higher-performance alternative to virtio-9p
4. **NVMe emulation**: For faster storage in nested VMs
5. **SR-IOV for nested VMs**: Share physical NIC functions with L1 for better network performance
6. **Live migration of nested VMs**: Migrate L2 between L1 instances
7. **Hardware-assisted nested virt**: Use Intel VMX "VMCS shadowing" and AMD "Nested Page Tables" for better nested performance
8. **Cross-arch snapshot/restore**: Save emulator state on one arch, restore on another
9. **Automated cross-arch CI pipeline**: GitHub Actions matrix testing across all architecture combinations
10. **Performance profiling tools**: Measure emulation overhead per architecture pair

---

## 12. Task Completion Checklist

- [ ] `hw.vmm.nested_virt` sysctl added (default 0)
- [ ] `VM_CAP_NESTED_VIRT` capability added
- [ ] Intel VMX nested virt implementation complete
- [ ] AMD SVM nested virt implementation complete
- [ ] MSR filtering for nested VMs implemented
- [ ] CPUID masking for nested VMs implemented
- [ ] VM-exit rate limiting implemented
- [ ] Passthrough device guard for nested VMs implemented
- [ ] virtio-9p device model implemented
- [ ] `--share` flag added to bhyve and `emu` CLI
- [ ] ZFS snapshot integration for clean test state
- [ ] Floppy disk controller emulation implemented
- [ ] CDROM/DVD/Blu-ray AHCI ATAPI support complete
- [ ] Host-only networking mode implemented
- [ ] Cross-architecture emulation infrastructure complete
- [ ] All 6 target architectures have CPU emulation
- [ ] Endianness conversion layer implemented
- [ ] Architecture-independent register/MMU/boot interfaces implemented
- [ ] Integration tests written and passing
- [ ] Security verification tests written and passing
- [ ] Documentation updated

---

## 13. Conclusion

This plan extends the base emulation framework with four critical capabilities:

1. **Nested virtualization** with two-level control (sysctl + per-VM capability), MSR filtering, CPUID masking, and resource limits — making it safe enough to enable for trusted workloads while keeping it off by default.

2. **Security architecture** that leverages hardware-enforced isolation (EPT/NPT, IOMMU) and adds software mitigations (MSR bitmaps, rate limiting, passthrough guards) to prevent escape from nested VMs.

3. **Filesystem strategy** using virtio-9p for host→guest sharing and ZFS snapshots for clean test state — enabling rapid kernel module iteration without host instability.

4. **Device integration** for floppy, CDROM/DVD/Blu-ray, and network cards — providing complete device models for nested VM environments.

5. **Cross-architecture emulation** where any host architecture can emulate any target architecture — achieved through architecture-independent dispatch tables, endianness conversion, and per-arch CPU/MMU/interrupt emulation modules.

The key architectural insight is that **EPT/NPT provides hardware-enforced isolation** — even a compromised L1 hypervisor cannot access L0 memory because the hardware page tables are controlled by L0. Combined with the software mitigations described here, nested virtualization can be made safe for kernel module development workflows.
