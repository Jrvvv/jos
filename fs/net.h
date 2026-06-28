#ifndef NET_H
#define NET_H

#include <inc/types.h>
#include <inc/string.h>

/*
 * JOS network configuration (fixed for QEMU user-mode networking).
 *   IP  : 192.168.56.101
 *   MAC : 52:54:00:12:34:56  (set in GNUmakefile via -device e1000,mac=...)
 *   GW  : 192.168.56.1       (QEMU SLIRP gateway)
 *
 * IP addresses are kept in HOST byte order (MSB first in the uint32_t).
 */
#define NET_IP_A  192
#define NET_IP_B  168
#define NET_IP_C   56
#define NET_IP_D  101

#define NET_IP4(a,b,c,d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
     ((uint32_t)(c) <<  8) |  (uint32_t)(d))

#define NET_OUR_IP  NET_IP4(192, 168, 56, 101)
#define NET_GW_IP   NET_IP4(192, 168, 56,   1)

/* Global state (defined in net.c) */
extern uint8_t  net_our_mac[6];
extern uint32_t net_our_ip;    /* host byte order */

/* -----------------------------------------------------------------------
 * Byte-order helpers (x86 is little-endian; network is big-endian).
 * The one's-complement checksum is endianness-neutral, so ip_checksum()
 * in net_ip.c works correctly with these helpers.
 * ----------------------------------------------------------------------- */
static inline uint16_t htons(uint16_t v) { return __builtin_bswap16(v); }
static inline uint16_t ntohs(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }

/* -----------------------------------------------------------------------
 * ARP table: maps IPv4 address (host order) → Ethernet MAC.
 * Entries are learned from incoming ARP requests and IP packets.
 * ----------------------------------------------------------------------- */
#define ARP_TABLE_SIZE 16

void arp_update(uint32_t ip, const uint8_t mac[6]);
int  arp_lookup(uint32_t ip, uint8_t mac_out[6]);

/* Main network polling loop (runs in child process after fork) */
void net_serve(void);

#endif /* NET_H */
