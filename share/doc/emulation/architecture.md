# FreeBSD Emulation Framework Architecture

## Overview

The FreeBSD emulation framework provides a unified mechanism for running emulated instances of different architectures on a FreeBSD host. It supports both native virtualization via bhyve/VMM and pure software emulation for cross-architecture testing.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Space                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   emu (CLI tool)                      │   │
│  │  init | start | stop | status | load | test | ...   │   │
│  └─────────────────────────────────────────────────────┘   │
│              │                  │                 │          │
└──────────────┼──────────────────┼─────────────────┼────────┘
               │                  │                 │
               │    ioctl/Unix    │    socket       │
               │      socket      │                 │
┌──────────────┼──────────────────┼─────────────────┼────────┐
│              ▼                  ▼                 ▼        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              emu_core.ko (Core Module)               │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │   │
│  │  │   Instance   │  │   Memory     │  │  Audit    │  │   │
│  │  │  Manager     │  │  Management  │  │  Logger   │  │   │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │   │
│  │  │  Resource    │  │  Security    │  │  Sysctl   │  │   │
│  │  │  Limits      │  │  Sandbox     │  │  Interface│  │   │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
│              │                  │                 │          │
└──────────────┼──────────────────┼─────────────────┼──────────┘
               │                  │                 │
               │    module        │    module       │
               │    linkage       │    linkage      │
┌──────────────┼──────────────────┼─────────────────┼──────────┐
│              ▼                  ▼                 ▼          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ emu_amd64.ko│  │ emu_arm64.ko│  │   emu_<arch>.ko    │ │
│  │   (x86-64)  │  │  (AArch64)  │  │  (other archs)     │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
│                                                               │
│                      Kernel Space                             │
└───────────────────────────────────────────────────────────────┘
```

## Components

### Core Module (emu_core.ko)

The core module provides:

- **Instance Manager**: Creates, tracks, and destroys emulated instances
- **Memory Management**: Allocates and manages guest memory with policy controls
- **Resource Limits**: Enforces per-instance and per-user resource quotas
- **Security Sandbox**: Capsicum-based isolation for emulator processes
- **Audit Logger**: Records security-relevant events
- **Sysctl Interface**: Runtime configuration and monitoring

### Architecture Modules (emu_*.ko)

Each architecture module provides:

- **CPU Emulation**: Instruction decoding and execution
- **Register State**: Architecture-specific register files
- **Memory Translation**: Guest virtual to host physical address translation
- **Exception Handling**: Traps, interrupts, and fault handling
- **Device Emulation**: Architecture-specific devices ( UART, timers, etc.)

### Userland Tool (emu)

The CLI tool provides:

- **Instance Lifecycle**: init, start, stop, destroy commands
- **Module Loading**: Load/unload kernel modules into instances
- **Console Access**: Connect to instance serial console
- **Snapshot Management**: Create and restore VM snapshots
- **Blob Management**: Download and verify firmware blobs
- **Output Formatting**: text, JSON, TAP, JUnit output formats

## Instance Lifecycle

```
┌────────────┐    init     ┌─────────────┐   start   ┌──────────┐
│            │ ──────────► │             │ ─────────►│          │
│   None     │             │   STOPPED   │            │ RUNNING  │
│            │             │             │  ◄─────────│          │
└────────────┘             └─────────────┘   stop    └──────────┘
                                 │                      │
                                 │ destroy              │ error
                                 ▼                      ▼
                           ┌──────────┐          ┌──────────┐
                           │          │          │          │
                           │ DESTROYED│          │  ERROR   │
                           │          │          │          │
                           └──────────┘          └──────────┘
```

## Memory Management

The framework supports two memory allocation policies:

### Pre-alloc (default)

All guest memory is allocated upfront using `malloc(M_WAITOK)`. This provides predictable memory usage but may fail if insufficient memory is available.

### Demand Paging

Memory is mapped using `mmap()` with `MAP_NORESERVE`. Pages are faulted in on first access. This allows running more instances but may encounter ENOMEM under heavy load.

### Memory Overcommit

When enabled (default: disabled):

- **Warn (1)**: Log warning when memory exceeds threshold
- **Silent (2)**: Allow overcommit without logging

### Memory Ballooning

The framework can dynamically adjust guest memory:

- Balloon can be inflated/deflated to reclaim or allocate memory
- Minimum balloon size configurable (default: 50% of allocated)
- Adjustment interval configurable (default: 30 seconds)

## Security Architecture

### Capsicum Sandboxing

Emulator processes are sandboxed using Capsicum:

- File descriptor rights are limited to required operations
- System call capabilities are restricted
- Network access can be disabled

### Privilege Dropping

After initial setup:

1. Process retains CAP_SYS_RESOURCE for OOM score adjustment
2. All other privileges are dropped
3. Process runs with reduced capabilities

### Securelevel Interaction

At securelevel >= 1:

- Kernel module loading is restricted
- Resource limit modifications are blocked
- Audit configuration changes are prevented

## Execution Modes

### Auto (default)

Automatically selects the best execution mode:

- For native architectures (matching host): Use bhyve if available
- For foreign architectures: Use software emulator

### Bhyve

Uses bhyve/VMM for virtualization:

- Requires VT-x/AMD-V hardware support
- Best performance for native architectures
- Not available for non-x86 architectures

### Emulator

Uses the framework's software emulator:

- Works on all architectures
- Slower than bhyve but more portable
- Required for cross-architecture testing

## Sysctl Hierarchy

```
kern.emulation
├── allow_nonroot              (int) Allow non-root users
├── modules_loaded            (string) Comma-separated module list
├── instance_count            (int) Number of active instances
├── max_instances             (int) Maximum concurrent instances
├── max_memory_per_instance   (int) Max memory per instance (MB)
├── max_instances_per_user    (int) Max instances per user
├── max_memory_per_user       (int) Max memory per user (MB)
├── max_cpu_time_per_instance  (int) Max CPU time (seconds, 0=unlimited)
├── sandbox_capsicum          (int) Enable Capsicum sandboxing
├── sandbox_strict            (int) Fail if Capsicum unavailable
├── drop_privileges           (int) Drop root privileges after setup
├── watchdog_seconds          (int) Watchdog timeout
├── securelevel_restrictions  (int) Enable securelevel-aware restrictions
├── memory
│   ├── policy                (string) prealloc|demand
│   ├── overcommit            (int) 0=disabled, 1=warn, 2=silent
│   ├── warn_percent          (int) Warning threshold (%)
│   ├── balloon_min_pct       (int) Minimum balloon size (%)
│   ├── balloon_interval      (int) Adjustment interval (seconds)
│   ├── system_reserve_percent (int) System reserve (%)
│   ├── total                (int) Total physical memory (bytes)
│   ├── system_used          (int) System used memory (bytes)
│   ├── available            (int) Available memory (bytes)
│   ├── instances_used       (int) Total instance memory (bytes)
│   └── scrub
│       ├── enabled          (int) Enable memory scrubbing
│       └── method           (string) zero|random|pattern
├── audit
│   ├── enabled              (int) Enable audit logging
│   ├── destination          (int) 0=none, 1=syslog, 2=file, 3=both
│   ├── file                 (string) Log file path
│   ├── rotation_size        (int) Rotation size (bytes)
│   ├── rotation_count       (int) Number of rotated files
│   ├── min_severity         (int) Minimum severity (0-7)
│   └── event_count          (int) Total events logged
├── module.<name>
│   ├── version              (int) Module version
│   └── refcount            (int) Reference count
└── instance.<name>
    ├── state                (string) Instance state
    ├── arch                 (string) Target architecture
    ├── memory_used          (int) Memory usage (bytes)
    └── cpu_time             (int) CPU time (nanoseconds)
```

## Error Handling

The framework uses structured error codes:

```c
enum emu_error {
    EMU_SUCCESS = 0,
    EMU_ERR_NO_MEMORY = 1,
    EMU_ERR_NO_INSTANCES = 2,
    EMU_ERR_NOT_RUNNING = 3,
    EMU_ERR_ALREADY_RUNNING = 4,
    EMU_ERR_INVALID_ARCH = 5,
    EMU_ERR_PERMISSION = 6,
    EMU_ERR_RESOURCE_LIMIT = 7,
    EMU_ERR_MODULE_NOT_FOUND = 8,
    EMU_ERR_TIMEOUT = 9,
    EMU_ERR_SECURELEVEL = 10,
};
```

## Performance Considerations

1. **Memory**: Pre-alloc policy uses more memory but has more predictable latency
2. **CPU**: Software emulation is 10-100x slower than native execution
3. **I/O**: Emulated devices add latency; use virtio when possible
4. **Concurrency**: Multiple instances share host resources

## Testing

The framework includes comprehensive tests:

- **Unit Tests**: Individual component testing
- **Integration Tests**: Full instance lifecycle
- **Stress Tests**: Resource limit enforcement
- **Security Tests**: Sandbox isolation verification

See [tests/sys/emulation/](tests/sys/emulation/) for test code.
