# FreeBSD Emulation Framework Developer Documentation

This directory contains developer documentation for the FreeBSD emulation framework.

## Contents

- [Architecture Overview](architecture.md) - High-level design and component interaction
- [API Reference](api.md) - Kernel module API for extending the framework
- [Porting Guide](porting.md) - Adding support for new architectures
- [Device Model](devices.md) - Emulated device implementation guide
- [GDB Stub Protocol](gdb.md) - Debugging interface specification

## Quick Start

### Building

```bash
# Build with emulation framework
cd sys/compile/MYCONFIG
make MK_EMULATION=yes buildworld
make MK_EMULATION=yes installworld

# Build tests
cd tests
make MK_EMULATION=yes
```

### Loading Modules

```bash
# Load all emulation modules
kldload emu

# Or load individual modules
kldload emu_core
kldload emu_amd64
```

### Basic Usage

```bash
# Create an instance
emu init -a amd64 -m 512M -n mytest

# Start it
emu start -n mytest

# Load a kernel module for testing
emu load -n mytest /path/to/test_module.ko

# Run tests
emu test -n mytest

# Stop and clean up
emu stop -n mytest
emu destroy -n mytest
```

## Architecture Overview

The emulation framework consists of:

1. **Core Module (emu_core.ko)** - Instance management, resource limits, sysctl interface
2. **Architecture Modules (emu_*.ko)** - Architecture-specific emulation
3. **Userland Tool (emu)** - CLI for instance management
4. **Kernel Tests** - Framework validation tests

See [architecture.md](architecture.md) for detailed design documentation.

## Contributing

- All code must have BSD 2-Clause license headers
- Follow FreeBSD style guidelines (see style(9))
- Add tests for new functionality
- Update documentation as needed

## Reporting Issues

Report bugs at: https://github.com/freebsd/freebsd-src/issues
