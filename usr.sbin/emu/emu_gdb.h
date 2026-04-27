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

#ifndef _EMU_GDB_H_
#define _EMU_GDB_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/*
 * GDB Remote Stub for Emulation Framework
 * 
 * Implements GDB remote protocol (gdbserver) for debugging emulated instances.
 * Security: Binds to localhost (127.0.0.1) only, no external connections.
 * Optional authentication, rate limiting, command validation.
 */

/* Default GDB stub port */
#define EMU_GDB_DEFAULT_PORT    1234

/* Maximum packet size (GDB protocol limit) */
#define EMU_GDB_MAX_PACKET      4096

/* Maximum register count (for x86-64) */
#define EMU_GDB_MAX_REGS        256

/* GDB stub states */
#define EMU_GDB_STATE_STOPPED   0
#define EMU_GDB_STATE_RUNNING   1
#define EMU_GDB_STATE_SINGLE    2

/* GDB signal numbers (Unix signal mapping) */
#define EMU_GDB_SIGINT          2       /* Interrupt */
#define EMU_GDB_SIGTRAP         5       /* Trap */
#define EMU_GDB_SIGSEGV         11      /* Segmentation violation */
#define EMU_GDB_SIGTERM         15      /* Termination */

/*
 * GDB stub context
 */
struct emu_gdb {
    /* Network configuration */
    int         g_listen_fd;            /* Listen socket */
    int         g_conn_fd;              /* Client connection */
    uint16_t    g_port;                 /* Port number */
    char        *g_bind_addr;           /* Bind address (always localhost) */
    
    /* Authentication */
    bool        g_auth_enabled;         /* Authentication required */
    char        *g_password;            /* Password (if enabled) */
    
    /* Connection state */
    int         g_state;                /* Running/stopped/single */
    bool        g_connected;            /* Client connected */
    bool        g_attached;             /* Attached to instance */
    
    /* Packet handling */
    uint8_t     *g_rx_buf;              /* Receive buffer */
    size_t      g_rx_len;               /* Receive length */
    uint8_t     *g_tx_buf;              /* Transmit buffer */
    size_t      g_tx_len;               /* Transmit length */
    
    /* Register access */
    uint64_t    g_regs[EMU_GDB_MAX_REGS];   /* Register file */
    int         g_reg_count;            /* Number of registers */
    
    /* Memory access */
    void        *g_mem_base;            /* Guest memory base */
    size_t      g_mem_size;             /* Guest memory size */
    
    /* Breakpoints */
    uint64_t    g_bp_addr[16];          /* Breakpoint addresses */
    bool        g_bp_enabled[16];       /* Breakpoint enable flags */
    int         g_bp_count;             /* Number of breakpoints */
    
    /* Thread safety */
    pthread_mutex_t g_lock;             /* Stub lock */
    pthread_cond_t g_cond;              /* Condition variable */
    
    /* Statistics */
    uint64_t    g_packets_in;           /* Packets received */
    uint64_t    g_packets_out;          /* Packets sent */
    uint64_t    g_errors;               /* Protocol errors */
    
    /* Rate limiting */
    uint64_t    g_rate_limit;           /* Packets per second limit */
    uint64_t    g_rate_tokens;          /* Token bucket */
};

/* GDB stub operations */
int     emu_gdb_init(struct emu_gdb *gdb, uint16_t port);
void    emu_gdb_destroy(struct emu_gdb *gdb);
int     emu_gdb_start(struct emu_gdb *gdb);
void    emu_gdb_stop(struct emu_gdb *gdb);

/* Connection handling */
int     emu_gdb_accept(struct emu_gdb *gdb);
void    emu_gdb_disconnect(struct emu_gdb *gdb);
bool    emu_gdb_is_connected(struct emu_gdb *gdb);

/* Packet I/O */
int     emu_gdb_receive(struct emu_gdb *gdb);
int     emu_gdb_send(struct emu_gdb *gdb, const uint8_t *data, size_t len);
int     emu_gdb_send_packet(struct emu_gdb *gdb, const char *packet);

/* Protocol handlers */
int     emu_gdb_handle_query(struct emu_gdb *gdb, const char *query);
int     emu_gdb_handle_set(struct emu_gdb *gdb, const char *set);
int     emu_gdb_handle_continue(struct emu_gdb *gdb, uint64_t addr);
int     emu_gdb_handle_step(struct emu_gdb *gdb, uint64_t addr);
int     emu_gdb_handle_breakpoint(struct emu_gdb *gdb, uint64_t addr, bool set);

/* Register access */
int     emu_gdb_read_regs(struct emu_gdb *gdb, uint8_t *buf, size_t *len);
int     emu_gdb_write_regs(struct emu_gdb *gdb, const uint8_t *buf, size_t len);
int     emu_gdb_read_reg(struct emu_gdb *gdb, int regno, uint64_t *value);
int     emu_gdb_write_reg(struct emu_gdb *gdb, int regno, uint64_t value);

/* Memory access */
int     emu_gdb_read_mem(struct emu_gdb *gdb, uint64_t addr, size_t len, 
            uint8_t *buf);
int     emu_gdb_write_mem(struct emu_gdb *gdb, uint64_t addr, size_t len, 
            const uint8_t *buf);

/* Configuration */
int     emu_gdb_set_password(struct emu_gdb *gdb, const char *password);
int     emu_gdb_set_rate_limit(struct emu_gdb *gdb, uint64_t limit);
int     emu_gdb_set_memory(struct emu_gdb *gdb, void *base, size_t size);

/* Status queries */
bool    emu_gdb_is_running(struct emu_gdb *gdb);
int     emu_gdb_get_state(struct emu_gdb *gdb);
uint16_t emu_gdb_get_port(struct emu_gdb *gdb);

#endif /* !_EMU_GDB_H_ */
