#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <inc/types.h>

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

/* Called by ip_input() when proto == IP_PROTO_ICMP */
void icmp_input(uint32_t src_ip, const uint8_t src_mac[6],
                const void *data, size_t len);

#endif /* NET_ICMP_H */
