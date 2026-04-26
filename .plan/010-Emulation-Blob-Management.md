# Emulation Framework Blob Management — Implementation Plan

## 1. Executive Summary

The emulation framework requires several firmware blobs to boot guest operating systems: SeaBIOS (legacy BIOS), OVMF (UEFI), U-Boot (boot loader), OpenSBI (RISC-V M-mode firmware), and device tree blobs (DTB). These blobs are **not** part of the FreeBSD source tree and **must not** be packaged into the FreeBSD kernel or release distribution.

Instead, the framework provides:

- **`emu blob` subcommand**: A tool to download, verify, list, and manage firmware blobs
- **Blob manifest system**: A declarative manifest (`blobs.json`) describing each blob's source URL, checksum, license, and target path
- **Blob cache directory**: A well-known location (`/var/emu/blobs/` or `~/.cache/emu/blobs/`) where blobs are stored
- **User guidance**: Clear instructions for each blob — where to get it, how to verify it, and where to place it
- **Build-time integration**: Optional `make blobs` target to pre-fetch blobs during build, but never bundled in the release

**Key design decisions:**
- Blobs are **never** committed to the FreeBSD source tree
- Blobs are **never** packaged in FreeBSD release tarballs or ISOs
- The `emu blob` tool handles download, verification, and caching
- Users can manually download and place blobs if they prefer
- Each blob has a documented source URL, checksum, and license
- The blob cache is shared across all instances and architectures

---

## 2. Blob Inventory

### 2.1 Complete Blob List

| Blob ID | Name | Architecture(s) | Type | License | Size (approx) | Required For |
|---------|------|-----------------|------|---------|---------------|--------------|
| `seabios` | SeaBIOS | amd64, i386 | Legacy BIOS firmware | LGPLv3 (with BSD-licensed components) | ~60KB | Booting FreeBSD on x86 without UEFI |
| `ovmf-ia32` | OVMF (IA32) | i386 | UEFI firmware | BSD-2-Clause | ~2MB | UEFI boot on i386 |
| `ovmf-x64` | OVMF (X64) | amd64 | UEFI firmware | BSD-2-Clause | ~2.5MB | UEFI boot on amd64 |
| `ovmf-aarch64` | OVMF (AArch64) | arm64 | UEFI firmware | BSD-2-Clause | ~3MB | UEFI boot on arm64 |
| `uboot-amd64` | U-Boot (amd64) | amd64 | Boot loader | GPLv2 | ~500KB | Alternative boot loader for x86 |
| `uboot-arm64` | U-Boot (arm64) | arm64 | Boot loader | GPLv2 | ~500KB | Booting FreeBSD on arm64 |
| `uboot-arm` | U-Boot (arm) | arm | Boot loader | GPLv2 | ~400KB | Booting FreeBSD on arm32 |
| `uboot-ppc` | U-Boot (powerpc) | powerpc | Boot loader | GPLv2 | ~400KB | Booting FreeBSD on powerpc |
| `uboot-riscv` | U-Boot (riscv) | riscv | Boot loader | GPLv2 | ~400KB | Booting FreeBSD on riscv |
| `opensbi` | OpenSBI | riscv | M-mode firmware | BSD-2-Clause | ~100KB | RISC-V SBI services |
| `dtb-virt-arm64` | Device Tree (arm64 virt) | arm64 | Device tree blob | GPLv2 / MIT | ~10KB | Platform description for arm64 |
| `dtb-virt-riscv` | Device Tree (riscv virt) | riscv | Device tree blob | GPLv2 / MIT | ~8KB | Platform description for riscv |

### 2.2 Blob Details

#### 2.2.1 SeaBIOS

| Property | Value |
|----------|-------|
| **Blob ID** | `seabios` |
| **Source URL** | `https://www.seabios.org/downloads/seabios-<version>-prebuilt.bin` |
| **Latest version** | 1.16.3 |
| **License** | LGPLv3 (some components BSD-licensed) |
| **Can redistribute?** | Yes (LGPLv3, source available) |
| **Build from source?** | Yes — `git clone https://git.seabios.org/seabios.git && make` |
| **Target filename** | `seabios.bin` |
| **Target path** | `${EMU_BLOB_DIR}/seabios/` |
| **Load address** | 0x000F0000 (aliased at 0xFFFF0000) |
| **Size** | ~60KB (128KB padded to BIOS region) |
| **Verification** | SHA-256 checksum |
| **Architecture** | amd64, i386 |

**Source build instructions:**
```sh
git clone https://git.seabios.org/seabios.git
cd seabios
make CONFIG_QEMU=y
# Output: out/bios.bin — copy to blob cache as seabios.bin
```

**Pre-built download:**
```sh
emu blob fetch seabios
# Downloads to /var/emu/blobs/seabios/seabios.bin
```

#### 2.2.2 OVMF (TianoCore EDK II)

| Property | Value |
|----------|-------|
| **Blob ID** | `ovmf-x64`, `ovmf-ia32`, `ovmf-aarch64` |
| **Source URL** | `https://github.com/tianocore/edk2/releases` |
| **Latest version** | edk2-stable202411 |
| **License** | BSD-2-Clause |
| **Can redistribute?** | Yes (BSD-2-Clause) |
| **Build from source?** | Yes — `git clone https://github.com/tianocore/edk2.git` |
| **Target filenames** | `OVMF_CODE.fd` (code), `OVMF_VARS.fd` (variables) |
| **Target path** | `${EMU_BLOB_DIR}/ovmf-<arch>/` |
| **Load address** | 0x000F0000-0x00FFFFFF (16MB flash) |
| **Size** | ~2-3MB per arch |
| **Verification** | SHA-256 checksum |
| **Architecture** | amd64 (x64), i386 (ia32), arm64 (aarch64) |

**Source build instructions:**
```sh
git clone https://github.com/tianocore/edk2.git
cd edk2
make -C BaseTools
source edksetup.sh
# For X64:
build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc
# Output: Build/OvmfX64/RELEASE_GCC5/FV/OVMF_CODE.fd, OVMF_VARS.fd

# For IA32:
build -a IA32 -t GCC5 -p OvmfPkg/OvmfPkgIa32.dsc

# For AARCH64:
build -a AARCH64 -t GCC5 -p ArmVirtPkg/ArmVirtQemu.dsc
```

**Pre-built download:**
```sh
emu blob fetch ovmf-x64
emu blob fetch ovmf-ia32
emu blob fetch ovmf-aarch64
```

#### 2.2.3 U-Boot

| Property | Value |
|----------|-------|
| **Blob ID** | `uboot-<arch>` |
| **Source URL** | `https://github.com/u-boot/u-boot/releases` |
| **Latest version** | v2025.04 |
| **License** | GPLv2 |
| **Can redistribute?** | Yes (GPLv2, source available) |
| **Build from source?** | Yes — `git clone https://github.com/u-boot/u-boot.git` |
| **Target filename** | `u-boot.bin` |
| **Target path** | `${EMU_BLOB_DIR}/uboot-<arch>/` |
| **Load address** | Architecture-specific (typically DRAM base) |
| **Size** | ~400-500KB per arch |
| **Verification** | SHA-256 checksum |
| **Architecture** | amd64, arm64, arm, powerpc, riscv |

**Source build instructions:**
```sh
git clone https://github.com/u-boot/u-boot.git
cd u-boot

# For arm64 virt:
make qemu_arm64_defconfig
make -j$(nproc)
# Output: u-boot.bin

# For arm virt:
make qemu_arm_defconfig
make -j$(nproc)

# For riscv64:
make qemu-riscv64_defconfig
make -j$(nproc)

# For amd64 (x86):
make qemu-x86_defconfig
make -j$(nproc)

# For powerpc:
make qemu-ppce500_defconfig
make -j$(nproc)
```

**Pre-built download:**
```sh
emu blob fetch uboot-arm64
emu blob fetch uboot-riscv
```

#### 2.2.4 OpenSBI

| Property | Value |
|----------|-------|
| **Blob ID** | `opensbi` |
| **Source URL** | `https://github.com/riscv-software-src/opensbi/releases` |
| **Latest version** | v1.6 |
| **License** | BSD-2-Clause |
| **Can redistribute?** | Yes (BSD-2-Clause) |
| **Build from source?** | Yes — `git clone https://github.com/riscv-software-src/opensbi.git` |
| **Target filename** | `fw_jump.bin` |
| **Target path** | `${EMU_BLOB_DIR}/opensbi/` |
| **Load address** | 0x80000000 (typical DRAM base) |
| **Size** | ~100KB |
| **Verification** | SHA-256 checksum |
| **Architecture** | riscv |

**Source build instructions:**
```sh
git clone https://github.com/riscv-software-src/opensbi.git
cd opensbi
make PLATFORM=generic CROSS_COMPILE=riscv64-unknown-freebsd-
# Output: build/platform/generic/firmware/fw_jump.bin
```

**Pre-built download:**
```sh
emu blob fetch opensbi
```

#### 2.2.5 Device Tree Blobs (DTB)

| Property | Value |
|----------|-------|
| **Blob ID** | `dtb-virt-arm64`, `dtb-virt-riscv` |
| **Source** | Generated from device tree source (DTS) files |
| **License** | GPLv2 / MIT (varies by platform) |
| **Build from source?** | Yes — compiled from `.dts` files shipped with the emulator |
| **Target filename** | `virt.dtb` |
| **Target path** | `${EMU_BLOB_DIR}/dtb/` |
| **Load address** | Architecture-specific (passed to firmware via register) |
| **Size** | ~8-10KB |
| **Verification** | SHA-256 checksum |
| **Architecture** | arm64, riscv |

**Build instructions:**
```sh
# Device tree source files are shipped with the emulator in:
#   usr.sbin/emu/dts/virt-arm64.dts
#   usr.sbin/emu/dts/virt-riscv.dts

# Compile with dtc (device tree compiler):
dtc -I dts -O dtb -o virt-arm64.dtb usr.sbin/emu/dts/virt-arm64.dts
dtc -I dts -O dtb -o virt-riscv.dtb usr.sbin/emu/dts/virt-riscv.dts

# Or use emu blob:
emu blob build dtb-virt-arm64
emu blob build dtb-virt-riscv
```

---

## 3. Predefined CPU Models

The emulator ships with a comprehensive database of predefined CPU models that can be selected via the `--cpu-level` flag when creating an instance. Each CPU model defines:

- **Base ISA**: The instruction set architecture version
- **Features**: Which instruction set extensions are available
- **Register width**: 32-bit, 64-bit, or mixed
- **Page table format**: How virtual memory is translated
- **Interrupt model**: Which interrupt controller is used
- **Default speed**: Recommended CPU speed in MHz (can be overridden with `--cpu-speed`)
- **Compatibility notes**: Known issues or quirks for specific software

### 3.1 AMD64 (x86-64) CPU Models

| Model ID | Base ISA | Features | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|----------|---------------|-------------|-----------------|---------------|-------|
| `x86-64` | AMD64 | CMPXCHG8B, CMPXCHG16B, LAHF/SAHF, POPCNT, SSE, SSE2, NX bit | 64-bit | 4-level (PML4) | LAPIC + I/O APIC | 2000 MHz | Baseline amd64, widest compatibility |
| `x86-64-v2` | AMD64 | x86-64 + SSE3, SSE4.1, SSE4.2, SSSE3, CX16 | 64-bit | 4-level (PML4) | LAPIC + I/O APIC | 2500 MHz | Modern amd64 (Nehalem-era+) |
| `x86-64-v3` | AMD64 | x86-64-v2 + AVX, AVX2, BMI1, BMI2, F16C, FMA, MOVBE, XSAVE | 64-bit | 4-level (PML4) | LAPIC + I/O APIC | 3000 MHz | Haswell-era+, FreeBSD 14+ default |
| `x86-64-v4` | AMD64 | x86-64-v3 + AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL | 64-bit | 4-level (PML4) | LAPIC + I/O APIC | 3500 MHz | Skylake-era+, high-performance |
| `x86-64-5level` | AMD64 | x86-64-v4 + 5-level paging (LA57) | 64-bit | 5-level (PML5) | LAPIC + I/O APIC | 3500 MHz | Future, 57-bit virtual address space |

### 3.2 i386 (x86-32) CPU Models

| Model ID | Base ISA | Features | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|----------|---------------|-------------|-----------------|---------------|-------|
| `i386` | IA-32 | CMPXCHG8B, SSE2, PAE, PSE36, FXSR | 32-bit | 2-level (no PAE) or 3-level (PAE) | PIC (i8259) + LAPIC | 66 MHz | Baseline i386, Windows 95/98 era |
| `i486` | IA-32 | i386 + CMPXCHG, XADD, BSWAP, INVLPG | 32-bit | 2-level or 3-level (PAE) | PIC (i8259) + LAPIC | 100 MHz | 486-class, Windows 95 |
| `i586` | IA-32 | i486 + MMX, conditional move, RDTSC | 32-bit | 2-level or 3-level (PAE) | PIC (i8259) + LAPIC | 200 MHz | Pentium-class, Windows 98 |
| `i686` | IA-32 | i586 + SSE, P6 features, CMOV, SYSENTER/SYSEXIT | 32-bit | 2-level or 3-level (PAE) | PIC (i8259) + LAPIC | 500 MHz | Pentium Pro+, Windows XP |

### 3.3 ARM64 (AArch64) CPU Models

| Model ID | Base ISA | Features | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|----------|---------------|-------------|-----------------|---------------|-------|
| `armv8.0-a` | ARMv8.0-A | A64 ISA, 4KB/16KB/64KB pages, 48-bit VA, GICv3, virtualization | 64-bit | VMSAv8-64 (3 or 4 levels) | GICv3 | 1500 MHz | Baseline arm64, Raspberry Pi 3 |
| `armv8.1-a` | ARMv8.0-A | + LSE (atomics), PAN, VHE | 64-bit | VMSAv8-64 | GICv3 | 1800 MHz | Modern arm64, Raspberry Pi 4 |
| `armv8.2-a` | ARMv8.1-A | + RAS, SVE (scalar), statistical profiling | 64-bit | VMSAv8-64 | GICv3 | 2000 MHz | Late 2010s SoCs, Raspberry Pi 5 |
| `armv8.3-a` | ARMv8.2-A | + Pointer authentication, JS conversion | 64-bit | VMSAv8-64 | GICv3 | 2200 MHz | 2018+ SoCs, Apple A12 |
| `armv8.4-a` | ARMv8.3-A | + SVE2, MPAM, DIT, IDST | 64-bit | VMSAv8-64 | GICv3 | 2400 MHz | 2020+ SoCs |
| `armv8.5-a` | ARMv8.4-A | + MTE (memory tagging), BTI, ETE/TRBE | 64-bit | VMSAv8-64 | GICv3 | 2500 MHz | 2021+ SoCs |
| `armv8.6-a` | ARMv8.5-A | + I8MM, BF16, AMUv1, enhanced virt | 64-bit | VMSAv8-64 | GICv3 | 2600 MHz | 2022+ SoCs |
| `armv9.0-a` | ARMv9.0-A | SVE2 base, RME, CCA | 64-bit | VMSAv8-64 | GICv3 | 2800 MHz | 2023+ SoCs |
| `armv9.1-a` | ARMv9.0-A | + Enhanced SVE2, more crypto | 64-bit | VMSAv8-64 | GICv3 | 3000 MHz | Future |
| `armv9.2-a` | ARMv9.1-A | + Further ISA extensions | 64-bit | VMSAv8-64 | GICv3 | 3200 MHz | Future |

### 3.4 ARM (32-bit) CPU Models

| Model ID | Base ISA | Features | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|----------|---------------|-------------|-----------------|---------------|-------|
| `armv4` | ARMv4 | ARM ISA, SWP, BLX | 32-bit | Section/page table | GICv2 (optional) | 100 MHz | Legacy, ARM7TDMI |
| `armv5` | ARMv4 | + BLX (immediate), CLZ, BKPT, DSP | 32-bit | Section/page table | GICv2 | 200 MHz | XScale, ARM9 |
| `armv6` | ARMv5 | + Thumb, SIMD, VFPv2, PIPT MMU | 32-bit | Section/page table | GICv2 | 600 MHz | ARM11, Raspberry Pi 1 |
| `armv7-a` | ARMv6 | + Thumb-2, NEON, VFPv3, virtualization | 32-bit | Short/long descriptor | GICv2 | 1000 MHz | FreeBSD arm primary, Cortex-A8/A9/A15 |

### 3.5 PowerPC CPU Models

| Model ID | Base ISA | Features | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|----------|---------------|-------------|-----------------|---------------|-------|
| `ppc-g1` | PowerISA 1.xx | FPU, no AltiVec | 32-bit | Hash MMU (32-bit) | OpenPIC | 100 MHz | 601/603/604, Power Macintosh |
| `ppc-g2` | PowerISA 2.01 | AltiVec, Book-E | 32-bit | Hash MMU (32-bit) | OpenPIC | 500 MHz | 74xx (G4), Power Mac G4 |
| `ppc-g3` | PowerISA 2.02 | 32/64-bit, AltiVec, hypervisor | 64-bit | Hash MMU (64-bit) | OpenPIC | 1000 MHz | 970 (G5), Power Mac G5 |
| `ppc-g4` | PowerISA 2.04 | 32/64-bit, AltiVec, VMX128 | 32/64-bit | Hash MMU | OpenPIC | 1200 MHz | e600 |
| `ppc-pwr8` | PowerISA 2.07B | 64-bit, VSX, LE support, crypto | 64-bit | Hash MMU (64-bit) | OpenPIC | 3000 MHz | POWER8, FreeBSD powerpc64 |
| `ppc-pwr9` | PowerISA 3.0B | 64-bit, VSX-3, radix MMU, crypto | 64-bit | Radix MMU | OpenPIC | 3500 MHz | POWER9 |
| `ppc-pwr10` | PowerISA 3.1 | 64-bit, VSX-4, MMA, prefix insns | 64-bit | Radix MMU | OpenPIC | 4000 MHz | POWER10 |

### 3.6 RISC-V CPU Models

| Model ID | Base ISA | Extensions | Register Width | Page Tables | Interrupt Model | Default Speed | Notes |
|----------|----------|------------|---------------|-------------|-----------------|---------------|-------|
| `rv64imafd` | RV64I | M, A, F, D | 64-bit | Sv39 | CLINT + PLIC | 1000 MHz | Baseline riscv64, FreeBSD riscv64 |
| `rv64imafdc` | RV64I | M, A, F, D, C (compressed) | 64-bit | Sv39 | CLINT + PLIC | 1200 MHz | Common embedded, SiFive U74 |
| `rv64imafdcv` | RV64I | M, A, F, D, C, V (vector) | 64-bit | Sv39/Sv48 | CLINT + PLIC | 1500 MHz | Vector-capable |
| `rv64imafdch` | RV64I | M, A, F, D, C, H (hypervisor) | 64-bit | Sv39/Sv48 | CLINT + PLIC | 1500 MHz | Hypervisor extension |
| `rv32imafd` | RV32I | M, A, F, D | 32-bit | Sv32 | CLINT + PLIC | 500 MHz | Baseline riscv32 |
| `rv32imac` | RV32I | M, A, C | 32-bit | Sv32 | CLINT + PLIC | 400 MHz | Embedded riscv32, low-cost |

### 3.7 CPU Model Selection

Users select a CPU model via the `--cpu-level` flag:

```sh
# Select a specific CPU model
emu start --name test --arch amd64 --cpu-level x86-64-v3

# Select a legacy CPU for compatibility testing
emu start --name win95-test --arch i386 --cpu-level i386 --cpu-speed 66

# Select a specific ARM CPU
emu start --name arm-test --arch arm64 --cpu-level armv8.2-a

# List available CPU models for an architecture
emu blob list --arch amd64 --cpu-models
```

The CPU model database is shipped with the emulator in:

```
usr.sbin/emu/cpu_models.json
```

This JSON file contains the full specification for each CPU model, including:
- Which CPUID leaves/features to expose
- Which MSRs/system registers to initialize
- Default timer frequencies
- Cache topology (for performance estimation)
- Known errata and workarounds

### 3.8 CPU Model JSON Schema

```json
{
  "version": 1,
  "models": {
    "x86-64-v3": {
      "arch": "amd64",
      "base_isa": "x86-64",
      "features": {
        "cpuid": {
          "leaf_1_ecx": 0x7FFAFBFF,
          "leaf_1_edx": 0xBFEBFBFF,
          "leaf_7_ebx": 0x00000281,
          "leaf_7_ecx": 0x00000000,
          "leaf_80000001_ecx": 0x00000021,
          "leaf_80000001_edx": 0x2C100800
        },
        "extensions": ["sse3", "ssse3", "sse4.1", "sse4.2", "avx", "avx2",
                       "bmi1", "bmi2", "f16c", "fma", "movbe", "xsave",
                       "popcnt", "cx16", "lahf_lm"]
      },
      "registers": {
        "msr_efer": 0x0001,
        "cr0_reserved": 0x80000001,
        "cr4_mask": 0x00000600
      },
      "timing": {
        "default_speed_mhz": 3000,
        "tsc_frequency_mhz": 3000,
        "apic_timer_frequency_hz": 1000000000
      },
      "cache": {
        "l1d_size_kb": 32,
        "l1i_size_kb": 32,
        "l2_size_kb": 256,
        "l3_size_kb": 8192
      },
      "compatibility": {
        "known_issues": [],
        "min_freebsd_version": "12.0"
      }
    }
  }
}
```

---

## 4. Blob Cache Directory Structure

### 4.1 Directory Layout

```
/var/emu/blobs/                          # System-wide blob cache (root)
  ├── blobs.json                         # Blob manifest (checksums, URLs, metadata)
  ├── seabios/
  │   ├── seabios.bin                    # SeaBIOS firmware binary
  │   └── seabios.bin.sha256            # Checksum file
  ├── ovmf-x64/
  │   ├── OVMF_CODE.fd                   # OVMF code image
  │   ├── OVMF_VARS.fd                   # OVMF variables image
  │   └── OVMF_CODE.fd.sha256
  ├── ovmf-ia32/
  │   ├── OVMF_CODE.fd
  │   ├── OVMF_VARS.fd
  │   └── OVMF_CODE.fd.sha256
  ├── ovmf-aarch64/
  │   ├── OVMF_CODE.fd
  │   ├── OVMF_VARS.fd
  │   └── OVMF_CODE.fd.sha256
  ├── uboot-arm64/
  │   ├── u-boot.bin
  │   └── u-boot.bin.sha256
  ├── uboot-arm/
  │   ├── u-boot.bin
  │   └── u-boot.bin.sha256
  ├── uboot-riscv/
  │   ├── u-boot.bin
  │   └── u-boot.bin.sha256
  ├── uboot-amd64/
  │   ├── u-boot.bin
  │   └── u-boot.bin.sha256
  ├── uboot-ppc/
  │   ├── u-boot.bin
  │   └── u-boot.bin.sha256
  ├── opensbi/
  │   ├── fw_jump.bin
  │   └── fw_jump.bin.sha256
  └── dtb/
      ├── virt-arm64.dtb
      ├── virt-riscv.dtb
      └── virt-arm64.dtb.sha256

~/.cache/emu/blobs/                      # Per-user blob cache (non-root)
  └── (same structure as above)
```

### 4.2 Cache Resolution Order

When the emulator needs a blob, it searches in this order:

1. **Instance-specific override**: `${EMU_INSTANCE_DIR}/blobs/<blob-id>/` (for testing custom firmware)
2. **System-wide cache**: `/var/emu/blobs/<blob-id>/` (preferred for root/multi-user)
3. **User cache**: `~/.cache/emu/blobs/<blob-id>/` (for non-root users)
4. **Build-time source**: `${EMU_SOURCE_DIR}/blobs/<blob-id>/` (if blobs were pre-fetched during build)

If the blob is not found in any location, the emulator reports a clear error message telling the user:
- Which blob is missing
- Where to download it from
- Which command to run (`emu blob fetch <blob-id>`)
- Where to place it manually if preferred

---

## 5. Blob Manifest Format

The blob manifest (`blobs.json`) is a JSON file that describes all known blobs. It is shipped with the emulator source code and installed to the blob cache.

### 5.1 Manifest Schema

```json
{
  "version": 1,
  "blobs": {
    "seabios": {
      "name": "SeaBIOS",
      "description": "Legacy BIOS firmware for x86 systems",
      "version": "1.16.3",
      "license": "LGPLv3",
      "license_url": "https://www.seabios.org/License",
      "homepage": "https://www.seabios.org/",
      "architectures": ["amd64", "i386"],
      "files": {
        "seabios.bin": {
          "url": "https://www.seabios.org/downloads/seabios-1.16.3-prebuilt.bin",
          "sha256": "a1b2c3d4e5f6...",
          "size": 65536,
          "load_address": "0x000F0000"
        }
      },
      "build_from_source": {
        "repository": "https://git.seabios.org/seabios.git",
        "instructions": "git clone ... && make CONFIG_QEMU=y",
        "output": "out/bios.bin"
      },
      "notes": "SeaBIOS is BSD-licensed in some components. The combined binary is LGPLv3."
    },
    "ovmf-x64": {
      "name": "OVMF (X64)",
      "description": "UEFI firmware for amd64 systems",
      "version": "edk2-stable202411",
      "license": "BSD-2-Clause",
      "license_url": "https://github.com/tianocore/edk2/blob/master/LICENSE",
      "homepage": "https://github.com/tianocore/tianocore.github.io/wiki/OVMF",
      "architectures": ["amd64"],
      "files": {
        "OVMF_CODE.fd": {
          "url": "https://github.com/tianocore/edk2/releases/download/edk2-stable202411/OVMF-X64.zip",
          "sha256": "b2c3d4e5f6a7...",
          "size": 2621440,
          "load_address": "0x000F0000"
        },
        "OVMF_VARS.fd": {
          "url": "https://github.com/tianocore/edk2/releases/download/edk2-stable202411/OVMF-X64.zip",
          "sha256": "c3d4e5f6a7b8...",
          "size": 131072,
          "load_address": "0x000F0000"
        }
      },
      "build_from_source": {
        "repository": "https://github.com/tianocore/edk2.git",
        "instructions": "git clone ... && make -C BaseTools && source edksetup.sh && build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc",
        "output": "Build/OvmfX64/RELEASE_GCC5/FV/OVMF_CODE.fd, OVMF_VARS.fd"
      }
    },
    "uboot-arm64": {
      "name": "U-Boot (arm64)",
      "description": "Boot loader for arm64 systems",
      "version": "v2025.04",
      "license": "GPLv2",
      "license_url": "https://github.com/u-boot/u-boot/blob/master/Licenses/gpl-2.0.txt",
      "homepage": "https://github.com/u-boot/u-boot",
      "architectures": ["arm64"],
      "files": {
        "u-boot.bin": {
          "url": "https://github.com/u-boot/u-boot/releases/download/v2025.04/u-boot-qemu-arm64.bin",
          "sha256": "d4e5f6a7b8c9...",
          "size": 524288,
          "load_address": "0x40000000"
        }
      },
      "build_from_source": {
        "repository": "https://github.com/u-boot/u-boot.git",
        "instructions": "git clone ... && make qemu_arm64_defconfig && make",
        "output": "u-boot.bin"
      }
    },
    "opensbi": {
      "name": "OpenSBI",
      "description": "RISC-V M-mode firmware providing SBI services",
      "version": "v1.6",
      "license": "BSD-2-Clause",
      "license_url": "https://github.com/riscv-software-src/opensbi/blob/master/LICENSE",
      "homepage": "https://github.com/riscv-software-src/opensbi",
      "architectures": ["riscv"],
      "files": {
        "fw_jump.bin": {
          "url": "https://github.com/riscv-software-src/opensbi/releases/download/v1.6/opensbi-generic-fw_jump.bin",
          "sha256": "e5f6a7b8c9d0...",
          "size": 102400,
          "load_address": "0x80000000"
        }
      },
      "build_from_source": {
        "repository": "https://github.com/riscv-software-src/opensbi.git",
        "instructions": "git clone ... && make PLATFORM=generic",
        "output": "build/platform/generic/firmware/fw_jump.bin"
      }
    },
    "dtb-virt-arm64": {
      "name": "Device Tree (arm64 virt)",
      "description": "Device tree blob for the arm64 virtual platform",
      "version": "1.0",
      "license": "BSD-2-Clause",
      "license_url": "",
      "homepage": "",
      "architectures": ["arm64"],
      "files": {
        "virt-arm64.dtb": {
          "url": null,
          "sha256": "f6a7b8c9d0e1...",
          "size": 10240,
          "load_address": null,
          "build_method": "dtc -I dts -O dtb -o virt-arm64.dtb usr.sbin/emu/dts/virt-arm64.dts"
        }
      },
      "build_from_source": {
        "repository": null,
        "instructions": "Shipped as DTS source. Compile with dtc.",
        "output": "virt-arm64.dtb"
      }
    }
  }
}
```

### 5.2 Manifest Location

The manifest is shipped with the emulator source at:

```
usr.sbin/emu/blobs.json
```

When `emu blob` commands run, they read this manifest to know where to download blobs from and how to verify them.

---

## 6. `emu blob` Subcommand Design

### 6.1 Command Interface

```
emu blob [command] [options]

Commands:
  fetch     Download one or more blobs
  list      List available and installed blobs
  status    Show status of blobs (installed, missing, outdated)
  verify    Verify checksums of installed blobs
  remove    Remove a blob from the cache
  build     Build a blob from source (DTB, or invoke build scripts)
  info      Show detailed information about a specific blob
  path      Print the path to a blob (for scripting)
  update    Update blob manifest from upstream
```

### 6.2 Command Details

#### `emu blob fetch`

```
emu blob fetch [<blob-id>...] [options]

Options:
  --all              Fetch all blobs for all architectures
  --arch <arch>      Fetch blobs for a specific architecture
  --force            Re-download even if already cached
  --verify           Verify checksums after download (default: on)
  --no-verify        Skip checksum verification
  --cache-dir <dir>  Use alternative cache directory
  --progress         Show download progress (default: on in terminals)

Examples:
  emu blob fetch seabios                          # Fetch SeaBIOS only
  emu blob fetch ovmf-x64 ovmf-aarch64            # Fetch multiple blobs
  emu blob fetch --arch amd64                     # Fetch all amd64 blobs
  emu blob fetch --all                            # Fetch everything
  emu blob fetch seabios --force                  # Re-download SeaBIOS
```

**Behavior:**
1. Read `blobs.json` manifest
2. For each requested blob ID, check if already cached (unless `--force`)
3. If not cached, download from the URL specified in the manifest
4. Verify SHA-256 checksum (unless `--no-verify`)
5. Extract if the download is an archive (zip, tar.gz)
6. Place files in the correct cache directory
7. Report success/failure for each blob

**Error handling:**
- If a blob's URL is unreachable, print the URL and suggest manual download
- If checksum verification fails, delete the downloaded file and report corruption
- If a blob has no URL (e.g., DTB which must be built), suggest `emu blob build`

#### `emu blob list`

```
emu blob list [options]

Options:
  --arch <arch>      List blobs for a specific architecture
  --installed        Only show installed blobs
  --missing          Only show missing blobs
  --outdated         Only show blobs with newer versions available
  --json             Output in JSON format

Examples:
  emu blob list                              # List all blobs
  emu blob list --arch arm64                 # List arm64 blobs
  emu blob list --missing                    # Show what's missing
  emu blob list --json                       # Machine-readable output
```

**Output format (table):**

```
Blob ID           Arch        Status      Version         Size
─────────────────────────────────────────────────────────────────
seabios           amd64,i386  ✓ INSTALLED 1.16.3          65KB
ovmf-x64          amd64       ✓ INSTALLED edk2-stable202411 2.5MB
ovmf-aarch64      arm64       ✗ MISSING   edk2-stable202411 3MB
uboot-arm64       arm64       ⚠ OUTDATED  v2024.01        500KB (latest: v2025.04)
opensbi           riscv       ✓ INSTALLED v1.6            100KB
dtb-virt-arm64    arm64       ✓ INSTALLED 1.0             10KB
```

#### `emu blob verify`

```
emu blob verify [<blob-id>...] [options]

Options:
  --all              Verify all installed blobs
  --arch <arch>      Verify blobs for a specific architecture
  --fix              Re-download blobs with mismatched checksums

Examples:
  emu blob verify --all                        # Verify everything
  emu blob verify seabios                      # Verify specific blob
  emu blob verify --all --fix                  # Verify and fix corrupted blobs
```

#### `emu blob remove`

```
emu blob remove <blob-id>... [options]

Options:
  --all              Remove all blobs
  --arch <arch>      Remove blobs for a specific architecture
  --force            Skip confirmation

Examples:
  emu blob remove seabios                      # Remove SeaBIOS
  emu blob remove --all                        # Remove all blobs (clean cache)
```

#### `emu blob build`

```
emu blob build <blob-id>... [options]

Options:
  --all              Build all buildable blobs
  --dtc-path <path>  Path to device tree compiler

Examples:
  emu blob build dtb-virt-arm64                # Build arm64 DTB from source
  emu blob build dtb-virt-riscv                # Build RISC-V DTB from source
```

**Behavior:**
- For DTB blobs: compile the `.dts` source files shipped with the emulator using `dtc`
- For other blobs: print instructions for building from source (clone repo, run make)
- The DTS source files are shipped in `usr.sbin/emu/dts/`

#### `emu blob info`

```
emu blob info <blob-id> [options]

Options:
  --json             Output in JSON format

Examples:
  emu blob info seabios                        # Show SeaBIOS details
  emu blob info ovmf-x64 --json                # Machine-readable output
```

**Output format:**

```
Blob ID:       seabios
Name:          SeaBIOS
Description:   Legacy BIOS firmware for x86 systems
Version:       1.16.3
License:       LGPLv3
Homepage:      https://www.seabios.org/
Architectures: amd64, i386
Files:
  seabios.bin
    URL:      https://www.seabios.org/downloads/seabios-1.16.3-prebuilt.bin
    SHA-256:  a1b2c3d4e5f6...
    Size:     65536 bytes (64 KB)
    Load at:  0x000F0000
Status:       ✓ INSTALLED at /var/emu/blobs/seabios/seabios.bin

Build from source:
  Repo:       https://git.seabios.org/seabios.git
  Command:    git clone ... && make CONFIG_QEMU=y
  Output:     out/bios.bin → rename to seabios.bin
```

#### `emu blob path`

```
emu blob path <blob-id> [<file>] [options]

Examples:
  emu blob path seabios                        # Print path to SeaBIOS directory
  emu blob path seabios seabios.bin            # Print path to specific file
  emu blob path ovmf-x64 OVMF_CODE.fd          # Print path to OVMF code image
```

**Behavior:**
- Resolves the blob path using the cache resolution order
- Returns the full path to the blob file or directory
- Exits with non-zero if the blob is not found
- Useful for scripting and integration with the emulator engine

#### `emu blob update`

```
emu blob update [options]

Options:
  --url <url>        URL to fetch updated manifest from
  --check-only       Check for updates without downloading

Examples:
  emu blob update                              # Update blob manifest
  emu blob update --check-only                 # Check if update is available
```

**Behavior:**
- Fetches the latest `blobs.json` from the emulator's upstream repository
- Merges with local manifest (preserving local overrides)
- Reports new blobs, updated versions, and removed blobs

### 6.3 Exit Codes

| Exit Code | Meaning |
|-----------|---------|
| 0 | Success |
| 1 | General error (permission, I/O, etc.) |
| 2 | Blob not found (unknown blob ID) |
| 3 | Download failed (network error, URL unreachable) |
| 4 | Checksum mismatch (corrupted download) |
| 5 | Blob not installed (for `path` command) |
| 6 | Build failed (for `build` command) |

---

## 7. Integration with Emulator Engine

### 7.1 Firmware Loading

The emulator engine (`usr.sbin/emu/emu_firmware.c`) uses the blob cache to locate firmware binaries:

```c
/* Firmware loading — uses blob cache resolution */
int
emu_firmware_load(struct emu_instance *inst, const char *blob_id)
{
    char path[PATH_MAX];
    int error;

    /* Resolve blob path using cache resolution order */
    error = emu_blob_resolve(blob_id, "seabios.bin", path, sizeof(path));
    if (error) {
        /* Blob not found — print helpful error message */
        emu_blob_print_help(blob_id);
        return (error);
    }

    /* Load firmware binary into emulated memory */
    return (emu_mem_load_file(inst, path, SEABIOS_LOAD_ADDRESS));
}
```

### 7.2 Blob Resolution API

```c
/* Resolve a blob file path using cache resolution order */
int emu_blob_resolve(const char *blob_id, const char *filename,
                     char *path, size_t pathlen);

/* Print helpful error message for missing blob */
void emu_blob_print_help(const char *blob_id);

/* Check if a blob is installed */
bool emu_blob_installed(const char *blob_id);

/* Get the blob cache directory */
const char *emu_blob_get_cache_dir(void);

/* Set the blob cache directory (for testing) */
void emu_blob_set_cache_dir(const char *dir);
```

### 7.3 Integration Points

| Component | Blob Used | Integration |
|-----------|-----------|-------------|
| `emu_arch_amd64.c` | `seabios.bin` or `OVMF_CODE.fd` | Load firmware at reset vector during instance start |
| `emu_arch_i386.c` | `seabios.bin` or `OVMF_CODE.fd` | Same as amd64 |
| `emu_arch_arm64.c` | `u-boot.bin` or `OVMF_CODE.fd` + `virt-arm64.dtb` | Load U-Boot + DTB at boot |
| `emu_arch_riscv.c` | `fw_jump.bin` + `u-boot.bin` + `virt-riscv.dtb` | Load OpenSBI + U-Boot + DTB |
| `emu_arch_arm.c` | `u-boot.bin` | Load U-Boot at boot |
| `emu_arch_ppc.c` | `u-boot.bin` | Load U-Boot at boot |

---

## 8. Build System Integration

### 8.1 Make Targets

The emulator's build system provides optional targets for blob management:

```makefile
# In usr.sbin/emu/Makefile

# Optional: Pre-fetch blobs during build (requires network access)
blobs:
    ${EMU_CMD} blob fetch --all

# Optional: Build DTB blobs from source
blobs-build:
    ${EMU_CMD} blob build --all

# Verify blobs after build
blobs-verify:
    ${EMU_CMD} blob verify --all

# Clean blob cache
blobs-clean:
    ${EMU_CMD} blob remove --all --force
```

These targets are **never** part of the default build. They must be explicitly invoked:

```sh
make blobs          # Pre-fetch all blobs
make blobs-build    # Build DTB blobs
make buildworld     # Normal build — no blobs fetched
```

### 8.2 Build-Time Blob Directory

If `make blobs` is run during build, blobs are placed in:

```
${SRCTOP}/usr.sbin/emu/blobs/
```

This directory is **not** committed to the source tree (added to `.gitignore`). It serves as a build-time cache so developers don't need to download blobs separately.

### 8.3 Release Integration

Blobs are **never** included in FreeBSD release artifacts:

- **No blobs in release tarballs** (`base.txz`, `kernel.txz`, etc.)
- **No blobs in installation ISO**
- **No blobs in the base system**
- The `emu` tool is shipped, but blobs must be fetched at runtime

The release build process explicitly excludes the blob cache:

```makefile
# In release/Makefile — blobs are excluded from release
NO_BLOBS=1
```

### 8.4 Package Integration (Future)

If the emulator is packaged (e.g., via pkg(8)), blobs could be provided as separate packages:

| Package | Contents |
|---------|----------|
| `emu` | Emulator tool and kernel modules (no blobs) |
| `emu-blobs-seabios` | SeaBIOS firmware |
| `emu-blobs-ovmf-x64` | OVMF for amd64 |
| `emu-blobs-ovmf-aarch64` | OVMF for arm64 |
| `emu-blobs-uboot-arm64` | U-Boot for arm64 |
| `emu-blobs-uboot-riscv` | U-Boot for RISC-V |
| `emu-blobs-opensbi` | OpenSBI for RISC-V |
| `emu-blobs-all` | Meta-package for all blobs |

This is a future enhancement and not part of the initial implementation.

---

## 9. User Guidance

### 9.1 Error Messages

When a blob is missing, the emulator prints a clear, actionable error message:

```
ERROR: Required firmware blob not found: seabios

  The emulator needs SeaBIOS firmware to boot FreeBSD on amd64.

  To download it automatically:
    emu blob fetch seabios

  To download manually:
    1. Visit: https://www.seabios.org/downloads/seabios-1.16.3-prebuilt.bin
    2. Save to: /var/emu/blobs/seabios/seabios.bin
    3. Verify checksum: emu blob verify seabios

  To build from source:
    git clone https://git.seabios.org/seabios.git
    cd seabios && make CONFIG_QEMU=y
    cp out/bios.bin /var/emu/blobs/seabios/seabios.bin

  For more information:
    emu blob info seabios
```

### 9.2 First-Run Experience

When a user runs `emu start` for the first time without blobs:

```
$ emu start --name test --arch amd64
ERROR: Required firmware blob not found: seabios

  This is your first time running the emulator. You need to download
  firmware blobs before starting an instance.

  Run: emu blob fetch --arch amd64

  This will download SeaBIOS (or OVMF for UEFI) to /var/emu/blobs/.

  If you prefer to download manually, see:
    emu blob info seabios
    emu blob info ovmf-x64
```

### 9.3 Documentation

Each blob's documentation includes:

- **Source URL**: Where to download the blob
- **License**: The license under which the blob is distributed
- **Verification**: How to verify the blob's integrity (SHA-256)
- **Build instructions**: How to build the blob from source
- **Placement**: Where to place the blob in the cache directory
- **Dependencies**: Any tools needed (e.g., `dtc` for DTB compilation)

---

## 10. Security Considerations

### 10.1 Blob Integrity

| Threat | Mitigation |
|--------|------------|
| Man-in-the-middle attack on download | SHA-256 checksum verification after download |
| Corrupted download | Checksum mismatch → delete and retry |
| Stale/outdated blob | Version tracking in manifest, `--outdated` flag |
| Malicious blob substitution | Checksums signed with the emulator's release key (future) |

### 10.2 Blob Execution

Firmware blobs are executed in the emulated environment, not on the host:

- Blobs are loaded into emulated guest memory, not host memory
- Blobs run inside the emulator's fetch-decode-execute loop
- Blobs cannot access host filesystem, network, or processes
- Blobs are subject to the same Capsicum sandboxing as the rest of the emulator

### 10.3 Cache Permissions

| Cache Location | Permissions | Owner |
|----------------|-------------|-------|
| `/var/emu/blobs/` | `0755` directories, `0644` files | `root:emu` |
| `~/.cache/emu/blobs/` | `0700` directories, `0600` files | User |

### 10.4 Supply Chain Security

- All blob URLs point to official upstream releases or well-known mirrors
- SHA-256 checksums are verified after every download
- The manifest (`blobs.json`) is shipped with the emulator and updated via `emu blob update`
- Future enhancement: GPG-signed manifests for additional trust

---

## 11. Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| BLB.1 | Create `usr.sbin/emu/blobs.json` manifest | NOT STARTED | | | `usr.sbin/emu/blobs.json` | Define all blobs with URLs, checksums, licenses, and metadata. Include all architectures. |
| BLB.2 | Implement `emu_blob.c` — blob subcommand framework | NOT STARTED | | | `usr.sbin/emu/emu_blob.c` | Command dispatch for `emu blob` subcommands. Parse subcommand and options. |
| BLB.3 | Implement `emu_blob_fetch.c` — blob download | NOT STARTED | | BLB.1, BLB.2 | `usr.sbin/emu/emu_blob_fetch.c` | HTTP download with progress. Archive extraction (zip, tar.gz). Checksum verification. Cache directory creation. |
| BLB.4 | Implement `emu_blob_list.c` — blob listing | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_list.c` | List all blobs with status. Filter by arch, installed, missing, outdated. JSON output. |
| BLB.5 | Implement `emu_blob_verify.c` — checksum verification | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_verify.c` | Read checksum files, compare with actual files. Report mismatches. `--fix` option. |
| BLB.6 | Implement `emu_blob_remove.c` — blob removal | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_remove.c` | Remove blob files from cache. Confirmation prompt. `--force` option. |
| BLB.7 | Implement `emu_blob_build.c` — blob building | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_build.c` | Build DTB blobs from DTS source. Invoke `dtc`. Print build instructions for other blobs. |
| BLB.8 | Implement `emu_blob_info.c` — blob information | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_info.c` | Display detailed blob info. JSON output. |
| BLB.9 | Implement `emu_blob_path.c` — path resolution | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_path.c` | Resolve blob path using cache resolution order. Exit with error if not found. |
| BLB.10 | Implement `emu_blob_update.c` — manifest update | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_update.c` | Fetch updated manifest. Merge with local. Report changes. |
| BLB.11 | Implement `emu_blob_cache.c` — cache management | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu_blob_cache.c` | Cache directory resolution (system-wide, user, instance override). Directory creation with correct permissions. |
| BLB.12 | Implement `emu_blob_resolve()` API in emulator engine | NOT STARTED | | BLB.11 | `usr.sbin/emu/emu_firmware.c` | `emu_blob_resolve()` function used by firmware loading code. Print helpful error messages for missing blobs. |
| BLB.13 | Create DTS source files for device tree blobs | NOT STARTED | | | `usr.sbin/emu/dts/virt-arm64.dts`, `usr.sbin/emu/dts/virt-riscv.dts` | Device tree source describing emulated platform: CPU, GIC, UART, timer, virtio devices, RTC, system reset. |
| BLB.14 | Update `emu_arch_amd64.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_amd64.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. |
| BLB.15 | Update `emu_arch_arm64.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_arm64.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. Load DTB. |
| BLB.16 | Update `emu_arch_riscv.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_riscv.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. Load DTB. |
| BLB.17 | Update `emu_arch_i386.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_i386.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. |
| BLB.18 | Update `emu_arch_arm.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_arm.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. |
| BLB.19 | Update `emu_arch_ppc.c` to use blob resolution | NOT STARTED | | BLB.12 | `usr.sbin/emu/emu_arch_ppc.c` | Replace hardcoded firmware paths with `emu_blob_resolve()`. |
| BLB.20 | Add `blobs`, `blobs-build`, `blobs-verify`, `blobs-clean` Make targets | NOT STARTED | | BLB.2 | `usr.sbin/emu/Makefile` | Optional build targets for blob management. Never part of default build. |
| BLB.21 | Add blob cache directory to `.gitignore` | NOT STARTED | | | `.gitignore` | Add `usr.sbin/emu/blobs/` to prevent accidental commits. |
| BLB.22 | Add `emu blob` subcommand to main CLI dispatch | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu.c` | Register `blob` as a subcommand in the main CLI entry point. |
| BLB.23 | Write blob management unit tests | NOT STARTED | | BLB.2-BLB.12 | `tests/usr.sbin/emu/blob_test.c` | Test manifest parsing, download (mock HTTP), checksum verification, cache resolution, path resolution. |
| BLB.24 | Write blob management integration tests | NOT STARTED | | BLB.23 | `tests/usr.sbin/emu/blob_integration_test.sh` | Test full fetch/verify/list/remove lifecycle. Test error messages for missing blobs. Test DTB building. |
| BLB.25 | Write blob documentation for man pages | NOT STARTED | | BLB.2 | `usr.sbin/emu/emu.8` | Document `emu blob` subcommand and all sub-subcommands. Include examples. |

---

## 12. Comprehensive Test Plan

This section defines the complete test suite for the blob management system and CPU model database. Tests are organized by component and functionality. Each test includes:

- **Test ID**: Unique identifier for traceability
- **Category**: Which component/functionality is being tested
- **Test Description**: What the test does
- **Input**: Test inputs and conditions
- **Expected Behavior**: What should happen on success
- **Failure Modes**: How the test should fail and what errors to expect
- **Edge Cases**: Boundary conditions and corner cases to verify

### 12.1 Blob Manifest Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-MAN-001 | Manifest Parsing | Parse valid `blobs.json` manifest | Well-formed JSON with all blob entries | Returns parsed manifest with all fields populated correctly | Malformed JSON → parse error; Missing required fields → validation error | Empty manifest (no blobs); Single blob; Maximum blobs |
| T-MAN-002 | Manifest Parsing | Parse manifest with missing required fields | JSON missing `version`, `blobs`, or blob sub-fields | Returns validation error identifying missing field | Silent acceptance of invalid manifest → bug | Missing `url` but has `build_method` (DTB case); Missing `sha256` |
| T-MAN-003 | Manifest Parsing | Parse manifest with unknown blob IDs | Manifest with unrecognized blob IDs | Returns warning for unknown IDs, continues parsing known ones | Crash on unknown ID → bug | All IDs unknown; Mix of known and unknown |
| T-MAN-004 | Manifest Parsing | Parse manifest with version mismatch | Manifest with `version` field different from expected | Returns version mismatch warning, attempts forward-compatible parsing | Crash on version mismatch → bug | Newer version with extra fields; Older version with missing fields |
| T-MAN-005 | Manifest Validation | Validate blob checksum format | Manifest with valid/invalid SHA-256 strings | Validates SHA-256 format (64 hex chars) | Accepts invalid hex → bug; Rejects valid hex → bug | Mixed case hex; Short hash; Hash with non-hex characters |
| T-MAN-006 | Manifest Validation | Validate blob URL format | Manifest with valid/invalid URLs | Validates URL format (http/https) | Accepts invalid URL → bug; Rejects valid URL → bug | FTP URLs; File URLs; URLs with authentication; Empty URLs (DTB case) |
| T-MAN-007 | Manifest Validation | Validate architecture list | Manifest with valid/invalid arch strings | Validates against known architecture list | Accepts unknown arch → bug; Rejects valid arch → bug | Empty arch list; Single arch; All architectures |
| T-MAN-008 | Manifest Versioning | Check manifest version compatibility | Manifest v1, v2 (future), v0 (invalid) | v1 accepted; v2 accepted with forward-compat; v0 rejected | Rejects v1 → bug; Accepts v0 → bug | Version as string vs integer; Missing version field |

### 12.2 Blob Download Tests (emu blob fetch)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-FETCH-001 | Download | Download a single blob successfully | Valid blob ID with reachable URL | Downloads file, verifies checksum, places in cache directory | Network timeout → retry with backoff; Connection refused → clear error message | Very large blob (>100MB); Very small blob (<1KB) |
| T-FETCH-002 | Download | Download multiple blobs simultaneously | Multiple valid blob IDs | Downloads all blobs, reports success/failure per blob | One blob fails → others continue; All fail → aggregate error | 0 blobs (no-op); 10+ blobs (stress test) |
| T-FETCH-003 | Download | Download blob that is already cached | Already-cached blob ID | Skips download, reports "already cached" | Re-downloads unnecessarily → wasted bandwidth | Cached blob with --force flag → re-downloads |
| T-FETCH-004 | Download | Download with --force flag | Already-cached blob ID with --force | Re-downloads and overwrites cached version | Fails to overwrite → stale cache; Deletes old before new succeeds → data loss | Corrupted cached file → --force fixes it |
| T-FETCH-005 | Download | Download with --arch filter | `--arch amd64` flag | Downloads only blobs for amd64 architecture | Downloads wrong arch blobs → bug; Downloads nothing when blobs exist → bug | Arch with no blobs; Arch with single blob; All archs |
| T-FETCH-006 | Download | Download with --all flag | `--all` flag | Downloads all blobs for all architectures | Misses some blobs → bug; Downloads blobs for unsupported archs → bug | Empty manifest; Single blob manifest |
| T-FETCH-007 | Download | Download from URL that returns redirect | URL with HTTP 301/302 redirect | Follows redirect, downloads from final URL | Fails to follow redirect → broken download; Follows redirect to wrong URL → security issue | Redirect chain (3+ hops); Redirect to HTTPS; Redirect to different domain |
| T-FETCH-008 | Download | Download from URL that returns error | URL returning HTTP 404/403/500 | Reports HTTP error with status code and URL | Retries indefinitely → hangs; Silent failure → confusing | 404 (not found); 403 (forbidden); 500 (server error); 429 (rate limit) |
| T-FETCH-009 | Download | Download with checksum verification | Valid blob with matching checksum | Verifies SHA-256 after download, reports success | Skips verification → security issue; Fails on valid checksum → bug | Checksum file missing → warning but continue; Multiple checksum files |
| T-FETCH-010 | Download | Download with checksum mismatch | Valid blob with intentionally wrong checksum in manifest | Detects mismatch, deletes downloaded file, reports corruption error | Keeps corrupted file → security issue; Reports success on mismatch → bug | Partial download (interrupted); Download of wrong file; Corrupted in transit |
| T-FETCH-011 | Download | Download with --no-verify flag | Valid blob with --no-verify | Downloads without checksum verification | Still verifies → ignores flag; Skips verification silently → misleading | Corrupted download with --no-verify → no error (expected) |
| T-FETCH-012 | Download | Download to custom cache directory | `--cache-dir /tmp/test-cache` | Downloads to specified directory instead of default | Downloads to default anyway → bug; Fails if custom dir doesn't exist → should create | Custom dir with existing blobs; Custom dir on different filesystem |
| T-FETCH-013 | Download | Download with archive extraction | Blob URL pointing to zip/tar.gz archive | Downloads archive, extracts files to blob directory | Fails to extract → broken blob; Extracts to wrong directory → bug | Nested archives; Archive with directory structure; Empty archive |
| T-FETCH-014 | Download | Download with progress display | Terminal with --progress flag | Shows progress bar with percentage, speed, ETA | No progress shown → poor UX; Progress causes performance issues → bug | Pipe (non-TTY) → progress suppressed; Very fast download → progress flashes briefly |
| T-FETCH-015 | Download | Network timeout handling | Unreachable URL with short timeout | Retries with exponential backoff (3 attempts), then reports failure | Retries forever → hangs; Gives up immediately → fragile | Intermittent network; DNS failure; Connection timeout vs read timeout |

### 12.3 Blob List Tests (emu blob list)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-LIST-001 | Listing | List all blobs with status | No filters | Shows table with all blobs, their status (installed/missing/outdated), version, size | Crashes on empty cache → bug; Shows incorrect status → bug | No blobs installed; All blobs installed; Mix of installed/missing |
| T-LIST-002 | Listing | List blobs filtered by architecture | `--arch arm64` | Shows only blobs for arm64 architecture | Shows blobs for wrong arch → bug; Shows nothing when blobs exist → bug | Arch with no blobs; Arch with all blobs installed |
| T-LIST-003 | Listing | List only installed blobs | `--installed` | Shows only blobs that are present in cache | Shows missing blobs → bug; Shows nothing when blobs installed → bug | No installed blobs; All installed |
| T-LIST-004 | Listing | List only missing blobs | `--missing` | Shows only blobs not present in cache | Shows installed blobs → bug | No missing blobs; All missing |
| T-LIST-005 | Listing | List only outdated blobs | `--outdated` | Shows only blobs with newer version available | Shows up-to-date blobs → bug; Misses outdated blobs → bug | No outdated blobs; All outdated; Version string comparison edge cases |
| T-LIST-006 | Listing | List with JSON output | `--json` | Outputs valid JSON array of blob objects | Invalid JSON → bug; Missing fields → bug; Wrong structure → bug | Empty list → empty JSON array; Single blob → single-element array |
| T-LIST-007 | Listing | List with combined filters | `--arch amd64 --installed` | Shows only installed amd64 blobs | Ignores one filter → bug; Returns empty when results exist → bug | Conflicting filters (no matches); All filters match everything |
| T-LIST-008 | Listing | List CPU models | `--cpu-models` flag | Shows available CPU models for the architecture | Shows blob data instead → bug; Missing CPU model info → bug | Arch with no CPU models; All models listed |

### 12.4 Blob Verify Tests (emu blob verify)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-VFY-001 | Verification | Verify all installed blobs | `--all` flag | Reads checksum files, compares with actual files, reports all as valid | Reports valid as invalid → false positive; Reports invalid as valid → false negative | Empty cache → no-op; Single blob; Many blobs |
| T-VFY-002 | Verification | Verify specific blob | Specific blob ID | Verifies only that blob's checksum | Verifies wrong blob → bug; Verifies all blobs → ignores filter | Blob not installed → reports missing; Unknown blob ID → error |
| T-VFY-003 | Verification | Verify with checksum mismatch | Blob with intentionally corrupted content | Detects mismatch, reports which file failed, which checksum expected vs actual | Silent on mismatch → security issue; Wrong error message → confusing | Single bit flip; Entirely different file; Empty file; Truncated file |
| T-VFY-004 | Verification | Verify with --fix flag | Corrupted blob with --fix | Re-downloads corrupted blob, verifies new download | Fails to re-download → corruption persists; Re-downloads valid blob unnecessarily | Network unavailable during --fix → reports failure; Multiple corrupted blobs |
| T-VFY-005 | Verification | Verify blob with missing checksum file | Blob installed but no .sha256 file | Reports warning about missing checksum, skips verification | Crashes on missing checksum → bug; Assumes valid without checksum → security issue | Checksum file deleted; Checksum file permissions issue |
| T-VFY-006 | Verification | Verify blob with wrong file permissions | Blob file with 0000 permissions | Reports permission error, suggests fix | Silent failure → confusing; Crashes → bug | Read-only filesystem; File owned by different user |

### 12.5 Blob Remove Tests (emu blob remove)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-RMV-001 | Removal | Remove a single blob | Specific blob ID | Removes blob files and checksum files from cache | Leaves orphaned files → bug; Removes wrong blob → bug | Blob not installed → reports not found; Blob with multiple files |
| T-RMV-002 | Removal | Remove all blobs | `--all` flag | Removes all blob files and directories from cache | Leaves some files → bug; Removes non-blob files → data loss | Empty cache → no-op; Cache with non-blob files (should not touch) |
| T-RMV-003 | Removal | Remove with confirmation prompt | Blob ID without --force | Prompts for confirmation before removing | Removes without prompt → data loss; Prompts in non-interactive mode → hangs | Non-interactive terminal → should skip prompt; --force flag → skip prompt |
| T-RMV-004 | Removal | Remove with --force flag | Blob ID with --force | Removes without confirmation prompt | Still prompts → ignores flag; Removes without any safety check → data loss | Removing blobs in use by running instance → should warn |
| T-RMV-005 | Removal | Remove by architecture | `--arch amd64` | Removes only blobs for specified architecture | Removes wrong arch → bug; Removes nothing when blobs exist → bug | Arch with no installed blobs; All archs |

### 12.6 Blob Build Tests (emu blob build)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-BLD-001 | Building | Build DTB blob from DTS source | `dtb-virt-arm64` blob ID | Invokes `dtc`, compiles DTS to DTB, places in cache | `dtc` not installed → clear error with install instructions; Compilation error → reports DTS error | DTS with syntax error; DTS with warnings; DTS with includes |
| T-BLD-002 | Building | Build DTB with custom dtc path | `--dtc-path /usr/local/bin/dtc` | Uses specified dtc binary | Uses wrong dtc → wrong output; Fails if dtc not at path → clear error | dtc path with spaces; dtc path that doesn't exist |
| T-BLD-003 | Building | Build non-DTB blob (e.g., SeaBIOS) | `seabios` blob ID | Prints build instructions (clone repo, make, copy output) | Tries to build automatically → unexpected dependency; Silent no-op → confusing | Blob with no build_from_source entry; Blob with null repository |
| T-BLD-004 | Building | Build all buildable blobs | `--all` flag | Builds all blobs that have build methods (DTBs) | Tries to build non-buildable blobs → bug; Misses buildable blobs → bug | No buildable blobs; Mix of buildable and non-buildable |

### 12.7 Blob Info Tests (emu blob info)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-INF-001 | Information | Show info for specific blob | Valid blob ID | Displays all fields: name, description, version, license, URL, checksum, size, load address, status | Missing fields → bug; Wrong values → bug | Blob not installed → shows info but marks as missing; Unknown blob ID → error |
| T-INF-002 | Information | Show info with JSON output | `--json` flag | Outputs valid JSON with all blob fields | Invalid JSON → bug; Missing fields → bug | Blob with null fields (DTB with no URL); Blob with multiple files |
| T-INF-003 | Information | Show info for blob with build instructions | Blob with build_from_source | Displays build instructions, repository URL, output path | Missing build info → bug; Wrong build info → bug | Blob with null build_from_source; Blob with only build instructions (no URL) |

### 12.8 Blob Path Tests (emu blob path)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-PTH-001 | Path Resolution | Resolve path to blob directory | Blob ID without filename | Prints path to blob's cache directory | Prints wrong path → bug; Prints file path instead → bug | Blob not installed → exit code 5 with error message |
| T-PTH-002 | Path Resolution | Resolve path to specific blob file | Blob ID with filename | Prints full path to specific file | Prints wrong file → bug; Prints directory path → bug | File doesn't exist → exit code 5; Multiple files in blob |
| T-PTH-003 | Path Resolution | Resolve path with cache resolution order | Blob in user cache only | Finds blob in user cache (not system cache) | Finds in wrong cache location → bug; Misses blob in user cache → bug | Blob in system cache only; Blob in both caches (system takes priority); Blob in instance override |
| T-PTH-004 | Path Resolution | Resolve path for scripting | Command in shell script | Returns clean path without extra output, suitable for variable assignment | Extra output (progress, status) → breaks scripts; Trailing newline issues → bug | Path with spaces; Path with special characters |

### 12.9 Blob Update Tests (emu blob update)

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-UPD-001 | Update | Update manifest from upstream | No options | Fetches latest manifest, merges with local, reports changes | Network error → keeps local manifest; Merge conflict → reports conflict | No changes (same version); New blobs added; Blobs removed; Version bump |
| T-UPD-002 | Update | Check for updates without downloading | `--check-only` | Compares local version with upstream, reports if update available | Downloads anyway → ignores flag; Reports wrong version → bug | Local is latest; Local is outdated; Local is newer (downgrade needed) |
| T-UPD-003 | Update | Update with custom URL | `--url https://example.com/manifest.json` | Fetches manifest from specified URL | Ignores custom URL → bug; Fails on invalid URL → clear error | URL returns invalid manifest; URL returns redirect |

### 12.10 Cache Management Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-CCH-001 | Cache | Create cache directory structure | First blob fetch | Creates `/var/emu/blobs/` (or `~/.cache/emu/blobs/`) with correct permissions | Fails to create → download fails; Wrong permissions → security issue | Parent directory doesn't exist; Read-only filesystem; Disk full |
| T-CCH-002 | Cache | Cache resolution order | Blob in multiple cache locations | Returns path from highest-priority location (instance > system > user > build) | Returns wrong priority → bug; Misses blob in higher priority → bug | Instance override with different version; All locations have the blob |
| T-CCH-003 | Cache | Cache with non-root user | Non-root user running emu blob | Uses `~/.cache/emu/blobs/` instead of `/var/emu/blobs/` | Tries to write to `/var/emu/blobs/` → permission error; Falls back incorrectly → bug | User with write access to `/var/emu/blobs/` (emu group); User with no home directory |
| T-CCH-004 | Cache | Cache with EMU_BLOB_DIR override | Environment variable set | Uses specified directory instead of default | Ignores env var → bug; Fails if dir doesn't exist → should create | Env var points to relative path; Env var with trailing slash |
| T-CCH-005 | Cache | Concurrent cache access | Multiple simultaneous blob operations | Proper locking, no corrupted files | Race condition → corrupted cache; Deadlock → hangs | Two fetches of same blob; Fetch and verify simultaneously; Fetch and remove simultaneously |

### 12.11 CPU Model Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-CPU-001 | CPU Models | Parse valid `cpu_models.json` | Well-formed JSON with all CPU models | Returns parsed model database with all fields | Malformed JSON → parse error; Missing fields → validation error | Empty models list; Single model; All architectures |
| T-CPU-002 | CPU Models | Select CPU model by ID | `x86-64-v3` model ID | Returns full model specification for that ID | Unknown ID → error; Wrong model returned → bug | Case sensitivity (X86-64-V3 vs x86-64-v3); Model with special characters in ID |
| T-CPU-003 | CPU Models | List CPU models for architecture | `--arch amd64` | Returns only models for that architecture | Returns models for wrong arch → bug; Returns empty when models exist → bug | Arch with no models; Arch with many models |
| T-CPU-004 | CPU Models | Apply CPU model to instance config | Model ID + instance config | Sets CPUID features, MSR values, timer frequencies, cache topology from model | Missing features → wrong emulation; Wrong values → incorrect behavior | Model with partial data (some null fields); Model with conflicting settings |
| T-CPU-005 | CPU Models | CPU model with speed override | Model ID + `--cpu-speed 100` | Uses model's feature set but overridden speed | Ignores speed override → bug; Overrides features too → bug | Speed of 0 (use default); Very high speed; Very low speed |
| T-CPU-006 | CPU Models | CPU model compatibility validation | Model ID + architecture | Validates model is compatible with specified architecture | Allows incompatible model → wrong emulation; Rejects compatible model → bug | Model for different arch; Model for same arch but different ISA version |
| T-CPU-007 | CPU Models | CPU model JSON schema validation | Model with invalid fields | Validates against schema, reports validation errors | Accepts invalid model → bug; Rejects valid model → bug | Missing required fields; Extra unknown fields; Wrong types (string instead of int) |
| T-CPU-008 | CPU Models | CPU model for legacy compatibility | `i386` model with `--cpu-speed 66` | Sets up 66MHz CPU for Windows 95 compatibility | Speed too high → Windows 95 crashes; Missing legacy features → boot failure | Speed exactly at known problematic threshold (2GHz for Win95); Model with known errata |

### 12.12 Integration Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-INT-001 | Integration | Full blob lifecycle | Fetch → verify → list → info → path → remove | All commands succeed in sequence, cache is clean at end | Any step fails → integration broken; Cache left dirty → side effects | Interrupted lifecycle (remove before verify); Repeated lifecycle |
| T-INT-002 | Integration | Missing blob error message | Start instance without required blob | Clear error message with blob ID, download command, manual instructions | Generic error → poor UX; Wrong blob ID → confusing; Missing download URL → unhelpful | Multiple missing blobs → lists all; Blob for wrong arch → suggests correct arch |
| T-INT-003 | Integration | First-run experience | Fresh install, no blobs | Helpful message guiding user through first blob fetch | No guidance → user confused; Technical error → intimidating | User with no network access → suggests manual download; User without permissions → suggests sudo |
| T-INT-004 | Integration | Blob resolution in firmware loading | Instance start with cached blob | Firmware loaded from cache, instance boots successfully | Blob not found → boot failure; Wrong blob loaded → boot failure | Blob in user cache (non-root); Blob in system cache (root); Blob in instance override |
| T-INT-005 | Integration | DTB building and loading | Build DTB → start arm64 instance | DTB compiled, loaded, instance recognizes devices | DTB missing devices → device not found; DTB wrong addresses → MMIO fault | DTB with all devices; DTB with minimal devices; DTB with custom device |
| T-INT-006 | Integration | CPU model selection and boot | Select CPU model → start instance | Instance boots with correct CPU features exposed | Wrong features → software crashes; Missing features → boot failure | Legacy CPU model (i386 at 66MHz); Modern CPU model (x86-64-v4); Cross-arch model |
| T-INT-007 | Integration | Network failure handling | Fetch blob with network unavailable | Graceful error, suggests manual download | Crash → poor UX; Indefinite hang → worse UX | Intermittent network; DNS failure; Proxy required but not configured |
| T-INT-008 | Integration | Permission handling | Non-root user without emu group | Appropriate permission error, suggests sudo or group membership | Silent failure → confusing; Crash → poor UX | User in emu group → allowed; Root → allowed; Regular user → denied with guidance |

### 12.13 Error Handling & Edge Case Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-ERR-001 | Error Handling | Unknown blob ID | Non-existent blob ID | Clear error: "Unknown blob ID: <id>. Use `emu blob list` to see available blobs." | Generic error → poor UX; Crash → bug | ID with special characters; ID that looks like a file path; Empty ID |
| T-ERR-002 | Error Handling | No subcommand | `emu blob` with no arguments | Shows usage/help for blob subcommand | Silent → confusing; Crash → bug | Just `emu blob`; `emu blob --help` |
| T-ERR-003 | Error Handling | Invalid option combination | `--installed --missing` (conflicting) | Error: "Cannot combine --installed and --missing flags" | Silent ignores one → misleading; Crash → bug | `--all --arch` (valid); `--force` without blob ID |
| T-ERR-004 | Error Handling | Disk full during download | Download to full filesystem | Error: "No space left on device. Free space: X bytes, needed: Y bytes." | Silent failure → corrupted cache; Crash → bug | Partial download then full; Download exactly fills remaining space |
| T-ERR-005 | Error Handling | Permission denied on cache | Cache directory with wrong ownership | Error: "Permission denied: <path>. Try: sudo emu blob fetch <id>" | Silent failure → confusing; Wrong fix suggestion → unhelpful | File owned by root, user is non-root; File with immutable flag |
| T-ERR-006 | Error Handling | Interrupted download (SIGINT) | Ctrl+C during download | Cleanly terminates, removes partial download | Leaves partial file → corrupted cache; Doesn't clean up → wasted space | Interrupt during checksum verification; Interrupt during archive extraction |
| T-ERR-007 | Error Handling | Corrupted manifest file | Manually corrupted blobs.json | Error: "Manifest corrupted. Run `emu blob update` to restore." | Silent corruption → wrong URLs/checksums; Crash → bug | JSON parse error; Missing blob entries; Wrong checksum format |
| T-ERR-008 | Error Handling | Unsupported architecture | `--arch mips` | Error: "Unsupported architecture: mips. Supported: amd64, i386, arm64, arm, powerpc, riscv" | Silent → confusing; Crash → bug | Arch with no blobs defined; Arch name with different case |

### 12.14 Performance & Stress Tests

| Test ID | Category | Description | Input | Expected Behavior | Failure Modes | Edge Cases |
|---------|----------|-------------|-------|-------------------|---------------|------------|
| T-PRF-001 | Performance | Download large blob | 500MB+ blob | Download completes within reasonable time, progress bar updates smoothly | Timeout → failure; Memory exhaustion → crash | Very slow network; Very fast network |
| T-PRF-002 | Performance | List with many blobs | 100+ blobs in manifest | List displays within 1 second | Slow rendering → poor UX; Memory exhaustion → crash | All installed; All missing; Mix |
| T-PRF-003 | Performance | Verify many blobs | 50+ installed blobs | Verification completes within reasonable time | Slow checksumming → poor UX; I/O bottleneck → slow | All on HDD; All on SSD; Mix of fast/slow storage |
| T-PRF-004 | Performance | Concurrent operations | 10 simultaneous blob fetches | All complete, no race conditions, no corrupted files | Network congestion → timeouts; File locking issues → corruption | All different blobs; Same blob 10 times; Mix of fetch/verify/remove |
| T-PRF-005 | Performance | Cache with many files | 1000+ files in cache directory | List and verify operations complete quickly | Slow directory traversal → poor UX; Filesystem limits reached → errors | Deep directory structure; Files with long names |

---

## 13. Cross-References

### 13.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 5 (Custom Emulator Engine), Phase 6 (Userland Tooling). The `emu blob` subcommand is part of the userland tooling. |
| `002-Emulation-Security-FS.md` | Security architecture. Blob integrity verification (Section 9.1), cache permissions (Section 9.3), supply chain security (Section 9.4). |
| `003-Emulation-Arch-amd64.md` | amd64 firmware requirements (SeaBIOS, OVMF). Task AMD64.26 (firmware loading). |
| `004-Emulation-Arch-i386.md` | i386 firmware requirements (SeaBIOS). Task I386.13. |
| `005-Emulation-Arch-arm64.md` | arm64 firmware requirements (U-Boot, OVMF, DTB). Tasks ARM64.23, ARM64.24. |
| `006-Emulation-Arch-arm.md` | arm firmware requirements (U-Boot). |
| `007-Emulation-Arch-powerpc.md` | powerpc firmware requirements (U-Boot). |
| `008-Emulation-Arch-riscv.md` | RISC-V firmware requirements (OpenSBI, U-Boot, DTB). Tasks RISCV.18, RISCV.19, RISCV.20. |
| `009-Emulation-Devices.md` | Device emulation. Firmware devices section (Section 8). Tasks DEV.22-DEV.25. |

### 13.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| SeaBIOS | https://www.seabios.org/ | Legacy BIOS firmware |
| OVMF (TianoCore) | https://github.com/tianocore/edk2 | UEFI firmware |
| U-Boot | https://github.com/u-boot/u-boot | Boot loader |
| OpenSBI | https://github.com/riscv-software-src/opensbi | RISC-V M-mode firmware |
| Device Tree Specification | https://www.devicetree.org/ | DTS/DTB format |
| `dtc` (Device Tree Compiler) | https://git.kernel.org/pub/scm/utils/dtc/dtc.git | DTS → DTB compilation |

### 13.3 Shared Infrastructure

| Component | Shared With | Location |
|-----------|-------------|----------|
| HTTP download utility | All `emu blob fetch` commands | `usr.sbin/emu/emu_blob_fetch.c` |
| Checksum verification | All blob commands | `usr.sbin/emu/emu_blob_verify.c` |
| Cache directory resolution | All blob commands, firmware loading | `usr.sbin/emu/emu_blob_cache.c` |
| Manifest parsing | All blob commands | `usr.sbin/emu/blobs.json` |
| Firmware loading | All architecture frontends | `usr.sbin/emu/emu_firmware.c` |

---

## 14. Task Completion Checklist

> **Note for agents:** When picking up a task, fill in the **Assigned To** column with your agent name/ID. When completing a task, update the **Status** column to `COMPLETED` and add your name/ID to the **Assigned To** column if not already filled. This ensures traceability across sessions.

| # | Item | Category | Status | Assigned To | Dependencies | Files | Notes |
|---|------|----------|--------|------------|--------------|-------|-------|
| BC.1 | `usr.sbin/emu/blobs.json` manifest created | Manifest | NOT STARTED | | | `usr.skin/emu/blobs.json` | All blobs defined with URLs, checksums, licenses |
| BC.2 | `emu blob fetch` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_fetch.c` | Download, verify, extract |
| BC.3 | `emu blob list` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_list.c` | List with status, filters, JSON |
| BC.4 | `emu blob verify` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_verify.c` | Checksum verification |
| BC.5 | `emu blob remove` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_remove.c` | Cache cleanup |
| BC.6 | `emu blob build` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_build.c` | DTB compilation |
| BC.7 | `emu blob info` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_info.c` | Detailed blob info |
| BC.8 | `emu blob path` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_path.c` | Path resolution for scripting |
| BC.9 | `emu blob update` implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_update.c` | Manifest update |
| BC.10 | Cache management (`emu_blob_cache.c`) implemented | Tool | NOT STARTED | | BC.1 | `usr.sbin/emu/emu_blob_cache.c` | Cache resolution, permissions |
| BC.11 | `emu_blob_resolve()` API implemented | Engine | NOT STARTED | | BC.10 | `usr.sbin/emu/emu_firmware.c` | Blob resolution for firmware loading |
| BC.12 | DTS source files created | Source | NOT STARTED | | | `usr.sbin/emu/dts/virt-arm64.dts`, `virt-riscv.dts` | Device tree source |
| BC.13 | Architecture frontends updated to use blob resolution | Integration | NOT STARTED | | BC.11 | Phase 5 files | amd64, arm64, riscv, i386, arm, powerpc |
| BC.14 | Make targets for blob management | Build | NOT STARTED | | BC.2 | `usr.sbin/emu/Makefile` | `blobs`, `blobs-build`, `blobs-verify`, `blobs-clean` |
| BC.15 | Blob cache added to `.gitignore` | Build | NOT STARTED | | | `.gitignore` | Prevent accidental commits |
| BC.16 | `emu blob` registered in main CLI | Tool | NOT STARTED | | BC.2 | `usr.sbin/emu/emu.c` | Subcommand dispatch |
| BC.17 | Unit tests for blob management | Testing | NOT STARTED | | BC.2-BC.12 | `tests/usr.sbin/emu/blob_test.c` | Manifest, download, verify, cache |
| BC.18 | Integration tests for blob management | Testing | NOT STARTED | | BC.17 | `tests/usr.sbin/emu/blob_integration_test.sh` | Full lifecycle, error messages |
| BC.19 | Blob documentation in man pages | Documentation | NOT STARTED | | BC.2 | `usr.sbin/emu/emu.8` | `emu blob` subcommand docs |

---

## 15. Future Enhancements

1. **GPG-signed manifests**: Sign `blobs.json` with the emulator's release key for additional supply chain security
2. **Package-based blobs**: Provide blobs as FreeBSD packages (`pkg install emu-blobs-seabios`)
3. **Automatic blob fetching on start**: `emu start` could auto-fetch missing blobs with user confirmation
4. **Blob mirror support**: Allow configuring alternative download mirrors in `emu.conf`
5. **Blob caching proxy**: Support for an HTTP proxy for blob downloads in restricted environments
6. **Blob version pinning**: Allow pinning specific blob versions per-instance for reproducibility
7. **Blob diff tool**: Compare installed blob versions against manifest and show changes
8. **Offline mode**: Support for pre-packaged blob bundles for air-gapped environments
9. **Blob integrity monitoring**: Periodic checksum verification of installed blobs
10. **Custom blob repositories**: Allow third-party blob repositories for custom firmware

---

## 16. Conclusion

The blob management system provides a clean separation between the emulator framework and the firmware blobs it needs to operate:

- **No blobs in the FreeBSD source tree** — the source tree contains only the manifest and tooling
- **No blobs in FreeBSD releases** — blobs are fetched at runtime by the user
- **Clear user guidance** — every missing blob produces an actionable error message
- **Multiple acquisition methods** — automatic download, manual download, or build from source
- **Integrity verification** — SHA-256 checksums ensure blobs haven't been tampered with
- **Cache hierarchy** — system-wide, per-user, and per-instance cache locations
- **Scriptable** — `emu blob path` enables easy integration with build scripts and automation

This approach aligns with FreeBSD's philosophy of keeping the base system lean while providing tooling to fetch optional components as needed.
