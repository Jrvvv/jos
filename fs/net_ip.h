#ifndef NET_IP_H
#define NET_IP_H

#include <inc/types.h>

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP  17

/* Minimal IPv4 header (20 bytes, no options) */
struct ip_hdr {
    uint8_t  ver_ihl;     /* 0x45 = version 4, IHL = 5 (20 bytes) */
    uint8_t  tos;
    uint16_t total_len;   /* header + payload, big-endian */
    uint16_t id;          /* big-endian */
    uint16_t flags_frag;  /* big-endian */
    uint8_t  ttl;
    uint8_t  proto;       /* IP_PROTO_* */
    uint16_t checksum;    /* one's complement over header */
    uint8_t  src[4];
    uint8_t  dst[4];
} __attribute__((packed));

/*
 * One's-complement checksum over `len` bytes at `data`.
 * Works correctly on little-endian x86: reading pairs as native uint16_t
 * is equivalent (by symmetry of one's complement) to reading big-endian.
 */
uint16_t ip_checksum(const void *data, size_t len);

/* Build and send an IP datagram (no fragmentation) */
void ip_send(uint32_t dst_ip, const uint8_t dst_mac[6],
             uint8_t proto, const void *payload, size_t payload_len);

/* Dispatch a received IP datagram to ICMP / UDP / TCP */
void ip_input(const uint8_t src_mac[6], const void *data, size_t len);

#endif /* NET_IP_H */
