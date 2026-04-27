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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "emu_gdb.h"

/*
 * GDB Remote Stub for Emulation Framework
 * 
 * Security: Binds to localhost (127.0.0.1) only, no external connections.
 * Optional password authentication, rate limiting, command validation.
 */

/*
 * Calculate GDB packet checksum
 */
static uint8_t
emu_gdb_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    size_t i;
    
    for (i = 0; i < len; i++)
        sum += data[i];
    
    return (sum);
}

/*
 * Validate bind address - must be localhost
 * Returns 0 if valid (localhost), -1 if invalid
 */
static int
emu_gdb_validate_bind_addr(const char *addr)
{
    if (addr == NULL)
        return (-1);
    
    /* Only allow localhost addresses */
    if (strcmp(addr, "127.0.0.1") == 0 ||
        strcmp(addr, "localhost") == 0 ||
        strcmp(addr, "::1") == 0) {
        return (0);
    }
    
    /* Reject all other addresses */
    return (-1);
}

/*
 * Initialize GDB stub
 */
int
emu_gdb_init(struct emu_gdb *gdb, uint16_t port)
{
    struct sockaddr_in sin;
    int opt;
    
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Validate port */
    if (port == 0) {
        errno = EINVAL;
        return (-1);
    }
    
    memset(gdb, 0, sizeof(*gdb));
    pthread_mutex_init(&gdb->g_lock, NULL);
    pthread_cond_init(&gdb->g_cond, NULL);
    
    /* Allocate buffers */
    gdb->g_rx_buf = malloc(EMU_GDB_MAX_PACKET);
    gdb->g_tx_buf = malloc(EMU_GDB_MAX_PACKET);
    if (gdb->g_rx_buf == NULL || gdb->g_tx_buf == NULL) {
        free(gdb->g_rx_buf);
        free(gdb->g_tx_buf);
        pthread_mutex_destroy(&gdb->g_lock);
        pthread_cond_destroy(&gdb->g_cond);
        errno = ENOMEM;
        return (-1);
    }
    
    /* Set localhost binding (security critical) */
    gdb->g_bind_addr = strdup("127.0.0.1");
    if (gdb->g_bind_addr == NULL) {
        free(gdb->g_rx_buf);
        free(gdb->g_tx_buf);
        pthread_mutex_destroy(&gdb->g_lock);
        pthread_cond_destroy(&gdb->g_cond);
        errno = ENOMEM;
        return (-1);
    }
    
    gdb->g_port = port;
    gdb->g_listen_fd = -1;
    gdb->g_conn_fd = -1;
    gdb->g_state = EMU_GDB_STATE_STOPPED;
    
    /* Create listen socket */
    gdb->g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (gdb->g_listen_fd < 0) {
        free(gdb->g_bind_addr);
        free(gdb->g_rx_buf);
        free(gdb->g_tx_buf);
        pthread_mutex_destroy(&gdb->g_lock);
        pthread_cond_destroy(&gdb->g_cond);
        return (-1);
    }
    
    /* Set socket options */
    opt = 1;
    setsockopt(gdb->g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    /* Bind to localhost only (security critical) */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* 127.0.0.1 */
    
    if (bind(gdb->g_listen_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(gdb->g_listen_fd);
        free(gdb->g_bind_addr);
        free(gdb->g_rx_buf);
        free(gdb->g_tx_buf);
        pthread_mutex_destroy(&gdb->g_lock);
        pthread_cond_destroy(&gdb->g_cond);
        return (-1);
    }
    
    /* Listen for connections */
    if (listen(gdb->g_listen_fd, 1) < 0) {
        close(gdb->g_listen_fd);
        free(gdb->g_bind_addr);
        free(gdb->g_rx_buf);
        free(gdb->g_tx_buf);
        pthread_mutex_destroy(&gdb->g_lock);
        pthread_cond_destroy(&gdb->g_cond);
        return (-1);
    }
    
    return (0);
}

/*
 * Start GDB stub (accept connections)
 */
int
emu_gdb_start(struct emu_gdb *gdb)
{
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    gdb->g_state = EMU_GDB_STATE_STOPPED;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (0);
}

/*
 * Stop GDB stub
 */
void
emu_gdb_stop(struct emu_gdb *gdb)
{
    if (gdb == NULL)
        return;
    
    pthread_mutex_lock(&gdb->g_lock);
    
    if (gdb->g_conn_fd >= 0) {
        close(gdb->g_conn_fd);
        gdb->g_conn_fd = -1;
    }
    
    gdb->g_state = EMU_GDB_STATE_STOPPED;
    gdb->g_connected = false;
    gdb->g_attached = false;
    
    pthread_mutex_unlock(&gdb->g_lock);
}

/*
 * Destroy GDB stub and free resources
 */
void
emu_gdb_destroy(struct emu_gdb *gdb)
{
    if (gdb == NULL)
        return;
    
    emu_gdb_stop(gdb);
    
    if (gdb->g_listen_fd >= 0) {
        close(gdb->g_listen_fd);
        gdb->g_listen_fd = -1;
    }
    
    if (gdb->g_bind_addr != NULL) {
        free(gdb->g_bind_addr);
        gdb->g_bind_addr = NULL;
    }
    
    if (gdb->g_password != NULL) {
        free(gdb->g_password);
        gdb->g_password = NULL;
    }
    
    if (gdb->g_rx_buf != NULL) {
        free(gdb->g_rx_buf);
        gdb->g_rx_buf = NULL;
    }
    
    if (gdb->g_tx_buf != NULL) {
        free(gdb->g_tx_buf);
        gdb->g_tx_buf = NULL;
    }
    
    pthread_mutex_destroy(&gdb->g_lock);
    pthread_cond_destroy(&gdb->g_cond);
}

/*
 * Accept client connection
 * Security: Only accepts from localhost (kernel enforces this)
 */
int
emu_gdb_accept(struct emu_gdb *gdb)
{
    struct sockaddr_in sin;
    socklen_t sin_len;
    
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (gdb->g_listen_fd < 0) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Accept connection */
    sin_len = sizeof(sin);
    gdb->g_conn_fd = accept(gdb->g_listen_fd, (struct sockaddr *)&sin, &sin_len);
    if (gdb->g_conn_fd < 0)
        return (-1);
    
    /* Verify client is localhost (defense in depth) */
    if (sin.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        close(gdb->g_conn_fd);
        gdb->g_conn_fd = -1;
        errno = EPERM;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    gdb->g_connected = true;
    gdb->g_packets_in = 0;
    gdb->g_packets_out = 0;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (0);
}

/*
 * Disconnect client
 */
void
emu_gdb_disconnect(struct emu_gdb *gdb)
{
    if (gdb == NULL)
        return;
    
    pthread_mutex_lock(&gdb->g_lock);
    
    if (gdb->g_conn_fd >= 0) {
        close(gdb->g_conn_fd);
        gdb->g_conn_fd = -1;
    }
    
    gdb->g_connected = false;
    gdb->g_attached = false;
    
    pthread_mutex_unlock(&gdb->g_lock);
}

/*
 * Check if client is connected
 */
bool
emu_gdb_is_connected(struct emu_gdb *gdb)
{
    bool connected;
    
    if (gdb == NULL)
        return (false);
    
    pthread_mutex_lock(&gdb->g_lock);
    connected = gdb->g_connected;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (connected);
}

/*
 * Send GDB packet with checksum
 */
int
emu_gdb_send_packet(struct emu_gdb *gdb, const char *packet)
{
    char *buf;
    size_t len;
    uint8_t cksum;
    int ret;
    
    if (gdb == NULL || packet == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (gdb->g_conn_fd < 0) {
        errno = ENOTCONN;
        return (-1);
    }
    
    len = strlen(packet);
    
    /* Allocate buffer for $packet#XX */
    buf = malloc(len + 5);
    if (buf == NULL) {
        errno = ENOMEM;
        return (-1);
    }
    
    /* Format packet */
    buf[0] = '$';
    memcpy(buf + 1, packet, len);
    buf[len + 1] = '#';
    cksum = emu_gdb_checksum((const uint8_t *)packet, len);
    snprintf(buf + len + 2, 3, "%02x", cksum);
    
    pthread_mutex_lock(&gdb->g_lock);
    
    /* Send packet */
    ret = write(gdb->g_conn_fd, buf, len + 4);
    if (ret > 0) {
        gdb->g_packets_out++;
        gdb->g_tx_len = ret;
    }
    
    pthread_mutex_unlock(&gdb->g_lock);
    
    free(buf);
    
    return (ret);
}

/*
 * Set password for authentication
 */
int
emu_gdb_set_password(struct emu_gdb *gdb, const char *password)
{
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    
    if (gdb->g_password != NULL) {
        free(gdb->g_password);
        gdb->g_password = NULL;
    }
    
    if (password != NULL && strlen(password) > 0) {
        gdb->g_password = strdup(password);
        gdb->g_auth_enabled = true;
    } else {
        gdb->g_auth_enabled = false;
    }
    
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (0);
}

/*
 * Set rate limit
 */
int
emu_gdb_set_rate_limit(struct emu_gdb *gdb, uint64_t limit)
{
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    gdb->g_rate_limit = limit;
    gdb->g_rate_tokens = limit;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (0);
}

/*
 * Set guest memory region
 */
int
emu_gdb_set_memory(struct emu_gdb *gdb, void *base, size_t size)
{
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    gdb->g_mem_base = base;
    gdb->g_mem_size = size;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (0);
}

/*
 * Read memory (bounds-checked)
 */
int
emu_gdb_read_mem(struct emu_gdb *gdb, uint64_t addr, size_t len, uint8_t *buf)
{
    if (gdb == NULL || buf == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    
    /* Bounds check */
    if (gdb->g_mem_base == NULL || addr + len > gdb->g_mem_size) {
        pthread_mutex_unlock(&gdb->g_lock);
        errno = EFAULT;
        return (-1);
    }
    
    memcpy(buf, (uint8_t *)gdb->g_mem_base + addr, len);
    
    pthread_mutex_unlock(&gdb->g_lock);
    
    return ((int)len);
}

/*
 * Write memory (bounds-checked)
 */
int
emu_gdb_write_mem(struct emu_gdb *gdb, uint64_t addr, size_t len, 
    const uint8_t *buf)
{
    if (gdb == NULL || buf == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    
    /* Bounds check */
    if (gdb->g_mem_base == NULL || addr + len > gdb->g_mem_size) {
        pthread_mutex_unlock(&gdb->g_lock);
        errno = EFAULT;
        return (-1);
    }
    
    memcpy((uint8_t *)gdb->g_mem_base + addr, buf, len);
    
    pthread_mutex_unlock(&gdb->g_lock);
    
    return ((int)len);
}

/*
 * Check if stub is running
 */
bool
emu_gdb_is_running(struct emu_gdb *gdb)
{
    bool running;
    
    if (gdb == NULL)
        return (false);
    
    pthread_mutex_lock(&gdb->g_lock);
    running = (gdb->g_state == EMU_GDB_STATE_RUNNING);
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (running);
}

/*
 * Get stub state
 */
int
emu_gdb_get_state(struct emu_gdb *gdb)
{
    int state;
    
    if (gdb == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&gdb->g_lock);
    state = gdb->g_state;
    pthread_mutex_unlock(&gdb->g_lock);
    
    return (state);
}

/*
 * Get port number
 */
uint16_t
emu_gdb_get_port(struct emu_gdb *gdb)
{
    if (gdb == NULL) {
        errno = EINVAL;
        return (0);
    }
    
    return (gdb->g_port);
}
