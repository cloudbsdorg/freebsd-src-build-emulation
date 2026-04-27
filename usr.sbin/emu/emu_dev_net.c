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
#include <sys/ioctl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "emu_dev_net.h"

/*
 * Virtio Network Device Emulation
 * 
 * Security: Host-only mode by default, NAT mode requires explicit configuration.
 * No external network access without explicit user configuration.
 * MAC filtering prevents unauthorized traffic.
 */

/*
 * Validate MAC address
 * Returns 0 if valid, -1 if invalid
 */
int
emu_net_validate_mac(const uint8_t *mac)
{
    if (mac == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Check for multicast bit in first octet */
    if (mac[0] & 0x01) {
        /* Multicast MAC - typically invalid for device MAC */
        return (-1);
    }
    
    /* Check for all zeros or all ones */
    if (memcmp(mac, "\x00\x00\x00\x00\x00\x00", 6) == 0 ||
        memcmp(mac, "\xff\xff\xff\xff\xff\xff", 6) == 0) {
        return (-1);
    }
    
    return (0);
}

/*
 * Check if MAC is multicast
 */
bool
emu_net_is_multicast(const uint8_t *mac)
{
    if (mac == NULL)
        return (false);
    
    return ((mac[0] & 0x01) != 0);
}

/*
 * Check if two MAC addresses match
 */
bool
emu_net_mac_matches(const uint8_t *mac1, const uint8_t *mac2)
{
    if (mac1 == NULL || mac2 == NULL)
        return (false);
    
    return (memcmp(mac1, mac2, EMU_NET_MAC_LEN) == 0);
}

/*
 * Generate random MAC address (locally administered)
 */
static void
emu_net_generate_mac(uint8_t *mac)
{
    int i;
    
    /* Use /dev/urandom for randomness */
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL) {
        fread(mac, 1, EMU_NET_MAC_LEN, f);
        fclose(f);
    } else {
        /* Fallback to time-based */
        uint32_t seed = time(NULL);
        for (i = 0; i < EMU_NET_MAC_LEN; i++)
            mac[i] = (seed + i * 17) & 0xFF;
    }
    
    /* Set locally administered bit */
    mac[0] |= 0x02;
    
    /* Clear multicast bit */
    mac[0] &= ~0x01;
}

/*
 * Initialize network device
 */
int
emu_net_init(struct emu_net *net, int mode, const uint8_t *mac)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Validate mode */
    if (mode < 0 || mode > EMU_NET_MODE_BRIDGE) {
        errno = EINVAL;
        return (-1);
    }
    
    memset(net, 0, sizeof(*net));
    pthread_mutex_init(&net->n_rx_lock, NULL);
    pthread_mutex_init(&net->n_tx_lock, NULL);
    
    /* Set mode (default to host-only) */
    net->n_mode = mode;
    
    /* Set MAC address */
    if (mac != NULL) {
        if (emu_net_validate_mac(mac) != 0) {
            pthread_mutex_destroy(&net->n_rx_lock);
            pthread_mutex_destroy(&net->n_tx_lock);
            errno = EINVAL;
            return (-1);
        }
        memcpy(net->n_mac, mac, EMU_NET_MAC_LEN);
    } else {
        emu_net_generate_mac(net->n_mac);
    }
    
    /* Allocate packet buffers */
    net->n_rx_buf = malloc(EMU_NET_MAX_PKT_SIZE);
    net->n_tx_buf = malloc(EMU_NET_MAX_PKT_SIZE);
    if (net->n_rx_buf == NULL || net->n_tx_buf == NULL) {
        free(net->n_rx_buf);
        free(net->n_tx_buf);
        pthread_mutex_destroy(&net->n_rx_lock);
        pthread_mutex_destroy(&net->n_tx_lock);
        errno = ENOMEM;
        return (-1);
    }
    net->n_rx_buf_size = EMU_NET_MAX_PKT_SIZE;
    net->n_tx_buf_size = EMU_NET_MAX_PKT_SIZE;
    
    /* Set default link state */
    net->n_link_up = false;
    net->n_speed = 1000;  /* 1 Gbps */
    net->n_duplex = true;
    
    /* Default rate limits (unlimited) */
    net->n_rx_rate_limit = 0;
    net->n_tx_rate_limit = 0;
    net->n_rx_tokens = 0;
    net->n_tx_tokens = 0;
    
    return (0);
}

/*
 * Open network device
 */
int
emu_net_open(struct emu_net *net, const char *ifname)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Store interface name for bridge mode */
    if (net->n_mode == EMU_NET_MODE_BRIDGE && ifname != NULL) {
        net->n_ifname = strdup(ifname);
        if (net->n_ifname == NULL) {
            errno = ENOMEM;
            return (-1);
        }
    }
    
    /* Bring link up */
    net->n_link_up = true;
    
    return (0);
}

/*
 * Close network device
 */
void
emu_net_close(struct emu_net *net)
{
    if (net == NULL)
        return;
    
    net->n_link_up = false;
    
    if (net->n_ifname != NULL) {
        free(net->n_ifname);
        net->n_ifname = NULL;
    }
}

/*
 * Destroy network device and free resources
 */
void
emu_net_destroy(struct emu_net *net)
{
    if (net == NULL)
        return;
    
    emu_net_close(net);
    
    if (net->n_rx_buf != NULL) {
        free(net->n_rx_buf);
        net->n_rx_buf = NULL;
    }
    if (net->n_tx_buf != NULL) {
        free(net->n_tx_buf);
        net->n_tx_buf = NULL;
    }
    
    pthread_mutex_destroy(&net->n_rx_lock);
    pthread_mutex_destroy(&net->n_tx_lock);
}

/*
 * Set network mode
 * Returns 0 on success, -1 on failure
 */
int
emu_net_set_mode(struct emu_net *net, int mode)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (mode < 0 || mode > EMU_NET_MODE_BRIDGE) {
        errno = EINVAL;
        return (-1);
    }
    
    net->n_mode = mode;
    return (0);
}

/*
 * Set MAC address
 */
int
emu_net_set_mac(struct emu_net *net, const uint8_t *mac)
{
    if (net == NULL || mac == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (emu_net_validate_mac(mac) != 0) {
        errno = EINVAL;
        return (-1);
    }
    
    memcpy(net->n_mac, mac, EMU_NET_MAC_LEN);
    return (0);
}

/*
 * Add MAC filter
 */
int
emu_net_add_mac_filter(struct emu_net *net, const uint8_t *mac)
{
    if (net == NULL || mac == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    if (net->n_mac_filter_count >= 16) {
        errno = ENOSPC;
        return (-1);
    }
    
    memcpy(net->n_mac_filter[net->n_mac_filter_count], mac, EMU_NET_MAC_LEN);
    net->n_mac_filter_count++;
    
    return (0);
}

/*
 * Clear all MAC filters
 */
void
emu_net_clear_mac_filters(struct emu_net *net)
{
    if (net == NULL)
        return;
    
    net->n_mac_filter_count = 0;
}

/*
 * Set promiscuous mode
 */
int
emu_net_set_promisc(struct emu_net *net, bool enable)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    net->n_promisc = enable;
    return (0);
}

/*
 * Set rate limits
 */
int
emu_net_set_rate_limit(struct emu_net *net, uint64_t rx_limit, uint64_t tx_limit)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    net->n_rx_rate_limit = rx_limit;
    net->n_tx_rate_limit = tx_limit;
    net->n_rx_tokens = rx_limit;
    net->n_tx_tokens = tx_limit;
    
    return (0);
}

/*
 * Receive packet from network
 * This is a simplified implementation - full implementation would
 * integrate with tap/tun or other network backend
 */
int
emu_net_receive(struct emu_net *net, const uint8_t *pkt, size_t len)
{
    if (net == NULL || pkt == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Check link state */
    if (!net->n_link_up) {
        net->n_stats[EMU_NET_STATS_RX_DROPPED]++;
        errno = ENETDOWN;
        return (-1);
    }
    
    /* Validate packet size */
    if (len > EMU_NET_MAX_PKT_SIZE || len < 14) {
        net->n_stats[EMU_NET_STATS_RX_ERRORS]++;
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&net->n_rx_lock);
    
    /* Check MAC filter if not promiscuous */
    if (!net->n_promisc && net->n_mac_filter_count > 0) {
        bool matched = false;
        int i;
        
        /* Check against device MAC */
        if (emu_net_mac_matches(pkt, net->n_mac))
            matched = true;
        
        /* Check against filter table */
        for (i = 0; i < net->n_mac_filter_count && !matched; i++) {
            if (emu_net_mac_matches(pkt, net->n_mac_filter[i]))
                matched = true;
        }
        
        /* Check for multicast if allmulti enabled */
        if (!matched && net->n_allmulti && emu_net_is_multicast(pkt))
            matched = true;
        
        if (!matched) {
            pthread_mutex_unlock(&net->n_rx_lock);
            net->n_stats[EMU_NET_STATS_RX_DROPPED]++;
            return (0);  /* Silently drop */
        }
    }
    
    /* Update statistics */
    net->n_stats[EMU_NET_STATS_RX_PACKETS]++;
    net->n_stats[EMU_NET_STATS_RX_BYTES] += len;
    
    pthread_mutex_unlock(&net->n_rx_lock);
    
    /* Packet would be delivered to guest via virtqueue here */
    return ((int)len);
}

/*
 * Transmit packet to network
 */
int
emu_net_transmit(struct emu_net *net, uint8_t *pkt, size_t len)
{
    if (net == NULL || pkt == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    /* Check link state */
    if (!net->n_link_up) {
        net->n_stats[EMU_NET_STATS_TX_DROPPED]++;
        errno = ENETDOWN;
        return (-1);
    }
    
    /* Validate packet size */
    if (len > EMU_NET_MAX_PKT_SIZE || len < 14) {
        net->n_stats[EMU_NET_STATS_TX_ERRORS]++;
        errno = EINVAL;
        return (-1);
    }
    
    pthread_mutex_lock(&net->n_tx_lock);
    
    /* Update statistics */
    net->n_stats[EMU_NET_STATS_TX_PACKETS]++;
    net->n_stats[EMU_NET_STATS_TX_BYTES] += len;
    
    pthread_mutex_unlock(&net->n_tx_lock);
    
    /* 
     * In host-only mode, packet stays within virtual network
     * In NAT mode, packet would be forwarded to host network
     * In bridge mode, packet would be sent to bridged interface
     * 
     * For now, just accept the packet (simplified implementation)
     */
    
    return ((int)len);
}

/*
 * Check if link is up
 */
bool
emu_net_is_link_up(struct emu_net *net)
{
    if (net == NULL)
        return (false);
    
    return (net->n_link_up);
}

/*
 * Get network mode
 */
int
emu_net_get_mode(struct emu_net *net)
{
    if (net == NULL) {
        errno = EINVAL;
        return (-1);
    }
    
    return (net->n_mode);
}

/*
 * Get MAC address
 */
const uint8_t *
emu_net_get_mac(struct emu_net *net)
{
    if (net == NULL) {
        errno = EINVAL;
        return (NULL);
    }
    
    return (net->n_mac);
}

/*
 * Get statistic value
 */
uint64_t
emu_net_get_stat(struct emu_net *net, int stat_id)
{
    if (net == NULL || stat_id < 0 || stat_id >= EMU_NET_STATS_COUNT) {
        errno = EINVAL;
        return (0);
    }
    
    return (net->n_stats[stat_id]);
}
