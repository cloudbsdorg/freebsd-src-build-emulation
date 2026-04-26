# Emulation Framework Security — Chapter 5: Additional Security Analysis

> **Part of:** Security chapter series (002a through 002f)
> **See also:** `002a` for threat model, `002c` for custom emulator security, `002f` for implementation phases (S7-S18)

---

## 10. Additional Security Analysis

### 10.1 Audit Logging

Every security-relevant event must be logged for forensic analysis:

**Events to log:**

| Event | Data Captured | Severity |
|-------|---------------|----------|
| Instance create | User, timestamp, arch, memory, cpus | INFO |
| Instance destroy | User, timestamp, instance name | INFO |
| Instance start | User, timestamp, instance name | INFO |
| Instance stop | User, timestamp, instance name | INFO |
| Permission denial | User, operation, instance, reason | WARNING |
| Share mount | User, host_path, guest_path, mode | INFO |
| Module load/unload | User, module name, instance | INFO |
| Crash detected | Instance name, panic string, timestamp | ERROR |
| Capsicum sandbox failure | Instance name, error, degraded mode | WARNING |
| Memory overcommit warning | Instance name, configured, available | WARNING |
| Snapshot create/restore | User, instance, snapshot name | INFO |
| Configuration change | User, sysctl, old value, new value | INFO |

**Log format:**
```
2026-04-26T03:44:00Z emu[1234]: INSTANCE_CREATE user=mark arch=amd64 memory=1024 name=test-vm
2026-04-26T03:44:01Z emu[1234]: PERM_DENIED user=alice operation=DESTROY instance=test-vm reason=EPERM
```

**Sysctl controls:**

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.audit.enabled` | CTLTYPE_INT | 1 | Enable audit logging |
| `kern.emulation.audit.destination` | CTLTYPE_STRING | "syslog" | Log destination: "syslog", "file", "both" |
| `kern.emulation.audit.file` | CTLTYPE_STRING | "/var/log/emu-audit.log" | Log file path (when destination=file or both) |
| `kern.emulation.audit.rotation_size` | CTLTYPE_INT | 10485760 | Log rotation size in bytes (10MB default) |
| `kern.emulation.audit.rotation_count` | CTLTYPE_INT | 5 | Number of rotated log files to keep |

### 10.2 MAC Framework Integration

The emulation framework must integrate with FreeBSD's TrustedBSD MAC framework:

| MAC Policy | Integration Point | Enforcement |
|------------|-------------------|-------------|
| `mac_bsdextended` (ugidfw) | File system access for disk images, snapshots | Standard file permissions apply |
| `mac_mls` (Multi-Level Security) | Instance labels, data classification | Label instances at creation, enforce on share/snapshot |
| `mac_partition` | Process partitioning | Emulator process assigned to partition |
| `mac_veriexec` | Binary integrity | Verify emu binary, kernel modules before execution |
| `mac_seeotheruids` | Process visibility | Instance processes hidden from non-owners |

**MAC label propagation:**
```c
/* Apply MAC label from creating process to instance */
static int
emu_instance_apply_mac_label(struct emu_instance *inst, struct ucred *cred)
{
    struct mac_label *label;

    /* Get label from creating process */
    if (mac_cred_label_get(cred, &label) != 0)
        return (0);  /* No label — continue without MAC */

    /* Store label in instance */
    inst->mac_label = label;

    /* Apply label to instance files */
    mac_file_label_set(inst->instance_dir, label);
    mac_file_label_set(inst->disk_image_path, label);

    return (0);
}
```

### 10.3 Securelevel Integration

When `kern.securelevel` is raised, certain operations must be restricted:

| Securelevel | Restricted Operations | Rationale |
|-------------|----------------------|-----------|
| -1 (Permanently insecure) | None | Development mode |
| 0 (Default) | None | Normal operation |
| 1 (Secure) | Module load/unload, `/dev/emu` write, sysctl modification | Prevent kernel module tampering |
| 2 (Highly secure) | All of level 1 + disk image writes, snapshot creation | Prevent persistent state modification |
| 3 (Network secure) | All of level 2 + network mode changes | Prevent network configuration changes |

**Securelevel check:**
```c
static int
emu_securelevel_check(int operation)
{
    int securelevel = 0;

    TUNABLE_INT_FETCH("kern.securelevel", &securelevel);

    switch (securelevel) {
    case 1:
        /* Level 1: restrict module operations, device writes, sysctl changes */
        if (operation == EMU_OP_MODULE_LOAD ||
            operation == EMU_OP_MODULE_UNLOAD ||
            operation == EMU_OP_DEV_WRITE ||
            operation == EMU_OP_SYSCTL_WRITE)
            return (EPERM);
        break;
    case 2:
        /* Level 2: also restrict disk writes, snapshots */
        if (operation == EMU_OP_DISK_WRITE ||
            operation == EMU_OP_SNAPSHOT_CREATE)
            return (EPERM);
        break;
    case 3:
        /* Level 3: also restrict network changes */
        if (operation == EMU_OP_NETWORK_CHANGE)
            return (EPERM);
        break;
    }

    return (0);
}
```

### 10.4 Memory Scrubbing

When an instance is destroyed, its memory could contain sensitive data:

```c
/* Zero out guest memory on instance destroy */
static void
emu_scrub_memory(struct emu_instance *inst)
{
    if (!memory_scrub_enabled)
        return;

    /* Overwrite guest memory with zeros */
    explicit_bzero(inst->guest_mem, inst->mem_size);

    /* If mlock'd, unlock after scrubbing */
    if (inst->memory_mlocked)
        munlock(inst->guest_mem, inst->mem_size);
}
```

**Sysctl controls:**

| Sysctl | Type | Default | Description |
|--------|------|---------|-------------|
| `kern.emulation.memory_scrub` | CTLTYPE_INT | 1 | Enable memory scrubbing on instance destroy |
| `kern.emulation.memory_scrub_method` | CTLTYPE_STRING | "zero" | Scrubbing method: "zero", "random", "pattern" |

### 10.5 Core Dump Security

Emulator core dumps could contain guest memory:

```c
/* Prevent core dumps from containing guest memory */
static void
emu_disable_coredump(struct emu_instance *inst)
{
    /* Set resource limit to 0 — no core dumps */
    struct rlimit rl = {0, 0};
    setrlimit(RLIMIT_CORE, &rl);

    /* Also use procctl for additional protection */
    int mode = PROC_COREDUMP_CTL_DISABLE;
    procctl(P_PID, 0, PROC_COREDUMP_CTL, &mode);
}
```

### 10.6 ptrace Attack Surface

A debugger could attach to the emulator process and read guest memory:

```c
/* Prevent ptrace to emulator process */
static void
emu_disable_ptrace(struct emu_instance *inst)
{
    /* Disable ptrace for non-root users */
    int mode = PROC_TRACE_CTL_DISABLE;
    procctl(P_PID, 0, PROC_TRACE_CTL, &mode);

    /* Also disable for root (defense in depth) */
    mode = PROC_TRACE_CTL_DISABLE_EXEC;
    procctl(P_PID, 0, PROC_TRACE_CTL, &mode);
}
```

### 10.7 TOCTOU Race Conditions

Time-of-check-time-of-use vulnerabilities in permission checks:

| Race Condition | Window | Mitigation |
|----------------|--------|------------|
| Permission check → operation | Instance destroyed between check and operation | Lock instance mutex across check+operation |
| Ownership check → access | Ownership changes between check and access | Use ucred snapshot at check time, validate against locked instance |
| Resource limit check → allocation | Memory freed between check and allocation | Check and allocate under same lock |
| Instance lookup → use | Instance removed from registry between lookup and use | Reference counting on instance struct |

**Atomic operation pattern:**
```c
static int
emu_atomic_operation(struct emu_instance *inst, struct ucred *cred,
                     int operation)
{
    int error;

    mtx_lock(&inst->lock);

    /* Permission check and operation are atomic under lock */
    error = emu_check_perm(inst, cred, operation);
    if (error != 0) {
        mtx_unlock(&inst->lock);
        return (error);
    }

    /* Perform operation — instance cannot change under lock */
    error = emu_perform_operation(inst, operation);

    mtx_unlock(&inst->lock);
    return (error);
}
```

### 10.8 Signal Handling Security

Signals could be used to inject unexpected behavior:

| Signal | Source | Handler Action | Security |
|--------|--------|----------------|----------|
| SIGSEGV | Guest triggers invalid memory access | Log error, capture crash dump, terminate cleanly | Safe — no guest code execution |
| SIGPIPE | Console pipe closed | Log warning, continue emulation | Safe — handled gracefully |
| SIGTERM | User requests stop | Save state if snapshot enabled, terminate | Safe — controlled shutdown |
| SIGINT | User interrupts | Same as SIGTERM | Safe |
| SIGHUP | Terminal closed | Log warning, continue (daemon mode) | Safe |
| SIGUSR1 | Internal timer | Trigger watchdog check | Safe |
| SIGUSR2 | Internal | Trigger snapshot | Safe |

**Signal handler safety:**
```c
/* Signal handler — only set flags, never call non-reentrant functions */
static volatile sig_atomic_t emu_sig_caught = 0;

static void
emu_signal_handler(int sig)
{
    /* Only set flag — actual handling in main loop */
    emu_sig_caught = sig;
}

/* Main loop checks signal flag */
static int
emu_check_signals(struct emu_instance *inst)
{
    int sig = emu_sig_caught;
    if (sig == 0)
        return (0);

    emu_sig_caught = 0;

    switch (sig) {
    case SIGSEGV:
        emu_crash_capture(inst, "SIGSEGV in emulator");
        return (EMU_ERR_CRASH);
    case SIGTERM:
    case SIGINT:
        return (EMU_ERR_STOP_REQUESTED);
    case SIGPIPE:
        /* Console disconnected — continue without console */
        inst->console_fd = -1;
        return (0);
    }
    return (0);
}
```

### 10.9 OOM Killer Interaction

The OOM killer could target the emulator process:

```c
/* Adjust OOM score to protect emulator process */
static void
emu_adjust_oom_score(struct emu_instance *inst)
{
    /* Set OOM score adjustment via procctl */
    int oom_adj = PROC_OOMADJ_MIN;  /* Least likely to be killed */
    procctl(P_PID, 0, PROC_OOMADJ_CTL, &oom_adj);
}
```

**OOM protection strategy:**
- Emulator processes get minimum OOM score (least likely to be killed)
- If OOM is unavoidable, balloon driver proactively releases memory
- `memory_overcommit` sysctl (default 0) prevents overcommit in the first place
- Host capacity check on instance start prevents starting when insufficient memory

### 10.10 Entropy/RNG

The guest needs a source of randomness:

```c
/* virtio-rng device — provides entropy to guest */
static int
emu_virtio_rng_read(struct emu_device *dev, uint64_t gaddr, size_t len)
{
    uint8_t buf[256];
    size_t chunk;

    while (len > 0) {
        chunk = min(len, sizeof(buf));
        arc4random_buf(buf, chunk);
        emu_mem_write(dev->instance, gaddr, buf, chunk);
        gaddr += chunk;
        len -= chunk;
    }

    return (0);
}
```

**Entropy sources:**
- Host `/dev/random` (via `arc4random_buf()`)
- virtio-rng device for guest consumption
- Entropy is never exhausted — host provides unlimited random bytes

### 10.11 Supply Chain Security

Ensuring the emulator binaries themselves are trustworthy:

| Measure | Implementation | Priority |
|---------|----------------|----------|
| Signed kernel modules | All `emu_*.ko` modules signed with FreeBSD's MODULE_VERIFICATION | P0 |
| Signed userland binary | `emu` binary signed with FreeBSD signing infrastructure | P1 |
| Reproducible builds | Build system produces bit-identical binaries from same source | P1 |
| Build attestation | Build logs and signatures recorded for each release | P2 |
| Dependency scanning | Scan all dependencies for known vulnerabilities | P1 |
| SBOM generation | Generate Software Bill of Materials for each release | P2 |

### 10.12 Firmware Security

Firmware blobs (SeaBIOS, OVMF, U-Boot, OpenSBI) must be verified before loading:

| Concern | Mitigation |
|---------|------------|
| Malicious firmware | GPG signature verification before loading |
| Tampered firmware | SHA-256 checksum verification against known-good values |
| Outdated firmware | Version checking against known-vulnerable versions |
| Firmware from untrusted source | Only download from verified mirrors, validate certificate chain |
| Firmware in cache | Periodic integrity verification of cached blobs |

```c
/* Verify firmware blob before loading */
static int
emu_verify_firmware(const char *path, const char *expected_sha256)
{
    unsigned char actual_sha256[SHA256_DIGEST_LENGTH];
    char actual_hex[SHA256_DIGEST_HEX_LENGTH + 1];

    /* Compute SHA-256 of firmware blob */
    if (!emu_compute_sha256(path, actual_sha256))
        return (EMU_ERR_HASH_FAILED);

    /* Convert to hex string */
    emu_sha256_to_hex(actual_sha256, actual_hex);

    /* Compare against expected */
    if (strcasecmp(actual_hex, expected_sha256) != 0)
        return (EMU_ERR_HASH_MISMATCH);

    /* Verify GPG signature if available */
    if (emu_gpg_verify(path) != 0)
        return (EMU_ERR_GPG_FAILED);

    return (0);
}
```
