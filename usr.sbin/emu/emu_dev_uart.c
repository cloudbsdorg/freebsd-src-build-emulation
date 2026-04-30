/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 * Copyright (c) 2026 JetBrains s.r.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "emu_dev_uart.h"

/*
 * NS16550-compatible UART Device Emulation
 * 
 * Security: Output goes only to instance console buffer via callback.
 * No host file access, no path traversal, no symlink escapes.
 */

/*
 * Initialize UART device
 */
int
emu_uart_init(struct emu_uart *uart)
{
    if (uart == NULL)
        return (EINVAL);
    
    memset(uart, 0, sizeof(*uart));
    uart->u_fifo_enabled = true;
    uart->u_baud = 115200;  /* Default baud rate */
    uart->u_dlla = 1;       /* Default divisor */
    
    /* Set initial line status */
    uart->u_regs[EMU_UART_LSR] = EMU_UART_LSR_THRE | EMU_UART_LSR_TEMT;
    
    return (0);
}

/*
 * Destroy UART device
 */
void
emu_uart_destroy(struct emu_uart *uart)
{
    if (uart == NULL)
        return;
    
    /* Clear console callback */
    uart->u_console_write = NULL;
    uart->u_console_arg = NULL;
}

/*
 * Reset UART to initial state
 */
void
emu_uart_reset(struct emu_uart *uart)
{
    if (uart == NULL)
        return;
    
    memset(uart->u_regs, 0, EMU_UART_REG_SIZE);
    uart->u_rx_head = uart->u_rx_tail = uart->u_rx_count = 0;
    uart->u_tx_head = uart->u_tx_tail = uart->u_tx_count = 0;
    uart->u_dlab = false;
    uart->u_fifo_enabled = true;
    uart->u_regs[EMU_UART_LSR] = EMU_UART_LSR_THRE | EMU_UART_LSR_TEMT;
}

/*
 * Set console output callback
 */
void
emu_uart_set_console(struct emu_uart *uart,
    void (*write)(void *, const char *, int), void *arg)
{
    if (uart == NULL)
        return;
    
    uart->u_console_write = write;
    uart->u_console_arg = arg;
}

/*
 * Receive data into UART FIFO
 */
int
emu_uart_receive(struct emu_uart *uart, const char *buf, int len)
{
    size_t i;
    size_t count;
    
    if (uart == NULL || buf == NULL || len <= 0)
        return (EINVAL);
    
    count = 0;
    for (i = 0; i < (size_t)len && (size_t)uart->u_rx_count < sizeof(uart->u_rx_fifo); i++) {
        uart->u_rx_fifo[uart->u_rx_head] = buf[i];
        uart->u_rx_head = (uart->u_rx_head + 1) % sizeof(uart->u_rx_fifo);
        uart->u_rx_count++;
        count++;
    }
    
    /* Update line status */
    if (uart->u_rx_count > 0)
        uart->u_regs[EMU_UART_LSR] |= EMU_UART_LSR_DR;
    
    uart->u_rx_bytes += count;
    
    return (count);
}

/*
 * Transmit data from UART FIFO (returns bytes available)
 */
int
emu_uart_transmit(struct emu_uart *uart, char *buf, int len)
{
    int count;
    
    if (uart == NULL || buf == NULL || len <= 0)
        return (0);
    
    count = 0;
    while (uart->u_tx_count > 0 && count < len) {
        buf[count++] = uart->u_tx_fifo[uart->u_tx_tail];
        uart->u_tx_tail = (uart->u_tx_tail + 1) % sizeof(uart->u_tx_fifo);
        uart->u_tx_count--;
    }
    
    /* Update line status */
    if (uart->u_tx_count == 0) {
        uart->u_regs[EMU_UART_LSR] |= EMU_UART_LSR_THRE;
        uart->u_regs[EMU_UART_LSR] |= EMU_UART_LSR_TEMT;
    }
    
    return (count);
}

/*
 * Check if data is ready in RX FIFO
 */
bool
emu_uart_data_ready(struct emu_uart *uart)
{
    if (uart == NULL)
        return (false);
    
    return (uart->u_rx_count > 0);
}

/*
 * Check if THR is empty
 */
bool
emu_uart_thr_empty(struct emu_uart *uart)
{
    if (uart == NULL)
        return (true);
    
    return ((uart->u_regs[EMU_UART_LSR] & EMU_UART_LSR_THRE) != 0);
}

/*
 * Check if transmitter is completely empty
 */
bool
emu_uart_temt_empty(struct emu_uart *uart)
{
    if (uart == NULL)
        return (true);
    
    return ((uart->u_regs[EMU_UART_LSR] & EMU_UART_LSR_TEMT) != 0);
}

/*
 * Read UART register
 */
int
emu_uart_read(struct emu_uart *uart, uint64_t offset, int size, uint64_t *value)
{
    uint8_t reg;
    
    if (uart == NULL || value == NULL)
        return (EINVAL);
    
    /* Validate offset and size */
    if (offset >= EMU_UART_REG_SIZE || size != 1)
        return (EINVAL);
    
    reg = (uint8_t)offset;
    
    /* Handle DLAB-dependent registers */
    if (uart->u_dlab) {
        switch (reg) {
        case EMU_UART_DLL:
            *value = uart->u_dlla & 0xFF;
            return (0);
        case EMU_UART_DLM:
            *value = (uart->u_dlla >> 8) & 0xFF;
            return (0);
        }
    }
    
    switch (reg) {
    case EMU_UART_RX:
        /* Read from RX FIFO */
        if (uart->u_rx_count > 0) {
            *value = uart->u_rx_fifo[uart->u_rx_tail];
            uart->u_rx_tail = (uart->u_rx_tail + 1) % sizeof(uart->u_rx_fifo);
            uart->u_rx_count--;
            
            if (uart->u_rx_count == 0)
                uart->u_regs[EMU_UART_LSR] &= ~EMU_UART_LSR_DR;
        } else {
            *value = 0;
        }
        break;
        
    case EMU_UART_IER:
        *value = uart->u_regs[EMU_UART_IER] & 0x0F;
        break;
        
    case EMU_UART_IIR:
        /* Interrupt identification */
        *value = uart->u_regs[EMU_UART_IIR] | 0xC0;  /* Upper bits always 1 */
        break;
        
    case EMU_UART_LCR:
        *value = uart->u_regs[EMU_UART_LCR];
        break;
        
    case EMU_UART_MCR:
        *value = uart->u_regs[EMU_UART_MCR] & 0x1F;
        break;
        
    case EMU_UART_LSR:
        *value = uart->u_regs[EMU_UART_LSR];
        break;
        
    case EMU_UART_MSR:
        *value = uart->u_regs[EMU_UART_MSR];
        break;
        
    case EMU_UART_SCR:
        *value = uart->u_regs[EMU_UART_SCR];
        break;
        
    default:
        *value = 0;
        break;
    }
    
    return (0);
}

/*
 * Write UART register
 */
int
emu_uart_write(struct emu_uart *uart, uint64_t offset, int size, uint64_t value)
{
    uint8_t reg;
    uint8_t data;
    
    if (uart == NULL)
        return (EINVAL);
    
    /* Validate offset and size */
    if (offset >= EMU_UART_REG_SIZE || size != 1)
        return (EINVAL);
    
    reg = (uint8_t)offset;
    data = (uint8_t)(value & 0xFF);
    
    /* Handle DLAB-dependent registers */
    if (uart->u_dlab) {
        switch (reg) {
        case EMU_UART_DLL:
            uart->u_dlla = (uart->u_dlla & 0xFF00) | data;
            /* Recalculate baud rate (simplified) */
            if (uart->u_dlla > 0)
                uart->u_baud = 115200 / uart->u_dlla;
            return (0);
        case EMU_UART_DLM:
            uart->u_dlla = (uart->u_dlla & 0x00FF) | (data << 8);
            if (uart->u_dlla > 0)
                uart->u_baud = 115200 / uart->u_dlla;
            return (0);
        }
    }
    
    switch (reg) {
    case EMU_UART_TX:
        /* Write to TX FIFO */
        if ((size_t)uart->u_tx_count < sizeof(uart->u_tx_fifo)) {
            uart->u_tx_fifo[uart->u_tx_head] = data;
            uart->u_tx_head = (uart->u_tx_head + 1) % sizeof(uart->u_tx_fifo);
            uart->u_tx_count++;
            uart->u_tx_bytes++;
            
            /* Clear THRE and TEMT */
            uart->u_regs[EMU_UART_LSR] &= ~EMU_UART_LSR_THRE;
            uart->u_regs[EMU_UART_LSR] &= ~EMU_UART_LSR_TEMT;
            
            /* Output to console immediately (character-by-character) */
            if (uart->u_console_write != NULL) {
                char c = (char)data;
                uart->u_console_write(uart->u_console_arg, &c, 1);
            }
        }
        break;
        
    case EMU_UART_IER:
        uart->u_regs[EMU_UART_IER] = data & 0x0F;
        break;
        
    case EMU_UART_FCR:
        if (data & EMU_UART_FCR_RXCLR) {
            uart->u_rx_head = uart->u_rx_tail = uart->u_rx_count = 0;
            uart->u_regs[EMU_UART_LSR] &= ~EMU_UART_LSR_DR;
        }
        if (data & EMU_UART_FCR_TXCLR) {
            uart->u_tx_head = uart->u_tx_tail = uart->u_tx_count = 0;
            uart->u_regs[EMU_UART_LSR] |= EMU_UART_LSR_THRE | EMU_UART_LSR_TEMT;
        }
        uart->u_fifo_enabled = (data & EMU_UART_FCR_FIFOE) != 0;
        break;
        
    case EMU_UART_LCR:
        uart->u_regs[EMU_UART_LCR] = data;
        uart->u_dlab = (data & EMU_UART_LCR_DLAB) != 0;
        break;
        
    case EMU_UART_MCR:
        uart->u_regs[EMU_UART_MCR] = data & 0x1F;
        break;
        
    case EMU_UART_SCR:
        uart->u_regs[EMU_UART_SCR] = data;
        break;
        
    default:
        /* Ignore writes to read-only registers */
        break;
    }
    
    return (0);
}
