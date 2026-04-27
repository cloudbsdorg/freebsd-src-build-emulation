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

#ifndef _EMU_DEV_NET_H_
#define _EMU_DEV_NET_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/*
 * Virtio Network Device Emulation
 * 
 * Emulates a virtio-net device for network connectivity.
 * Security: Host-only mode by default, NAT mode optional.
 * No external access without explicit configuration.
 * MAC filtering, packet validation, rate limiting.
 */

/* Network device modes */
#define EMU_NET_MODE_HOSTONLY   0       /* Internal network only */
#define EMU_NET_MODE_NAT        1       /* Outbound NAT access */
#define EMU_NET_MODE_BRIDGE     2       /* Bridged to host interface */

/* Network statistics */
#define EMU_NET_STATS_RX_PACKETS        0
#define EMU_NET_STATS_RX_BYTES          1
#define EMU_NET_STATS_RX_ERRORS         2
#define EMU_NET_STATS_RX_DROPPED        3
#define EMU_NET_STATS_TX_PACKETS        4
#define EMU_NET_STATS_TX_BYTES          5
#define EMU_NET_STATS_TX_ERRORS         6
#define EMU_NET_STATS_TX_DROPPED        7
#define EMU_NET_STATS_COUNT             8

/* MAC address length */
#define EMU_NET_MAC_LEN         6

/* Maximum packet size (including headers) */
#define EMU_NET_MAX_PKT_SIZE    1518    /* Standard Ethernet MTU + headers */

/* Virtqueue size */
#define EMU_NET_RX_QUEUE_SIZE   256
#define EMU_NET_TX_QUEUE_SIZE   256

/*
 * Network device context
 */
struct emu_net {
    /* Device configuration */
    int         n_mode;                 /* Network mode (hostonly/NAT/bridge) */
    uint8_t     n_mac[EMU_NET_MAC_LEN]; /* MAC address */
    char        *n_ifname;              /* Interface name (for bridge mode) */
    
    /* Virtqueues */
    void        *n_rx_vq;               /* Receive virtqueue */
    void        *n_tx_vq;               /* Transmit virtqueue */
    pthread_mutex_t n_rx_lock;          /* RX queue lock */
    pthread_mutex_t n_tx_lock;          /* TX queue lock */
    
    /* Packet buffers */
    uint8_t     *n_rx_buf;              /* RX buffer */
    size_t      n_rx_buf_size;          /* RX buffer size */
    uint8_t     *n_tx_buf;              /* TX buffer */
    size_t      n_tx_buf_size;          /* TX buffer size */
    
    /* Statistics */
    uint64_t    n_stats[EMU_NET_STATS_COUNT];
    
    /* Link state */
    bool        n_link_up;              /* Link status */
    uint32_t    n_speed;                /* Link speed (Mbps) */
    bool        n_duplex;               /* Full duplex */
    
    /* Rate limiting */
    uint64_t    n_rx_rate_limit;        /* RX rate limit (bytes/sec) */
    uint64_t    n_tx_rate_limit;        /* TX rate limit (bytes/sec) */
    uint64_t    n_rx_tokens;            /* RX token bucket */
    uint64_t    n_tx_tokens;            /* TX token bucket */
    
    /* Filter flags */
    bool        n_promisc;              /* Promiscuous mode */
    bool        n_allmulti;             /* All-multicast mode */
    uint8_t     n_mac_filter[16][EMU_NET_MAC_LEN];  /* MAC filter table */
    int         n_mac_filter_count;     /* Number of MAC filters */
};

/* Network operations */
int     emu_net_init(struct emu_net *net, int mode, const uint8_t *mac);
void    emu_net_destroy(struct emu_net *net);
int     emu_net_open(struct emu_net *net, const char *ifname);
void    emu_net_close(struct emu_net *net);

/* Packet I/O */
int     emu_net_receive(struct emu_net *net, const uint8_t *pkt, size_t len);
int     emu_net_transmit(struct emu_net *net, uint8_t *pkt, size_t len);

/* Configuration */
int     emu_net_set_mode(struct emu_net *net, int mode);
int     emu_net_set_mac(struct emu_net *net, const uint8_t *mac);
int     emu_net_add_mac_filter(struct emu_net *net, const uint8_t *mac);
void    emu_net_clear_mac_filters(struct emu_net *net);
int     emu_net_set_promisc(struct emu_net *net, bool enable);
int     emu_net_set_rate_limit(struct emu_net *net, uint64_t rx_limit, 
            uint64_t tx_limit);

/* Status queries */
bool    emu_net_is_link_up(struct emu_net *net);
int     emu_net_get_mode(struct emu_net *net);
const uint8_t *emu_net_get_mac(struct emu_net *net);
uint64_t emu_net_get_stat(struct emu_net *net, int stat_id);

/* Helper functions */
int     emu_net_validate_mac(const uint8_t *mac);
bool    emu_net_is_multicast(const uint8_t *mac);
bool    emu_net_mac_matches(const uint8_t *mac1, const uint8_t *mac2);

#endif /* !_EMU_DEV_NET_H_ */
