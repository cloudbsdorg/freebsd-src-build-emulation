# Emulated Device Models — Implementation Plan

## 1. Overview

This document describes the device models that must be emulated for each target architecture. Devices are categorized by type and shared across architectures where possible.

### 1.1 Device Sharing Matrix

| Device | amd64 | i386 | arm64 | arm | powerpc | riscv |
|--------|-------|------|-------|-----|---------|-------|
| NS16550 UART | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| PL011 UART | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| i8254 PIT | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| i8259 PIC | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| I/O APIC | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| LAPIC | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| HPET | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| MC146818 RTC | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| ACPI (FADT/MADT/DSDT) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| AHCI/SATA | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| GICv3 (Dist/Redist/CPU IF) | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| GICv2 | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ |
| ARM Generic Timer | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| SP804 Timer | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ |
| PL031 RTC | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| CLINT | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| PLIC | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| OpenPIC | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |
| Decrementer (SPR) | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |
| virtio-blk | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| virtio-net | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| virtio-balloon | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| SeaBIOS (legacy BIOS) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| OVMF (UEFI) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| U-Boot | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ |
| OpenSBI | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| Sound Blaster 16 (ISA) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| Intel HDA (PCI) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| AC97 (PCI) | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ |
| Device I/O Ring Buffer | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## 2. Serial / Console Devices

### 2.1 NS16550 UART

| Property | Value |
|----------|-------|
| **Type** | Serial port (COM) |
| **Base address** | 0x3F8 (COM1, x86), 0x2F8 (COM2, x86), 0x10000000 (RISC-V), 0x1C090000 (PowerPC) |
| **IRQ** | 4 (COM1, x86), 3 (COM2, x86), 10 (RISC-V) |
| **Register spacing** | 1 byte |
| **Architectures** | amd64, i386, powerpc, riscv |

**Registers:**

| Offset | DLAB=0 Read | DLAB=0 Write | DLAB=1 |
|--------|-------------|--------------|--------|
| 0 | RBR (Receive Buffer) | THR (Transmit Holding) | DLL (Divisor Latch Low) |
| 1 | IER (Interrupt Enable) | IER | DLM (Divisor Latch High) |
| 2 | IIR (Interrupt ID) | FCR (FIFO Control) | — |
| 3 | LCR (Line Control) | LCR | — |
| 4 | MCR (Modem Control) | MCR | — |
| 5 | LSR (Line Status) | — | — |
| 6 | MSR (Modem Status) | — | — |
| 7 | SCR (Scratch) | SCR | — |

**Key registers:**
- **LSR** (0x05): Bit 0=DR (data ready), Bit 5=THRE (transmit holding register empty), Bit 6=TEMT (transmitter empty)
- **IER** (0x01): Bit 0=ERBFI (rx interrupt), Bit 1=ETBEI (tx interrupt), Bit 2=ELSI (line status interrupt)
- **IIR** (0x02): Bit 0=0 (interrupt pending), Bits 3-1=interrupt ID (6=RLS, 4=RX, 2=TX, 1=MS)
- **LCR** (0x03): Bits 0-1=word length, Bit 2=stop bits, Bit 3=parity enable, Bit 4=even parity, Bit 7=DLAB

**Emulation approach:**
- Map to a ring buffer for console output capture
- RX data comes from a pipe or pty connected to the emulator's stdin
- TX data goes to the instance console buffer
- Interrupts raised via the arch-specific interrupt controller (I/O APIC, PLIC, OpenPIC)

**Implementation:** `usr.sbin/emu/emu_dev_uart.c`

### 2.2 PL011 UART (ARM PrimeCell)

| Property | Value |
|----------|-------|
| **Type** | Serial port |
| **Base address** | 0x9000000 (arm64), 0x9000000 (arm) |
| **IRQ** | 33 (SPI) |
| **Register spacing** | 4 bytes (32-bit) |
| **Architectures** | arm64, arm |

**Registers:**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | UARTDR | Data Register (read=RX, write=TX) |
| 0x004 | UARTRSR/UARTECR | Receive Status / Error Clear |
| 0x018 | UARTFR | Flag Register (busy, RXFE, RXFF, TXFE, TXFF) |
| 0x020 | UARTILPR | IrDA Low-Power Counter |
| 0x024 | UARTIBRD | Integer Baud Rate Divisor |
| 0x028 | UARTFBRD | Fractional Baud Rate Divisor |
| 0x02C | UARTLCR_H | Line Control (word length, parity, FIFO enable) |
| 0x030 | UARTCR | Control Register (UART enable, TX enable, RX enable) |
| 0x034 | UARTIFLS | Interrupt FIFO Level Select |
| 0x038 | UARTIMSC | Interrupt Mask Set/Clear |
| 0x03C | UARTRIS | Raw Interrupt Status |
| 0x040 | UARTMIS | Masked Interrupt Status |
| 0x044 | UARTICR | Interrupt Clear Register |
| 0x048 | UARTDMACR | DMA Control Register |

**Emulation approach:** Same as NS16550 but with 32-bit register access and different register layout.

**Implementation:** `usr.sbin/emu/emu_dev_uart.c` (shared with NS16550 via abstraction)

---

## 3. Timer Devices

### 3.1 i8254 PIT (Programmable Interval Timer)

| Property | Value |
|----------|-------|
| **Type** | Timer |
| **Base address** | 0x40-0x43 (x86 I/O ports) |
| **IRQ** | 0 (IRQ0, routed to I/O APIC pin 0) |
| **Channels** | 3 (0=IRQ0, 1=DRAM refresh, 2=PC speaker) |
| **Architectures** | amd64, i386 |

**Registers:**

| Port | Read | Write |
|------|------|-------|
| 0x40 | Counter 0 (current count) | Counter 0 (initial count) |
| 0x41 | Counter 1 (current count) | Counter 1 (initial count) |
| 0x42 | Counter 2 (current count) | Counter 2 (initial count) |
| 0x43 | — | Control Word Register |

**Control Word (port 0x43):**
- Bits 7-6: Counter select (00=C0, 01=C1, 10=C2, 11=read-back)
- Bits 5-4: Read/Load mode (00=latch, 01=LSB, 10=MSB, 11=LSB then MSB)
- Bits 3-1: Operating mode (000=interrupt on terminal count, 001=hardware one-shot, 010=rate generator, 011=square wave, 100=software strobe, 101=hardware strobe)
- Bit 0: BCD/Binary (0=binary, 1=BCD)

**Emulation approach:**
- Counter 0 generates IRQ0 at a configurable frequency (default 100Hz for FreeBSD)
- Use host timer to schedule counter decrements
- Support all 6 operating modes
- Latch command saves current count for reading

**Implementation:** `usr.sbin/emu/emu_dev_timer.c`

### 3.2 HPET (High Precision Event Timer)

| Property | Value |
|----------|-------|
| **Type** | Timer |
| **Base address** | 0xFED00000 (MMIO) |
| **IRQ** | Configurable per comparator |
| **Comparators** | 3 (minimum), up to 32 |
| **Frequency** | 10 MHz (typical, 10-20 MHz range) |
| **Architectures** | amd64, i386 |

**Registers (MMIO, 64-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | GCAP_ID | General Capabilities ID (vendor, rev, count, width, legacy) |
| 0x010 | GEN_CONF | General Configuration (enable, legacy routing) |
| 0x020 | GEN_INTR_STA | General Interrupt Status |
| 0x0F0 | MAIN_CNT | Main Counter (64-bit, free-running) |
| 0x100 | TIM0_CONF | Timer 0 Configuration (type, size, enable, periodic, IRQ) |
| 0x108 | TIM0_COMP | Timer 0 Comparator Value |
| 0x110 | TIM0_FSB | Timer 0 FSB Interrupt Route |
| 0x120 | TIM1_CONF | Timer 1 Configuration |
| 0x128 | TIM1_COMP | Timer 1 Comparator Value |
| ... | ... | ... |

**Emulation approach:**
- Main counter increments at configured frequency (default 10MHz)
- Each comparator generates an interrupt when main counter reaches its value
- Periodic timers reload comparator on match
- Legacy routing maps timer 0 to IRQ0 and timer 1 to IRQ8

**Implementation:** `usr.sbin/emu/emu_dev_timer.c`

### 3.3 ARM Generic Timer

| Property | Value |
|----------|-------|
| **Type** | Timer (system registers) |
| **Access** | MRS/MSR system register instructions |
| **IRQ** | PPI 26 (physical timer), PPI 27 (virtual timer) |
| **Architectures** | arm64 |

**Registers (accessed via MRS/MSR):**
- `CNTPCT_EL0` — Physical Count Register (free-running, 64-bit)
- `CNTFRQ_EL0` — Counter Frequency Register
- `CNTP_TVAL_EL0` — Physical Timer Value (countdown, 32-bit)
- `CNTP_CTL_EL0` — Physical Timer Control (enable, mask, status)
- `CNTP_CVAL_EL0` — Physical Timer Compare Value (64-bit)
- `CNTVCT_EL0` — Virtual Count Register
- `CNTV_TVAL_EL0` — Virtual Timer Value
- `CNTV_CTL_EL0` — Virtual Timer Control
- `CNTV_CVAL_EL0` — Virtual Timer Compare Value

**Emulation approach:**
- Implement as part of the CPU emulation (system register access)
- Free-running counter increments at configured frequency (default 10MHz)
- Timer interrupt fires when CNTP_TVAL decrements to 0 or CNTP_CVAL matches CNTPCT
- Interrupt routed to GIC as PPI

**Implementation:** `sys/emulation/arm64/emu_intr_arm64.c`

### 3.4 SP804 Timer (ARM)

| Property | Value |
|----------|-------|
| **Type** | Timer |
| **Base address** | 0x1C110000 (MMIO) |
| **IRQ** | 34 (SPI) |
| **Timers** | 2 (timer 0 and timer 1) |
| **Architectures** | arm |

**Registers (per timer, 32-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | Timer1Load | Load register (initial value) |
| 0x004 | Timer1Value | Current value (read-only) |
| 0x008 | Timer1Control | Control (enable, periodic, prescale, size, interrupt) |
| 0x00C | Timer1IntClr | Interrupt clear |
| 0x010 | Timer1RIS | Raw interrupt status |
| 0x014 | Timer1MIS | Masked interrupt status |
| 0x018 | Timer1BGLoad | Background load register |
| 0x020 | Timer2Load | Same as above for timer 2 |
| ... | ... | ... |

**Emulation approach:** Similar to i8254 but with MMIO access and 32-bit registers.

**Implementation:** `usr.sbin/emu/emu_dev_timer.c`

### 3.5 PowerPC Decrementer

| Property | Value |
|----------|-------|
| **Type** | Timer (SPR) |
| **Access** | MTSPR/MFSPR (SPR 22/23) |
| **IRQ** | 0x0900 (Decrementer interrupt vector) |
| **Architectures** | powerpc |

**Emulation approach:**
- DEC SPR auto-decrements at the time base frequency (typically 512MHz / 16 = 32MHz)
- When DEC reaches 0 or transitions from 0 to negative, a decrementer interrupt is raised
- Implement as part of CPU emulation

**Implementation:** `sys/emulation/powerpc/emu_intr_ppc.c`

### 3.6 RISC-V CLINT Timer

| Property | Value |
|----------|-------|
| **Type** | Timer (MMIO) |
| **Base address** | 0x02000000 |
| **Registers** | mtime (free-running), mtimecmp (per-hart compare) |
| **IRQ** | Machine timer interrupt (mtimecmp match) |
| **Architectures** | riscv |

**Emulation approach:**
- mtime increments at configured frequency (default 10MHz)
- When mtime >= mtimecmp, raise machine timer interrupt
- Supervisor timer interrupt delegated via mie/mip

**Implementation:** `sys/emulation/riscv/emu_intr_riscv.c`

---

## 4. Interrupt Controller Devices

### 4.1 i8259 PIC (Programmable Interrupt Controller)

| Property | Value |
|----------|-------|
| **Type** | Interrupt controller |
| **Base address** | 0x20-0x21 (master), 0xA0-0xA1 (slave) |
| **IRQs** | 8 per PIC, 15 total (master + slave cascade) |
| **Architectures** | amd64, i386 |

**Registers:**

| Port | Read | Write |
|------|------|-------|
| 0x20 | IRR/ISR (if A0=0) | ICW1/OCW2/OCW3 (if A0=0) |
| 0x21 | IMR (if A0=1) | ICW2/ICW3/ICW4/OCW1 (if A0=1) |
| 0xA0 | IRR/ISR (slave) | ICW1/OCW2/OCW3 (slave) |
| 0xA1 | IMR (slave) | ICW2/ICW3/ICW4/OCW1 (slave) |

**Initialization sequence (ICW1-ICW4):**
1. ICW1 (0x20): Edge/level, cascade/single, ICW4 needed
2. ICW2 (0x21): Base vector address (e.g., 0x08 for IRQ0=0x08)
3. ICW3 (0x21): Slave connection (master: bit mask, slave: slave ID)
4. ICW4 (0x21): Mode (8086 mode, AEOI, buffered, SFNM)

**Emulation approach:**
- Track IRR (Interrupt Request Register), ISR (In-Service Register), IMR (Interrupt Mask Register)
- Priority resolution: lowest IRQ number has highest priority
- Cascade: slave PIC connected to master IRQ2
- EOI (End of Interrupt) via OCW2 (0x20) or automatic

**Implementation:** `usr.sbin/emu/emu_dev_pic.c`

### 4.2 I/O APIC

| Property | Value |
|----------|-------|
| **Type** | Interrupt controller |
| **Base address** | 0xFEC00000 (MMIO) |
| **IRQs** | 24 redirection entries |
| **Architectures** | amd64, i386 |

**Registers (MMIO, 32-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | IOREGSEL | I/O Register Select (index) |
| 0x10 | IOWIN | I/O Window (data) |
| Index 0x00 | IOAPICID | I/O APIC ID |
| Index 0x01 | IOAPICVER | Version (24 entries) |
| Index 0x02 | IOAPICARB | Arbitration ID |
| Index 0x10-0x3F | IOREDTBL[0-23] | Redirection Table Entries (2 registers each, 64-bit) |

**IOREDTBL entry (64-bit):**
- Bits 0-7: Vector
- Bits 8-10: Delivery Mode (000=Fixed, 100=NMI, 101=INIT, 111=ExtINT)
- Bit 11: Destination Mode (0=Physical, 1=Logical)
- Bit 12: Delivery Status (0=Idle, 1=Send Pending)
- Bit 13: Interrupt Polarity (0=Active High, 1=Active Low)
- Bit 14: Remote IRR
- Bit 15: Trigger Mode (0=Edge, 1=Level)
- Bit 16: Mask (1=masked)
- Bits 56-63: Destination (APIC ID)

**Emulation approach:**
- MMIO at 0xFEC00000
- Route interrupts to LAPIC based on destination field
- Support edge and level-triggered modes
- Mask/unmask support

**Implementation:** `usr.sbin/emu/emu_dev_intr.c`

### 4.3 LAPIC (Local APIC)

| Property | Value |
|----------|-------|
| **Type** | Per-CPU interrupt controller |
| **Base address** | 0xFEE00000 (MMIO) |
| **Architectures** | amd64, i386 |

See `003-Emulation-Arch-amd64.md` Section 4.4 for full register map.

**Emulation approach:**
- MMIO at 0xFEE00000
- TPR (Task Priority), PPR (Processor Priority), EOI
- IRR/ISR/TMR bitmaps (256 bits each)
- ICR (Interrupt Command Register) for IPI
- LVT entries for timer, thermal, performance, LINT0, LINT1, error
- Timer: periodic or one-shot, based on initial count and divide configuration

**Implementation:** `sys/emulation/amd64/emu_intr_amd64.c`

### 4.4 GICv3 (ARM Generic Interrupt Controller v3)

| Property | Value |
|----------|-------|
| **Type** | Interrupt controller |
| **Base address** | 0x2F000000 (Distributor), 0x2F100000+ (Redistributor), 0x2F200000 (CPU IF) |
| **IRQs** | SGI 0-15, PPI 16-31, SPI 32-1019, LPI 8192+ |
| **Architectures** | arm64 |

See `005-Emulation-Arch-arm64.md` Section 4.4 for full register map.

**Emulation approach:**
- Distributor: MMIO at 0x2F000000. Interrupt configuration, routing, enables.
- Redistributor: Per-CPU at 0x2F100000 + n*0x20000. SGI/PPI management.
- CPU Interface: MMIO at 0x2F200000. IAR, EOI, PMR, BPR.
- Support SGI (IPI), PPI (per-CPU timers), SPI (device interrupts)
- Interrupt priority and affinity routing

**Implementation:** `sys/emulation/arm64/emu_intr_arm64.c`

### 4.5 GICv2 (ARM Generic Interrupt Controller v2)

| Property | Value |
|----------|-------|
| **Type** | Interrupt controller |
| **Base address** | 0x2F000000 (Distributor), 0x2F001000 (CPU Interface) |
| **IRQs** | SGI 0-15, PPI 16-31, SPI 32-1019 |
| **Architectures** | arm |

Simplified version of GICv3 without redistributors and LPIs. CPU interface is at a fixed address rather than per-CPU.

**Implementation:** `sys/emulation/arm/emu_intr_arm.c`

### 4.6 CLINT (RISC-V Core-Local Interrupt Controller)

| Property | Value |
|----------|-------|
| **Type** | Timer + software interrupt controller |
| **Base address** | 0x02000000 |
| **Architectures** | riscv |

**Registers (MMIO, 32-bit/64-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x0000 + hart*4 | msip | Machine software interrupt pending (write 1 to IPI) |
| 0x4000 + hart*8 | mtimecmp | Machine timer compare register |
| 0xBFF8 | mtime | Machine timer (free-running, 64-bit) |

**Emulation approach:**
- mtime is a free-running 64-bit counter
- When mtime >= mtimecmp, raise machine timer interrupt
- Writing 1 to msip raises machine software interrupt (IPI)
- Interrupts delegated to S-mode via mideleg

**Implementation:** `sys/emulation/riscv/emu_intr_riscv.c`

### 4.7 PLIC (RISC-V Platform-Level Interrupt Controller)

| Property | Value |
|----------|-------|
| **Type** | External interrupt controller |
| **Base address** | 0x0C000000 |
| **IRQs** | 0-31 (configurable, up to 1023) |
| **Priority levels** | 0-7 |
| **Architectures** | riscv |

**Registers (MMIO, 32-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000000 + irq*4 | Priority | Interrupt priority (0=never, 1-7) |
| 0x001000 + word*4 | Pending | Interrupt pending bits |
| 0x002000 + context*0x80 + word*4 | Enable | Per-context enable bits |
| 0x200000 + context*0x1000 | Threshold | Priority threshold |
| 0x200004 + context*0x1000 | Claim/Complete | Claim and complete interrupt |

**Emulation approach:**
- External interrupts (SPI) are routed through PLIC
- Priority-based arbitration: highest priority pending interrupt is delivered
- Threshold filtering: interrupts with priority <= threshold are masked
- Claim: read the highest priority pending interrupt ID
- Complete: write the interrupt ID to signal completion

**Implementation:** `sys/emulation/riscv/emu_intr_riscv.c`

### 4.8 OpenPIC (PowerPC Open Programmable Interrupt Controller)

| Property | Value |
|----------|-------|
| **Type** | Interrupt controller |
| **Base address** | 0x40000 (MMIO) |
| **IRQs** | Up to 196 (configurable) |
| **Architectures** | powerpc |

**Emulation approach:**
- Interrupt source configuration (priority, vector, destination)
- Per-CPU interrupt acknowledge and EOI
- Timer group (4 global timers)
- IPI (inter-processor interrupt) support

**Implementation:** `sys/emulation/powerpc/emu_intr_ppc.c`

---

## 5. Storage Devices

### 5.1 virtio-blk

| Property | Value |
|----------|-------|
| **Type** | Block device |
| **Transport** | MMIO (virtio-mmio) or PCI |
| **MMIO base** | 0x3F000000 (arm64), configurable for others |
| **IRQ** | Configurable (typically 42 for arm64) |
| **Architectures** | All |

**virtio-mmio registers (32-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | MagicValue | 0x74726976 ("virt") |
| 0x004 | Version | 2 (virtio v1) |
| 0x008 | DeviceID | 2 (block device) |
| 0x00C | VendorID | 0x554D ("VM") |
| 0x010 | DeviceFeatures | Features offered by device |
| 0x014 | DeviceFeaturesSel | Feature selection (bits 32-63) |
| 0x020 | DriverFeatures | Features accepted by driver |
| 0x024 | DriverFeaturesSel | Driver feature selection |
| 0x028 | QueueSel | Virtual queue selection |
| 0x02C | QueueNumMax | Maximum queue size |
| 0x030 | QueueNum | Queue size set by driver |
| 0x034 | QueueReady | Queue ready flag |
| 0x038 | QueueNotify | Queue notification |
| 0x044 | QueueDescLow | Descriptor area base (low 32 bits) |
| 0x048 | QueueDescHigh | Descriptor area base (high 32 bits) |
| 0x04C | QueueDriverLow | Driver area base (low 32 bits) |
| 0x050 | QueueDriverHigh | Driver area base (high 32 bits) |
| 0x054 | QueueDeviceLow | Device area base (low 32 bits) |
| 0x058 | QueueDeviceHigh | Device area base (high 32 bits) |
| 0x060 | Status | Device status |
| 0x064 | ConfigGen | Configuration generation |
| 0x100+ | Config | Device-specific config (capacity, size, etc.) |

**virtio-blk configuration space:**
- `capacity` (64-bit): Number of 512-byte sectors
- `size_max` (32-bit): Maximum segment size
- `seg_max` (32-bit): Maximum number of segments

**virtio-blk request header (in descriptor):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | type | 0=IN, 1=OUT, 2=FLUSH, 3=GET_ID, 8=DISCARD, 9=WRITE_ZEROES |
| 4 | 4 | ioprio | I/O priority |
| 8 | 8 | sector | Sector number (byte offset / 512) |

**virtio-blk response (in descriptor):**
- 1 byte: 0=OK, 1=IOERR, 2=UNSUPP

**Emulation approach:**
- Backed by a raw disk image file (or ZFS volume)
- Read/write operations translate to file seeks + reads/writes
- Flush translates to fsync()
- Support discard/write-zeroes for thin provisioning
- Interrupt on request completion

**Implementation:** `usr.sbin/emu/emu_dev_storage.c`

### 5.2 AHCI/SATA

| Property | Value |
|----------|-------|
| **Type** | Storage controller |
| **PCI address** | 0:0:0 function 2 (typical) |
| **BAR5 (ABAR)** | MMIO at PCI BAR5 |
| **Ports** | 1-6 (configurable) |
| **Architectures** | amd64, i386 |

**AHCI registers (ABAR, MMIO):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | CAP | Capabilities (number of ports, etc.) |
| 0x004 | GHC | Global Host Control (AHCI enable, interrupt enable) |
| 0x008 | IS | Interrupt Status |
| 0x00C | PI | Ports Implemented |
| 0x010 | VS | Version (1.3.0 = 0x00010300) |
| 0x100 + port*0x80 | PxCLB | Port Command List Base |
| 0x104 + port*0x80 | PxCLBU | Port Command List Base Upper |
| 0x108 + port*0x80 | PxFB | Port FIS Base |
| 0x10C + port*0x80 | PxFBU | Port FIS Base Upper |
| 0x118 + port*0x80 | PxIE | Port Interrupt Enable |
| 0x120 + port*0x80 | PxCMD | Port Command (start, stop, FIS receive) |
| 0x128 + port*0x80 | PxTFD | Port Task File Data |
| 0x130 + port*0x80 | PxSIG | Port Signature |
| 0x134 + port*0x80 | PxSSTS | Port Serial ATA Status |
| 0x138 + port*0x80 | PxSCTL | Port Serial ATA Control |
| 0x13C + port*0x80 | PxSERR | Port Serial ATA Error |
| 0x140 + port*0x80 | PxACT | Port Command Active |
| 0x148 + port*0x80 | PxCI | Port Command Issue |

**Emulation approach:**
- Present as a PCI device on the emulated PCI bus
- Implement AHCI 1.3.1 specification
- Backed by a raw disk image file
- Support NCQ (Native Command Queuing)
- Simpler alternative: use virtio-blk instead for most use cases

**Implementation:** `usr.sbin/emu/emu_dev_ahci.c`

---

## 6. Network Devices

### 6.1 virtio-net

| Property | Value |
|----------|-------|
| **Type** | Network adapter |
| **Transport** | MMIO (virtio-mmio) or PCI |
| **MMIO base** | 0x3F100000 (arm64), configurable for others |
| **IRQ** | Configurable (typically 43 for arm64) |
| **Architectures** | All |

**virtio-net configuration space:**
- `mac` (6 bytes): MAC address
- `status` (16-bit): Link status

**virtio-net request header (in descriptor):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | flags | GSO flags, checksum info |
| 8 | 2 | gso_size | GSO size |
| 10 | 2 | hdr_len | Header length |
| 12 | 2 | csum_start | Checksum start |
| 14 | 2 | csum_offset | Checksum offset |
| 16 | 4 | num_buffers | Number of buffers |

**Emulation approach:**
- Two virtual queues: receive and transmit
- TX packets are written to a tap interface or socket
- RX packets come from a tap interface or socket
- MAC address filtering
- Support checksum offloading (optional)

**Implementation:** `usr.sbin/emu/emu_dev_net.c`

---

## 7. System Devices

### 7.1 MC146818 RTC (Real-Time Clock)

| Property | Value |
|----------|-------|
| **Type** | Real-time clock + CMOS RAM |
| **Base address** | 0x70-0x71 (x86 I/O ports) |
| **IRQ** | 8 (IRQ8, RTC alarm) |
| **Architectures** | amd64, i386 |

**Registers:**

| Port | Read/Write | Description |
|------|------------|-------------|
| 0x70 | Write | CMOS address/index register (bit 7=NMI disable) |
| 0x71 | Read/Write | CMOS data register |

**CMOS memory map (relevant registers):**

| Index | Size | Field | Description |
|-------|------|-------|-------------|
| 0x00 | 1 byte | Seconds | BCD seconds |
| 0x02 | 1 byte | Minutes | BCD minutes |
| 0x04 | 1 byte | Hours | BCD hours (bit 7=PM for 12-hour) |
| 0x06 | 1 byte | Day of week | 1=Sunday |
| 0x07 | 1 byte | Day of month | BCD day |
| 0x08 | 1 byte | Month | BCD month |
| 0x09 | 1 byte | Year | BCD year (0-99) |
| 0x0A | 1 byte | Status A | Update in progress, divider, rate |
| 0x0B | 1 byte | Status B | 24hr, DST, binary/BCD, enable |
| 0x0C | 1 byte | Status C | IRQ flags (UF, AF, PF) |
| 0x0D | 1 byte | Status D | Valid RAM and time |
| 0x0E-0x7F | | | CMOS configuration RAM |

**Emulation approach:**
- Track current time and date
- Support periodic interrupt (at configurable rate)
- Support alarm interrupt
- NMI disable bit in port 0x70 bit 7
- Century register at CMOS index 0x32 (FreeBSD convention)

**Implementation:** `usr.sbin/emu/emu_dev_rtc.c`

### 7.2 PL031 RTC (ARM PrimeCell)

| Property | Value |
|----------|-------|
| **Type** | Real-time clock |
| **Base address** | 0x1C170000 (MMIO) |
| **IRQ** | 35 (SPI) |
| **Architectures** | arm64, arm |

**Registers (MMIO, 32-bit):**

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | RTCDR | Data Register (current time in seconds since epoch) |
| 0x004 | RTCMR | Match Register (alarm time) |
| 0x008 | RTCLR | Load Register (set time) |
| 0x00C | RTCCR | Control Register (enable) |
| 0x010 | RTCIMSC | Interrupt Mask Set/Clear |
| 0x014 | RTCRIS | Raw Interrupt Status |
| 0x018 | RTCMIS | Masked Interrupt Status |
| 0x01C | RTCICR | Interrupt Clear Register |

**Emulation approach:** Simpler than MC146818. Just track epoch time and compare with match register for alarm.

**Implementation:** `usr.sbin/emu/emu_dev_rtc.c`

### 7.3 ACPI (Advanced Configuration and Power Interface)

| Property | Value |
|----------|-------|
| **Type** | System description tables |
| **Base address** | FADT at 0x7FE00000 (typical), RSDP in EBDA |
| **Architectures** | amd64, i386 |

**Required tables:**

| Table | Signature | Description |
|-------|-----------|-------------|
| **RSDP** | "RSD PTR " | Root System Description Pointer (found in EBDA or BIOS area) |
| **RSDT** | "RSDT" | Root System Description Table (32-bit pointers) |
| **XSDT** | "XSDT" | Extended System Description Table (64-bit pointers) |
| **FADT** | "FACP" | Fixed ACPI Description Table (PM1a, PM1b, SMI, etc.) |
| **DSDT** | "DSDT" | Differentiated System Description Table (AML bytecode) |
| **MADT** | "APIC" | Multiple APIC Description Table (LAPIC, I/O APIC, interrupt overrides) |
| **HPET** | "HPET" | HPET Description Table |
| **SRAT** | "SRAT" | System Resource Affinity Table (NUMA) |
| **MCFG** | "MCFG" | PCI Express Memory-Mapped Configuration Space |

**MADT entries:**
- LAPIC entry: APIC ID, processor ID, flags (enabled)
- I/O APIC entry: I/O APIC ID, address (0xFEC00000), global interrupt base
- Interrupt source override: bus, source, global interrupt, flags (polarity, trigger)
- LAPIC NMI: processor ID, LINT pin, flags

**Emulation approach:**
- Generate ACPI tables dynamically based on instance configuration
- DSDT can be minimal (just declare the platform topology)
- RSDP placed at 0x000F0000 (EBDA area) for BIOS boot
- Use iasl (ACPI compiler) to compile DSDT AML

**Implementation:** `usr.sbin/emu/emu_dev_acpi.c`

---

## 8. Firmware Devices

### 8.1 SeaBIOS (Legacy BIOS)

| Property | Value |
|----------|-------|
| **Type** | x86 BIOS firmware |
| **Load address** | 0x000F0000-0x000FFFFF (128KB BIOS region) |
| **Reset vector** | 0xFFFFFFF0 (alias of 0x000FFFF0) |
| **Architectures** | amd64, i386 |

**Emulation approach:**
- Load SeaBIOS binary from blob cache via `emu_blob_resolve("seabios", "seabios.bin", ...)`
- Load at 0x000F0000 (with alias at 0xFFFF0000)
- SeaBIOS handles: INT 0x13 (disk), INT 0x10 (video), INT 0x15 (E820 memory map), INT 0x16 (keyboard), INT 0x1A (RTC)
- Provide SMBIOS tables for system identification
- Provide PIRQ routing table for PCI interrupts

**Integration:**
- SeaBIOS is BSD-licensed (LGPLv3 combined binary)
- **Not** included in the FreeBSD source tree or release
- Downloaded at runtime via `emu blob fetch seabios`
- See `010-Emulation-Blob-Management.md` for full blob management details

**Implementation:** `usr.sbin/emu/emu_firmware.c`

### 8.2 OVMF (UEFI Firmware)

| Property | Value |
|----------|-------|
| **Type** | UEFI firmware |
| **Load address** | 0x000F0000-0x00FFFFFF (16MB flash) |
| **Reset vector** | 0xFFFFFFF0 |
| **Architectures** | amd64, i386, arm64 |

**Emulation approach:**
- Load OVMF binary from blob cache via `emu_blob_resolve("ovmf-x64", "OVMF_CODE.fd", ...)`
- Load at the flash base address
- OVMF handles: UEFI boot services, GPT partition table, EFI system partition
- Provide UEFI runtime services (get time, set time, reset system)
- Provide ACPI tables via UEFI configuration tables

**Integration:**
- OVMF is BSD-licensed
- **Not** included in the FreeBSD source tree or release
- Downloaded at runtime via `emu blob fetch ovmf-x64` (or `ovmf-ia32`, `ovmf-aarch64`)
- Support both OVMF_CODE.fd (code) and OVMF_VARS.fd (variables)
- See `010-Emulation-Blob-Management.md` for full blob management details

**Implementation:** `usr.sbin/emu/emu_firmware.c`

### 8.3 U-Boot

| Property | Value |
|----------|-------|
| **Type** | Boot loader |
| **Load address** | Architecture-specific (typically at DRAM base + offset) |
| **Architectures** | arm64, arm, powerpc, riscv |

**Emulation approach:**
- Load U-Boot binary from blob cache via `emu_blob_resolve("uboot-<arch>", "u-boot.bin", ...)`
- Load at the architecture-specific entry point
- U-Boot handles: device tree loading, kernel loading (booti/bootm), network boot (TFTP)
- Provide U-Boot environment variables for boot configuration

**Integration:**
- U-Boot is GPLv2 licensed
- **Not** included in the FreeBSD source tree or release
- Downloaded at runtime via `emu blob fetch uboot-<arch>`
- Build for each target architecture and board
- Use a generic "virt" board configuration
- See `010-Emulation-Blob-Management.md` for full blob management details

**Implementation:** `usr.sbin/emu/emu_firmware.c`

### 8.4 OpenSBI (RISC-V)

| Property | Value |
|----------|-------|
| **Type** | M-mode firmware |
| **Load address** | 0x80000000 (typical DRAM base) |
| **Entry point** | 0x80000000 (reset vector) |
| **Architectures** | riscv |

**Emulation approach:**
- Load OpenSBI binary from blob cache via `emu_blob_resolve("opensbi", "fw_jump.bin", ...)`
- Load at the reset vector address (0x80000000)
- OpenSBI provides: SBI (Supervisor Binary Interface) services
- SBI services: timer, IPI, console, system reset

**Integration:**
- OpenSBI is BSD-licensed
- **Not** included in the FreeBSD source tree or release
- Downloaded at runtime via `emu blob fetch opensbi`
- Build for RISC-V 64-bit
- Configure for the emulated platform (number of harts, timer frequency)
- See `010-Emulation-Blob-Management.md` for full blob management details

**Implementation:** `usr.sbin/emu/emu_firmware.c`

---

## 9. Device Emulation Implementation Tasks

| # | Task | Status | Assigned To | Dependencies | Files | Notes |
|---|------|--------|-------------|--------------|-------|-------|
| DEV.1 | Implement NS16550 UART emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_uart.c` | Shared across amd64, i386, powerpc, riscv. Ring buffer for console. Interrupt via I/O APIC/PLIC/OpenPIC. |
| DEV.2 | Implement PL011 UART emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_uart.c` | ARM PrimeCell UART. 32-bit register access. Shared across arm64, arm. |
| DEV.3 | Implement i8254 PIT emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_timer.c` | 3 channels. Counter 0 → IRQ0. All 6 operating modes. Latch command. |
| DEV.4 | Implement HPET emulation | NOT STARTED | | DEV.3 | `usr.sbin/emu/emu_dev_timer.c` | MMIO at 0xFED00000. 3+ comparators. 10MHz default. Legacy routing. |
| DEV.5 | Implement ARM Generic Timer emulation | NOT STARTED | | | `sys/emulation/arm64/emu_intr_arm64.c` | System register access. CNTPCT, CNTP_TVAL, CNTP_CVAL. PPI 26/27. |
| DEV.6 | Implement SP804 Timer emulation | NOT STARTED | | DEV.3 | `usr.sbin/emu/emu_dev_timer.c` | ARM dual-timer. MMIO at 0x1C110000. Periodic/one-shot. |
| DEV.7 | Implement PowerPC Decrementer emulation | NOT STARTED | | | `sys/emulation/powerpc/emu_intr_ppc.c` | SPR-based. Auto-decrement at time base frequency. Vector 0x0900. |
| DEV.8 | Implement i8259 PIC emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_pic.c` | Master + slave cascade. IRR/ISR/IMR. ICW1-ICW4 init. EOI. |
| DEV.9 | Implement I/O APIC emulation | NOT STARTED | | DEV.8 | `usr.sbin/emu/emu_dev_intr.c` | MMIO at 0xFEC00000. 24 IOREDTBL entries. Edge/level. Physical/logical destination. |
| DEV.10 | Implement LAPIC emulation | NOT STARTED | | DEV.9 | `sys/emulation/amd64/emu_intr_amd64.c` | MMIO at 0xFEE00000. TPR/PPR/EOI. IRR/ISR/TMR. ICR for IPI. LVT timer. |
| DEV.11 | Implement GICv3 emulation | NOT STARTED | | | `sys/emulation/arm64/emu_intr_arm64.c` | Distributor + Redistributor + CPU IF. SGI/PPI/SPI. Priority routing. |
| DEV.12 | Implement GICv2 emulation | NOT STARTED | | DEV.11 | `sys/emulation/arm/emu_intr_arm.c` | Simplified GIC. Fixed CPU interface. No LPIs. |
| DEV.13 | Implement CLINT emulation | NOT STARTED | | | `sys/emulation/riscv/emu_intr_riscv.c` | MMIO at 0x02000000. msip, mtimecmp, mtime. Timer + software IPI. |
| DEV.14 | Implement PLIC emulation | NOT STARTED | | DEV.13 | `sys/emulation/riscv/emu_intr_riscv.c` | MMIO at 0x0C000000. Priority/pending/enable/threshold/claim. |
| DEV.15 | Implement OpenPIC emulation | NOT STARTED | | | `sys/emulation/powerpc/emu_intr_ppc.c` | MMIO at 0x40000. Source config. Per-CPU ack/EOI. Timers. IPI. |
| DEV.16 | Implement virtio-blk emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_storage.c` | virtio-mmio transport. Raw disk image backing. Read/write/flush/discard. |
| DEV.17 | Implement AHCI/SATA emulation | NOT STARTED | | DEV.16 | `usr.sbin/emu/emu_dev_ahci.c` | PCI device. AHCI 1.3.1. NCQ. Raw disk backing. |
| DEV.18 | Implement virtio-net emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_net.c` | virtio-mmio transport. Tap/socket backing. MAC filtering. |
| DEV.19 | Implement MC146818 RTC emulation | NOT STARTED | | | `usr.sbin/emu/emu_dev_rtc.c` | I/O ports 0x70-0x71. CMOS RAM. Periodic/alarm interrupts. |
| DEV.20 | Implement PL031 RTC emulation | NOT STARTED | | DEV.19 | `usr.sbin/emu/emu_dev_rtc.c` | ARM PrimeCell RTC. MMIO at 0x1C170000. Match register alarm. |
| DEV.21 | Implement ACPI table generation | NOT STARTED | | | `usr.sbin/emu/emu_dev_acpi.c` | RSDP, RSDT/XSDT, FADT, DSDT, MADT, HPET. Dynamic generation. |
| DEV.22 | Implement SeaBIOS firmware loading | NOT STARTED | | | `usr.sbin/emu/emu_firmware.c` | Load SeaBIOS binary from blob cache via `emu_blob_resolve()`. SMBIOS tables. PIRQ routing. See `010-Emulation-Blob-Management.md`. |
| DEV.23 | Implement OVMF firmware loading | NOT STARTED | | DEV.22 | `usr.sbin/emu/emu_firmware.c` | Load OVMF binary from blob cache via `emu_blob_resolve()`. UEFI runtime services. See `010-Emulation-Blob-Management.md`. |
| DEV.24 | Implement U-Boot firmware loading | NOT STARTED | | | `usr.sbin/emu/emu_firmware.c` | Load U-Boot binary from blob cache via `emu_blob_resolve()`. Architecture-specific entry. See `010-Emulation-Blob-Management.md`. |
| DEV.25 | Implement OpenSBI firmware loading | NOT STARTED | | DEV.24 | `usr.sbin/emu/emu_firmware.c` | Load OpenSBI binary from blob cache via `emu_blob_resolve()`. SBI services. RISC-V M-mode. See `010-Emulation-Blob-Management.md`. |
| DEV.26 | Write device emulation unit tests | NOT STARTED | | DEV.1-DEV.25 | `tests/usr.sbin/emu/device/` | Test each device: register read/write, interrupt generation, data transfer. |
| DEV.27 | Write device integration tests | NOT STARTED | | DEV.26 | `tests/usr.sbin/emu/device_integration.sh` | Boot FreeBSD in emulator. Verify console output. Verify disk access. Verify network. |

---

## 10. Cross-References

### 10.1 Related Plan Documents

| Document | Relationship |
|----------|-------------|
| `001-Emulation-Overview.md` | Main implementation plan. Phase 5 (Custom Emulator Engine), device model framework. |
| `002-Emulation-Security-FS.md` | Security architecture for device emulation (MMIO validation, no DMA to host). |
| `003-Emulation-Arch-amd64.md` | x86 device requirements (LAPIC, I/O APIC, PIC, PIT, HPET, RTC, ACPI). |
| `004-Emulation-Arch-i386.md` | Same x86 devices as amd64. |
| `005-Emulation-Arch-arm64.md` | ARM device requirements (GICv3, Generic Timer, PL011, PL031). |
| `006-Emulation-Arch-arm.md` | ARM device requirements (GICv2, SP804, PL011, PL031). |
| `007-Emulation-Arch-powerpc.md` | PowerPC device requirements (OpenPIC, Decrementer, NS16550). |
| `008-Emulation-Arch-riscv.md` | RISC-V device requirements (CLINT, PLIC, NS16550). |
| `010-Emulation-Blob-Management.md` | Blob management and CPU model database. Firmware blobs (SeaBIOS, OVMF, U-Boot, OpenSBI, DTB). |

### 10.2 Reference Materials

| Resource | URL / Path | Use |
|----------|------------|-----|
| NS16550 datasheet | https://www.ti.com/lit/ds/symlink/pc16550d.pdf | UART register map |
| ARM PrimeCell UART (PL011) TRM | https://developer.arm.com/documentation/ddi0183/ | PL011 register map |
| Intel 8254 PIT datasheet | https://www.intel.com/ | PIT register map |
| Intel HPET specification | https://www.intel.com/ | HPET register map |
| Intel I/O APIC specification | https://www.intel.com/ | I/O APIC register map |
| Intel MP specification | https://www.intel.com/ | LAPIC, IPI |
| ARM GICv3 specification (IHI0069) | https://developer.arm.com/documentation/ihi0069/ | GICv3 register map |
| RISC-V CLINT specification | https://github.com/riscv/riscv-isa-manual | CLINT register map |
| RISC-V PLIC specification | https://github.com/riscv/riscv-isa-manual | PLIC register map |
| virtio specification v1.2 | https://docs.oasis-open.org/virtio/virtio/v1.2/ | virtio-mmio, virtio-blk, virtio-net |
| AHCI 1.3.1 specification | https://www.intel.com/ | AHCI register map |
| MC146818 RTC datasheet | https://www.freescale.com/ | RTC register map |
| ACPI 6.5 specification | https://uefi.org/specifications | ACPI table generation |
| SeaBIOS | https://www.seabios.org/ | Legacy BIOS firmware |
| OVMF (TianoCore) | https://github.com/tianocore | UEFI firmware |
| U-Boot | https://github.com/u-boot/u-boot | Boot loader |
| OpenSBI | https://github.com/riscv-software-src/opensbi | RISC-V M-mode firmware |

---

## 11. Notes

- **Start simple**: Begin with NS16550 UART, virtio-blk, and the arch-specific interrupt controller. These are sufficient to boot FreeBSD.
- **Add complexity later**: HPET, ACPI, AHCI, and network can be added after basic boot works.
- **Firmware as blobs**: SeaBIOS, OVMF, U-Boot, and OpenSBI are pre-built binaries. **Never** included in the FreeBSD source tree or release. Downloaded at runtime via `emu blob fetch <blob-id>`. See `010-Emulation-Blob-Management.md` for the complete blob management system.
- **Device tree generation**: For ARM and RISC-V, the device tree blob (DTB) must describe all emulated devices and their MMIO addresses.
- **ACPI generation**: For x86, ACPI tables must be generated dynamically based on instance configuration (number of CPUs, memory size, etc.).
- **Interrupt routing**: Each device's interrupt must be correctly routed to the arch-specific interrupt controller. Document the IRQ assignments clearly.
