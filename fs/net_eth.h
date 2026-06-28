#ifndef NET_ETH_H
#define NET_ETH_H

#include <inc/types.h>

#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;  /* big-endian (network byte order) */
} __attribute__((packed));

/* Dispatch one received raw frame (including Ethernet header) */
void eth_input(const uint8_t *frame, size_t len);

/* Build Ethernet header and send via e1000_send() */
void eth_send(const uint8_t dst[6], uint16_t ethertype,
              const void *payload, size_t payload_len);

#endif /* NET_ETH_H */
