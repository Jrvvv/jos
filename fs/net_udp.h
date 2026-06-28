#ifndef NET_UDP_H
#define NET_UDP_H

#include <inc/types.h>

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;    /* header + data, big-endian */
    uint16_t checksum;  /* optional for IPv4; we send 0 */
} __attribute__((packed));

/* Well-known ports handled by the UDP echo server */
#define UDP_PORT_ECHO  7      /* RFC 862 echo */
#define UDP_PORT_TEST  10001  /* test port forwarded by QEMU hostfwd */

/* Send a UDP datagram */
void udp_send(uint32_t dst_ip, const uint8_t dst_mac[6],
              uint16_t src_port, uint16_t dst_port,
              const void *payload, size_t payload_len);

/* Called by ip_input() when proto == IP_PROTO_UDP */
void udp_input(uint32_t src_ip, const uint8_t src_mac[6],
               const void *data, size_t len);

#endif /* NET_UDP_H */
