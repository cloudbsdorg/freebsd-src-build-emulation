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

#ifndef _EMU_DEV_UART_H_
#define _EMU_DEV_UART_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * NS16550-compatible UART Device Emulation
 * 
 * Emulates a standard 16550 UART for serial console output.
 * Security: Output goes only to instance console buffer, never to host files.
 * No host file access, no symlink escapes, no path traversal.
 */

/* UART register offsets (standard 16550 layout) */
#define EMU_UART_RX         0   /* Receiver buffer (read) */
#define EMU_UART_TX         0   /* Transmitter holding (write) */
#define EMU_UART_IER        1   /* Interrupt Enable */
#define EMU_UART_IIR        2   /* Interrupt Identification (read) */
#define EMU_UART_FCR        2   /* FIFO Control (write) */
#define EMU_UART_LCR        3   /* Line Control */
#define EMU_UART_MCR        4   /* Modem Control */
#define EMU_UART_LSR        5   /* Line Status */
#define EMU_UART_MSR        6   /* Modem Status */
#define EMU_UART_SCR        7   /* Scratch */
#define EMU_UART_DLL        0   /* Divisor Latch LSB (DLAB=1) */
#define EMU_UART_DLM        1   /* Divisor Latch MSB (DLAB=1) */

/* UART register size */
#define EMU_UART_REG_SIZE   8

/* Line Status Register bits */
#define EMU_UART_LSR_DR     0x01    /* Data Ready */
#define EMU_UART_LSR_OE     0x02    /* Overrun Error */
#define EMU_UART_LSR_PE     0x04    /* Parity Error */
#define EMU_UART_LSR_FE     0x08    /* Framing Error */
#define EMU_UART_LSR_BI     0x10    /* Break Interrupt */
#define EMU_UART_LSR_THRE   0x20    /* THR Empty */
#define EMU_UART_LSR_TEMT   0x40    /* TEMT Empty */
#define EMU_UART_LSR_FIFOE  0x80    /* FIFO Error */

/* Line Control Register bits */
#define EMU_UART_LCR_DLAB   0x80    /* Divisor Latch Access Bit */

/* FIFO Control Register bits */
#define EMU_UART_FCR_FIFOE  0x01    /* FIFO Enable */
#define EMU_UART_FCR_RXCLR  0x02    /* Clear Receiver FIFO */
#define EMU_UART_FCR_TXCLR  0x04    /* Clear Transmitter FIFO */

/* Interrupt Enable Register bits */
#define EMU_UART_IER_ERDA   0x01    /* Enable Received Data Available */
#define EMU_UART_IER_ETBEI  0x02    /* Enable THR Empty */
#define EMU_UART_IER_ERLSI  0x04    /* Enable Receiver Line Status */
#define EMU_UART_IER_EDSSI  0x08    /* Enable Modem Status */

/*
 * UART device context
 */
struct emu_uart {
    uint8_t     u_regs[EMU_UART_REG_SIZE];  /* Register file */
    uint8_t     u_rx_fifo[16];              /* Receive FIFO (16 bytes) */
    uint8_t     u_tx_fifo[16];              /* Transmit FIFO (16 bytes) */
    int         u_rx_head;                  /* RX FIFO write index */
    int         u_rx_tail;                  /* RX FIFO read index */
    int         u_tx_head;                  /* TX FIFO write index */
    int         u_tx_tail;                  /* TX FIFO read index */
    int         u_rx_count;                 /* Characters in RX FIFO */
    int         u_tx_count;                 /* Characters in TX FIFO */
    bool        u_dlab;                     /* Divisor Latch Access */
    bool        u_fifo_enabled;             /* FIFO mode enabled */
    uint32_t    u_baud;                     /* Baud rate (calculated) */
    int         u_dlla;                     /* Divisor Latch (LSB+MSB) */
    
    /* Console output callback */
    void (*u_console_write)(void *arg, const char *buf, int len);
    void *u_console_arg;
    
    /* Statistics */
    uint64_t    u_tx_bytes;                 /* Total bytes transmitted */
    uint64_t    u_rx_bytes;                 /* Total bytes received */
    uint64_t    u_overruns;                 /* RX overruns */
    uint64_t    u_errors;                   /* Total errors */
};

/* UART operations */
int     emu_uart_init(struct emu_uart *uart);
void    emu_uart_destroy(struct emu_uart *uart);
void    emu_uart_reset(struct emu_uart *uart);

/* MMIO access */
int     emu_uart_read(struct emu_uart *uart, uint64_t offset, int size, uint64_t *value);
int     emu_uart_write(struct emu_uart *uart, uint64_t offset, int size, uint64_t value);

/* Console interface */
void    emu_uart_set_console(struct emu_uart *uart, 
            void (*write)(void *, const char *, int), void *arg);
int     emu_uart_receive(struct emu_uart *uart, const char *buf, int len);
int     emu_uart_transmit(struct emu_uart *uart, char *buf, int len);

/* Status queries */
bool    emu_uart_data_ready(struct emu_uart *uart);
bool    emu_uart_thr_empty(struct emu_uart *uart);
bool    emu_uart_temt_empty(struct emu_uart *uart);

#endif /* !_EMU_DEV_UART_H_ */
