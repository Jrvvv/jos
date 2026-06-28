#ifndef NET_ARP_H
#define NET_ARP_H

#include <inc/types.h>

/* ARP packet for Ethernet/IPv4 (fixed 28-byte body) */
struct arp_pkt {
    uint16_t htype;   /* hardware type: 1 = Ethernet */
    uint16_t ptype;   /* protocol type: 0x0800 = IPv4 */
    uint8_t  hlen;    /* hardware address length: 6 */
    uint8_t  plen;    /* protocol address length: 4 */
    uint16_t oper;    /* operation: 1 = request, 2 = reply */
    uint8_t  sha[6];  /* sender hardware address */
    uint8_t  spa[4];  /* sender protocol address */
    uint8_t  tha[6];  /* target hardware address */
    uint8_t  tpa[4];  /* target protocol address */
} __attribute__((packed));

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

/* Called by eth_input() when EtherType == ARP */
void arp_input(const uint8_t src_mac[6], const void *data, size_t len);

#endif /* NET_ARP_H */
