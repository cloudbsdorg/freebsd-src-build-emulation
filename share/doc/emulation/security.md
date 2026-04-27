# Emulation Framework Security Documentation

## Overview

This document describes the security architecture, threat model, and operational guidelines for the FreeBSD emulation framework. The framework supports running emulated instances of multiple CPU architectures (amd64, i386, arm64, arm, PowerPC, RISC-V) with strong security isolation from the host system.

## Threat Model

### Assets to Protect

1. **Host kernel integrity**: Prevent guest code from compromising kernel stability or security
2. **Host filesystem**: Prevent unauthorized access to host files and directories
3. **Host network**: Prevent guest from accessing network resources without explicit authorization
4. **Host processes**: Prevent guest from interfering with or observing other host processes
5. **Instance isolation**: Prevent one emulation instance from accessing another instance's resources
6. **Sensitive data**: Prevent leakage of cryptographic keys, credentials, or other sensitive information

### Threat Actors

1. **Malicious guest code**: Intentionally crafted binaries attempting to escape emulation
2. **Compromised guest OS**: Guest operating system that has been subverted
3. **Malicious users**: Unauthorized users attempting to access emulation resources
4. **Privilege escalation**: Attempts to gain elevated privileges on the host

### Attack Vectors

1. **Memory corruption**: Buffer overflows, use-after-free, integer overflows in emulator
2. **MMIO attacks**: Malicious device register accesses to corrupt emulator state
3. **DMA attacks**: Direct memory access attempts to host memory
4. **Side-channel attacks**: Timing, cache, or power analysis attacks
5. **Resource exhaustion**: DoS attacks consuming host resources
6. **Escape attempts**: Breaking out of emulation sandbox to host system

## Security Architecture

### Defense in Depth

The emulation framework employs multiple layers of security:

#### Layer 1: Access Control

- **Root-only by default**: Emulation framework requires root privileges unless explicitly configured otherwise
- **Group delegation**: `GID_EMU` (979) allows delegated access to emulation framework
- **Per-instance ownership**: Each instance owned by creating user's credentials
- **Granular permissions**: Separate permissions for create/destroy/modify/admin/audit operations
- **Jail integration**: Respects FreeBSD jail boundaries with `PR_ALLOW_EMULATION` flag

#### Layer 2: Resource Limits

- **Per-user instance limits**: Maximum instances per user (default: 8)
- **Memory limits**: Maximum memory per instance (default: 16GB)
- **CPU time limits**: Maximum CPU time per instance (default: 24 hours)
- **Global limits**: System-wide instance and memory caps

#### Layer 3: Memory Safety

- **Bounds checking**: All guest memory accesses validated against allocated regions
- **Integer overflow protection**: All size calculations checked for overflow
- **Permission validation**: Read/write/execute permissions enforced per region
- **Alignment validation**: Misaligned accesses detected and handled safely

#### Layer 4: Input Validation

- **ELF validation**: Comprehensive ELF header and segment validation before loading
- **Instruction decoder safety**: Bounds-checked instruction fetch, 15-byte max length
- **MMIO validation**: All device register accesses validated for bounds and permissions
- **Path validation**: `realpath()` resolution with blocked prefixes for filesystem sharing

#### Layer 5: Sandboxing

- **Capsicum capability mode**: Emulator enters capability mode after initialization
- **FD rights limiting**: File descriptor rights restricted to minimum required
- **ioctl restriction**: Only essential ioctls permitted on VMM FD
- **Non-essential FD closure**: All unnecessary file descriptors closed before sandbox

#### Layer 6: Device Isolation

- **Minimal device emulation**: Only essential devices emulated
- **Console-only UART**: UART output goes only to instance console buffer
- **Host-only networking**: Default network mode prevents external access
- **GDB localhost-only**: Debug stub binds only to 127.0.0.1

#### Layer 7: Visibility Control

- **cr_cansee() integration**: Users can only see instances they own or have permission for
- **Instance filtering**: `emu_find_instance()` applies visibility checks
- **Jail awareness**: Jailed processes restricted to jail-scoped instances

## Security Controls

### Access Control Primitives

| Control | Implementation | Location |
|---------|---------------|----------|
| Root-only default | `kern.emulation.allow_nonroot` sysctl (default 0) | `emu_sysctl.c` |
| Group delegation | `GID_EMU` (979) in `conf.h` | `sys/sys/conf.h` |
| Privileges | `PRIV_EMU_*` (720-725) in `priv.h` | `sys/sys/priv.h` |
| Per-instance ownership | `inst_uid` from creating process credentials | `emu_instance.c` |
| Permission checks | `emu_check_priv()`, `emu_check_access()` | `emu_access.c` |
| Jail integration | `PR_ALLOW_EMULATION` flag, `prison_emulation_allowed()` | `kern_jail.c` |

### Memory Safety Controls

| Control | Implementation | Location |
|---------|---------------|----------|
| Bounds checking | `emu_mem_check_bounds()` | `emu_engine.c` |
| Safe memory accessors | `emu_mem_read8/16/32/64()`, `emu_mem_write8/16/32/64()` | `emu_engine.c` |
| Endianness awareness | `emu_mem_read_le/be16/32/64()` | `emu_mem.h` |
| Region permissions | `EMU_MEM_READ`, `EMU_MEM_WRITE`, `EMU_MEM_EXEC` flags | `emu_engine.h` |

### Input Validation Controls

| Control | Implementation | Location |
|---------|---------------|----------|
| ELF validation | `emu_elf_validate_header()`, `emu_elf_validate_segments()` | `emu_boot.c` |
| Instruction decoder | `emu_decode_instruction()` with bounds checking | `emu_decoder.c` |
| MMIO validation | `emu_mmio_read/write*()` with region validation | `emu_mmio.c` |
| Path validation | `emu_validate_share_path()` with `realpath()` | `emu_file_sharing.c` |

### Sandboxing Controls

| Control | Implementation | Location |
|---------|---------------|----------|
| Capability mode | `cap_enter()` after initialization | `emu_engine.c`, `emu_bhyve.c` |
| FD rights limiting | `cap_rights_limit()` per FD type | `emu_engine.c` |
| ioctl restriction | `cap_ioctls_limit()` to essential ioctls | `emu_engine.c` |
| Non-essential FD closure | Close FDs 3 to `OPEN_MAX` except stdio | `emu_engine.c` |

## Operational Guidelines

### For System Administrators

#### Enabling Non-Root Access

By default, only root can use the emulation framework. To enable non-root access:

```bash
# Enable non-root access (use with caution)
sysctl kern.emulation.allow_nonroot=1

# Make persistent across reboots
echo "kern.emulation.allow_nonroot=1" >> /etc/sysctl.conf
```

**Warning**: Enabling non-root access increases attack surface. Only enable in trusted environments.

#### Creating Emulation Group

To delegate emulation access to specific users:

```bash
# Create emu group (GID 979)
pw groupadd emu -g 979

# Add users to emu group
pw groupmod emu -m user1,user2
```

Users in the `emu` group can create and manage instances without root privileges.

#### Setting Resource Limits

```bash
# Set system-wide instance limit
sysctl kern.emulation.max_instances=64

# Set per-user instance limit
sysctl kern.emulation.max_instances_per_user=8

# Set memory limit per instance (in bytes)
sysctl kern.emulation.max_memory_per_instance=17179869184  # 16GB

# Set CPU time limit per instance (in seconds)
sysctl kern.emulation.max_cpu_time_per_instance=86400  # 24 hours
```

#### Monitoring Emulation Activity

```bash
# View loaded emulation modules
sysctl kern.emulation.modules_loaded

# View instance count
sysctl kern.emulation.instance_count

# View per-instance details
sysctl kern.emulation.instance.<name>.*
```

### For Developers

#### Security Best Practices

1. **Never trust guest input**: All guest-provided data must be validated
2. **Check bounds on every access**: Never assume offsets or sizes are valid
3. **Use safe integer arithmetic**: Check for overflow on all calculations
4. **Minimize attack surface**: Only implement essential features
5. **Fail safely**: On error, terminate cleanly rather than continuing in undefined state
6. **Log security events**: All permission denials and validation failures should be logged

#### Adding New Device Emulations

When implementing new device emulations:

1. **Register MMIO region**: Use `emu_mmio_register_region()` with explicit bounds
2. **Validate all register accesses**: Check offset, size, and alignment in read/write handlers
3. **Implement minimal functionality**: Only emulate registers required for guest operation
4. **No host resource access**: Device should not access host filesystem, network, or processes
5. **Test with fuzzing**: Use fuzz testing to validate robustness

#### Testing Security Controls

```bash
# Run security unit tests
cd /usr/tests/sys/emulation
kyua test security_test

# Run device security tests
cd /usr/tests/usr.sbin/emu
kyua test device_security_test

# Run Capsicum integration tests
cd /usr/tests/usr.sbin/emu
kyua test capsicum_integration_test

# Run fuzz tests
cd /usr/tests/sys/emulation
./fuzz_decoder
```

## Incident Response

### Suspected Escape Attempt

If an emulation instance is suspected of attempting to escape:

1. **Isolate immediately**: Stop the instance
   ```bash
   emu stop <instance_name>
   ```

2. **Preserve evidence**: Capture instance state and logs
   ```bash
   emu stack <instance_name> --output /tmp/escape_evidence.json
   emu console <instance_name> --dump > /tmp/console.log
   ```

3. **Analyze crash state**: Review captured state for attack indicators
   - Check for unusual MMIO accesses
   - Review instruction stream at crash point
   - Examine memory regions for corruption

4. **Report**: Document findings and report to security team

### Vulnerability Disclosure

To report a security vulnerability in the emulation framework:

1. **Do not disclose publicly**: Responsible disclosure only
2. **Contact**: Send details to security@freebsd.org
3. **Include**: Reproduction steps, impact assessment, suggested fix if available
4. **Timeline**: Allow 90 days for patch development before public disclosure

### Security Updates

Security updates for the emulation framework are distributed via:

- **FreeBSD Security Advisories**: SA-YY:NN format
- **FreeBSD Errata Notices**: EN-YY:NN format
- **GitHub releases**: For development versions

Apply security updates promptly:

```bash
# Update FreeBSD source tree
cd /usr/src
git pull

# Rebuild and install emulation modules
cd sys/modules/emu
make clean && make && make install

# Rebuild and install userland tools
cd /usr.sbin/emu
make clean && make && make install
```

## Audit Logging

### Enabling Audit Logging

```bash
# Enable audit logging
sysctl kern.emulation.audit.enabled=1

# Set audit destination (syslog, file, or both)
sysctl kern.emulation.audit.destination="syslog,file"

# Set audit log file path
sysctl kern.emulation.audit.file="/var/log/emu_audit.log"

# Configure log rotation
sysctl kern.emulation.audit.rotation_size=104857600  # 100MB
sysctl kern.emulation.audit.rotation_count=5
```

### Audited Events

The following events are logged:

- Instance create/destroy/start/stop
- Permission denials
- Share mount/unmount
- Crashes and exceptions
- Capsicum sandbox failures
- Resource limit violations
- Configuration changes
- Module load/unload

### Reviewing Audit Logs

```bash
# View recent audit events
tail -f /var/log/emu_audit.log

# Search for permission denials
grep "PERM_DENIED" /var/log/emu_audit.log

# Search for crashes
grep "CRASH" /var/log/emu_audit.log

# Generate audit report
emu audit --report --from 2026-01-01 --to 2026-01-31
```

## Compliance Considerations

### Common Criteria

The emulation framework is designed to support Common Criteria evaluation:

- **Security Target**: Defined security functions and assurance requirements
- **TOE Security Functions**: Access control, identification/authentication, audit, resource management
- **Assurance Level**: Targeting EAL4+ with formal security policy model

### FIPS 140-3

When used with FIPS-validated cryptographic modules:

- Guest cryptographic operations are not covered by FIPS validation
- Host cryptographic operations must use FIPS-validated modules
- Key material must not be exposed to guest instances

## References

- FreeBSD Capsicum documentation: https://www.freebsd.org/doc/handbook/security-capsicum.html
- FreeBSD Jail documentation: https://www.freebsd.org/doc/handbook/jails.html
- FreeBSD Privilege documentation: https://man.freebsd.org/cgi/man.cgi?query=priv
- bhyve architecture: https://bhyve.org/
- MMIO security best practices: See `share/doc/emulation/mmio_security_audit.md`

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-04-27 | freedev002 | Initial security documentation |
