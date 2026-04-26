# AGENTS START HERE — Kernel Emulation Framework for FreeBSD

> **Purpose:** This is the primary entry point for autonomous agents working on the FreeBSD Kernel Emulation Framework. Read this file **first** before consuming any other documents in the `.plan/` directory.

> **FreeBSD:** The environment in which this work is being done may have elements that state that you are in linux, that would be false. You are running in FreeBSD.


---

## What We're Building

A **kernel emulation framework** for FreeBSD that allows:

- **Testing kernel modules** in a safe, isolated environment without rebooting the host
- **Emulating multiple CPU architectures** (amd64, i386, arm64, arm, PowerPC, RISC-V) via a custom emulator engine
- **Running unmodified FreeBSD kernels** as emulated instances for development and testing
- **Safe filesystem sharing** between host and emulated instances for rapid kernel module iteration
- **Granular access control** — root-only by default, with group delegation and fine-grained permissions

The framework has **two execution paths**:
1. **bhyve/VMM path** — Native amd64-on-amd64 using the existing bhyve hypervisor
2. **Custom emulator path** — Cross-architecture emulation via a software emulator engine

---

## Document Structure

All plan documents are in the `.plan/` directory, numbered using the `<Major>.<Minor>` convention:

| # | File | What It Covers |
|---|------|----------------|
| `0.0` | [`0.0-Emulation-TOC.md`](.plan/0.0-Emulation-TOC.md) | Master table of contents with clickable links to all documents |
| `0.1` | [`0.1-Emulation-Workflow.md`](.plan/0.1-Emulation-Workflow.md) | **Read this first** — task claiming, completion, merge conflict handling |
| `1.0` | [`1.0-Emulation-Overview.md`](.plan/1.0-Emulation-Overview.md) | High-level architecture, implementation phases, data structures, sysctls |
| `1.1`–`1.6` | Security series | Threat model, access control, custom emulator security, filesystem/devices, additional analysis, implementation phases |
| `2.0`–`2.5` | Architecture specs | amd64, i386, arm64, arm, PowerPC, RISC-V instruction sets and CPU models |
| `3.0` | [`3.0-Emulation-Devices.md`](.plan/3.0-Emulation-Devices.md) | Device emulation models (sound, USB, NICs, storage, display) |
| `4.0` | [`4.0-Emulation-Blob-Management.md`](.plan/4.0-Emulation-Blob-Management.md) | Firmware blob acquisition, CPU model database, ports collection integration |

---

## Primary Directives

### 1. Security First
- **Root-only by default** — The emulation framework must be locked down until explicitly opened up
- **Capsicum sandboxing** — All emulator processes must enter capability mode after initialization
- **No escape** — Emulated instances must never access host filesystem, network, or processes except through explicitly configured shares
- **Memory scrubbing** — Guest memory must be zeroed between instances to prevent data leakage

### 2. Modular Architecture
- **Kernel modules** — `emu.ko` (master) loads `emu_core.ko` + `emu_<arch>.ko` per architecture
- **Per-architecture granularity** — Load only what you need: `kldload emu_amd64` or `kldload emu` for all
- **Plugin-style device models** — Devices are registered via a device model registry, not hardcoded

### 3. Traceability
- **Every task must be claimed** — Update the task table before starting work
- **Every task must be completed with tests** — No task is done until all unit tests pass
- **Every change must be committed** — Commit after claiming, commit after completing
- **Fix other agents' code** — If tests fail due to another agent's bugs, fix them as part of your task

### 4. No Blobs in Base
- **Firmware blobs are never** committed to the source tree or included in FreeBSD releases
- **Use FreeBSD ports** when available (e.g., `pkg install edk2-ovmf` for UEFI firmware)
- **Fall back to direct download** when ports aren't available

---

## Workflow Summary

### Picking a Task
1. Pull latest: `git pull --rebase`
2. Open the relevant document from `.plan/`
3. Find a task with empty `Status`, `Assigned To`, and `Start`
4. Check that all `Dependencies` are marked `✅ DONE`
5. Claim it: set `Status` → `🔄 IN PROGRESS`, fill `Assigned To` and `Start`
6. `git pull --rebase` again and check to see if your task was taken by another agent.
7. Commit: `git add .plan/<doc>.md && git commit -m "Claim task <ID>" && git push`

### Completing a Task
1. Implement the task following the plan document
2. **Run all unit tests** — fix any failures, even in other agents' code
3. Mark complete: set `Status` → `✅ DONE`, fill `End`, update `Notes`
4. Commit: `git add -A && git commit -m "Complete task <ID>: <desc>" && git push`
5. Move to the next task

### Handling Merge Conflicts
1. Check if your task was taken by another agent (look at `Assigned To`)
2. If taken, abandon and pick a different task
3. If not taken, resolve the conflict, keep both changes if they affect different tasks
4. `git add <file> && git rebase --continue && git push`

> **Full details:** See [`0.1-Emulation-Workflow.md`](.plan/0.1-Emulation-Workflow.md)

---

## Reading Order

For a new agent, read the documents in this order:

1. **This file** (`AGENTS_START_HERE.md`) — You are here
2. **[`0.1-Emulation-Workflow.md`](.plan/0.1-Emulation-Workflow.md)** — How to work on tasks
3. **[`1.0-Emulation-Overview.md`](.plan/1.0-Emulation-Overview.md)** — The big picture
4. **[`1.1`](.plan/1.1-Emulation-Security-p1-ThreatModel-Isolation.md) through [`1.6`](.plan/1.6-Emulation-Security-p6-Implementation.md)** — Security architecture
5. **[`4.0-Emulation-Blob-Management.md`](.plan/4.0-Emulation-Blob-Management.md)** — Blob management
6. **[`3.0-Emulation-Devices.md`](.plan/3.0-Emulation-Devices.md)** — Device models
7. **[`2.0`](.plan/2.0-Emulation-Arch-amd64.md) through [`2.5`](.plan/2.5-Emulation-Arch-riscv.md)** — Architecture details (as needed)

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Kernel modules vs built-in | Loadable kernel modules (`emu.ko` + `emu_<arch>.ko`) | Flexibility, smaller kernel, per-architecture loading |
| Memory allocation | Demand-paged with `MAP_NORESERVE` | No overcommit by default, balloon driver for dynamic release |
| Access control | Root-only default, `GID_EMU` group, granular permissions | Security-first, then open up as needed |
| Firmware blobs | Never in base, use ports or download | Keep FreeBSD base system lean, respect licenses |
| CPU models | JSON database with predefined models | Easy to add new models, no recompilation needed |
| Device models | Plugin registry with ring buffer for slow emulation | Extensible, handles emulation speed differences |
| Testing | Comprehensive unit tests per component | Every task must have tests, tests must pass before completion |

---

## Quick Reference

### Key Files

| File | Purpose |
|------|---------|
| `sys/emulation/emu_core.c` | Core kernel module |
| `sys/emulation/emu_<arch>.c` | Per-architecture CPU emulation |
| `usr.sbin/emu/emu.c` | Userland CLI entry point |
| `usr.sbin/emu/emu_blob.c` | Blob management subcommand |
| `usr.sbin/emu/blobs.json` | Blob manifest with URLs and checksums |
| `usr.sbin/emu/cpu_models.json` | CPU model database |

### Key Sysctls

| Sysctl | Default | Purpose |
|--------|---------|---------|
| `kern.emulation.allow_nonroot` | `0` | Allow non-root users to use emulation |
| `kern.emulation.required_group` | `979` (GID_EMU) | Group required for non-root access |
| `kern.emulation.max_instances` | `16` | Maximum concurrent instances |
| `kern.emulation.memory_overcommit` | `0` | Allow overcommitting host memory |
| `kern.emulation.sandbox_capsicum` | `1` | Enable Capsicum sandboxing |
| `kern.emulation.nested_virt` | `0` | Enable nested virtualization (bhyve path only) |

### Key Groups

| Group | GID | Purpose |
|-------|-----|---------|
| `emu` | `979` | Emulation operator group |

---

## Need Help?

If you encounter issues:
1. Check the relevant plan document for guidance
2. Check the task's `Notes` column for known issues
3. Mark the task as `🟡 BLOCKED` with the reason
4. Commit and push so other agents know
5. Ask for guidance

> **Remember:** The goal is to build a secure, modular, and well-tested kernel emulation framework for FreeBSD. Every task should bring us closer to that goal.
