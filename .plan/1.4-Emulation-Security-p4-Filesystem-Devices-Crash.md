# Emulation Framework Security — Chapter 4: Filesystem, Devices & Crash Safety

> **Part of:** Security chapter series (002a through 002f)
> **See also:** `002a` for isolation architecture, `002c` for custom emulator security, `002f` for implementation phases (S3, S4)

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

The custom emulator implements file sharing by intercepting guest filesystem calls and translating them to host file operations:

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
| Sound Blaster 16 | ~16 registers | Medium — DMA, IRQ | Medium | Validate DMA buffer addresses |
| Intel HDA | ~64 registers | High — complex DMA engine | High | Validate stream descriptor tables, BDL entries |
| USB xHCI | ~200+ registers | High — complex state machine | High | Validate endpoint contexts, TRB rings |
| NE2000 | ~16 registers | Low — simple FIFO | Low | Well-understood |
| e1000 | ~100+ registers | High — complex descriptor rings | High | Validate descriptor addresses, lengths |

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
