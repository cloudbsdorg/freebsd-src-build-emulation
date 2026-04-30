# Device Model Documentation

This document describes the device emulation framework and how to implement new emulated devices.

## Overview

The FreeBSD emulation framework provides a framework for emulating hardware devices. Each emulated device implements a set of callbacks that are invoked by the architecture-specific code.

## Device Structure

```c
struct emu_device {
    const char *name;              /* Device name */
    uint64_t base_address;         /* MMIO base address */
    uint64_t size;                 /* MMIO region size */
    
    /* Read/write callbacks */
    int (*read)(struct emu_device *dev, uint64_t offset,
        size_t size, uint64_t *value);
    int (*write)(struct emu_device *dev, uint64_t offset,
        size_t size, uint64_t value);
    
    /* Interrupt callbacks */
    int (*assert_irq)(struct emu_device *dev, int irq);
    int (*deassert_irq)(struct emu_device *dev, int irq);
    
    /* Lifecycle callbacks */
    int (*init)(struct emu_device *dev);
    void (*fini)(struct emu_device *dev);
    
    /* Private data */
    void *private;
};
```

## Registering Devices

```c
int emu_device_register(struct emu_instance *inst, struct emu_device *dev);
void emu_device_unregister(struct emu_instance *inst, struct emu_device *dev);
```

## MMIO Access

### Reading from Device

```c
static int
mydev_read(struct emu_device *dev, uint64_t offset, size_t size,
    uint64_t *value)
{
    struct mydev_state *state = dev->private;
    
    switch (offset) {
    case MYDEV_REG_STATUS:
        *value = state->status;
        break;
    case MYDEV_REG_DATA:
        *value = state->data;
        break;
    default:
        return (EINVAL);
    }
    
    return (0);
}
```

### Writing to Device

```c
static int
mydev_write(struct emu_device *dev, uint64_t offset, size_t size,
    uint64_t value)
{
    struct mydev_state *state = dev->private;
    
    switch (offset) {
    case MYDEV_REG_CONTROL:
        state->control = value;
        if (value & MYDEV_CTRL_START)
            mydev_start(state);
        break;
    case MYDEV_REG_DATA:
        state->data = value;
        break;
    default:
        return (EINVAL);
    }
    
    return (0);
}
```

## Interrupt Handling

### Asserting Interrupts

```c
static int
mydev_assert_irq(struct emu_device *dev, int irq)
{
    struct mydev_state *state = dev->private;
    
    state->irq_pending |= (1 << irq);
    
    /* Notify CPU of interrupt */
    return (emu_instance_raise_irq(dev->instance, irq));
}
```

### Handling Interrupt Acknowledgment

```c
static int
mydev_check_irq(struct emu_device *dev)
{
    struct mydev_state *state = dev->private;
    
    if (state->irq_pending && state->irq_enabled) {
        return (1);  /* Interrupt is pending and enabled */
    }
    return (0);
}
```

## UART Device

The framework includes a reference UART implementation:

### Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | DATA | Data register (read: RX, write: TX) |
| 0x04 | STATUS | Status register |
| 0x08 | CONTROL | Control register |
| 0x0C | BAUD | Baud rate divisor |

### Status Register Bits

- `RX_READY (0x01)` - Data available to read
- `TX_READY (0x02)` - Ready to accept data
- `RX_ERROR (0x04)` - Receive error
- `TX_IDLE (0x08)` - Transmitter idle

### Control Register Bits

- `RX_IRQ_EN (0x01)` - Enable RX interrupt
- `TX_IRQ_EN (0x02)` - Enable TX interrupt
- `LOOPBACK (0x10)` - Loopback mode

### Example Usage

```c
struct emu_device *uart;

uart = emu_uart_create(instance, 0x1000);
if (uart == NULL) {
    return (ENOMEM);
}

emu_device_register(instance, uart);

/* Later, when guest writes to UART */
static int
emu_uart_write(struct emu_device *dev, uint64_t offset, size_t size,
    uint64_t value)
{
    /* Handle UART write */
}
```

## Timer Device

The framework provides a virtual timer device:

### Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | COUNT | Current count |
| 0x04 | COMPARE | Compare value |
| 0x08 | CONTROL | Control register |
| 0x0C | STATUS | Status register |

### Control Register Bits

- `ENABLE (0x01)` - Timer enable
- `PERIODIC (0x02)` - Periodic mode
- `INT_ENABLE (0x04)` - Interrupt enable

### Example Usage

```c
struct emu_device *timer;

timer = emu_timer_create(instance, 0x2000, 1000000);  /* 1 MHz */
if (timer == NULL) {
    return (ENOMEM);
}

emu_device_register(instance, timer);
```

## Storage Device

Emulated storage devices use a block interface:

```c
struct emu_block_dev {
    struct emu_device base;
    
    /* Block device operations */
    int (*read_blocks)(struct emu_block_dev *dev, uint64_t lba,
        size_t count, void *buf);
    int (*write_blocks)(struct emu_block_dev *dev, uint64_t lba,
        size_t count, const void *buf);
    
    /* Geometry */
    uint64_t sector_size;
    uint64_t total_sectors;
};
```

### Creating a Block Device

```c
static int
my_storage_read_blocks(struct emu_block_dev *dev, uint64_t lba,
    size_t count, void *buf)
{
    off_t offset = lba * dev->sector_size;
    size_t size = count * dev->sector_size;
    
    return (pread(fd, buf, size, offset));
}

struct emu_block_dev *storage;

storage = emu_block_create(instance, "sata0", 512, total_sectors);
storage->read_blocks = my_storage_read_blocks;
```

## Network Device

Emulated network devices provide packet I/O:

```c
struct emu_net_dev {
    struct emu_device base;
    
    /* Packet I/O */
    int (*send)(struct emu_net_dev *dev, const void *packet,
        size_t len);
    int (*recv)(struct emu_net_dev *dev, void *packet,
        size_t *len);
    
    /* MAC address */
    uint8_t mac[6];
};
```

## Virtio Devices

The framework supports Virtio for high-performance I/O:

### Virtio Configuration

```c
struct emu_virtio_config {
    uint16_t device_id;
    uint16_t vendor_id;
    uint32_t device_features;
    uint32_t host_features;
    
    /* Device-specific config */
    void *config;
    size_t config_size;
};
```

### Implementing a Virtio Device

1. Implement Virtio header structure
2. Handle common Virtio registers
3. Implement device-specific features
4. Set up virtqueues

## Debugging Tips

1. **Log MMIO accesses**: Enable debug output for device reads/writes
2. **Break on invalid access**: Check for out-of-bounds offsets
3. **Verify state transitions**: Ensure device state is consistent
4. **Test interrupt delivery**: Verify interrupts reach the guest

## Common Issues

### Device Not Responding

- Check base address configuration
- Verify MMIO region is mapped correctly
- Ensure device is registered with instance

### Interrupts Not Working

- Verify interrupt number is valid
- Check interrupt enable bits
- Ensure interrupt handler is registered

### Performance Issues

- Use DMA for bulk transfers
- Implement buffering for slow devices
- Consider using Virtio for storage/network
