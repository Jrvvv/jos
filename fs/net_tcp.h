#ifndef NET_TCP_H
#define NET_TCP_H

#include <inc/types.h>
#include "net_ip.h"

/* TCP flag bits (in the flags byte of the header) */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;       /* big-endian */
    uint32_t ack;       /* big-endian */
    uint8_t  data_off;  /* upper 4 bits = header length in 32-bit words */
    uint8_t  flags;     /* TCP_* bits */
    uint16_t window;    /* big-endian */
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

/* Listening port for the HTTP server */
#define TCP_HTTP_PORT 80

/* Initialise the TCP server (set state to LISTEN) */
void tcp_init(void);

/* Called by ip_input() when proto == IP_PROTO_TCP */
void tcp_input(uint32_t src_ip, const uint8_t src_mac[6],
               const struct ip_hdr *ip, const void *data, size_t len);

/* Send data on the current established connection */
void tcp_send_data(const void *data, size_t len);

/* Initiate an active close (send FIN) on the current connection */
void tcp_close(void);

#endif /* NET_TCP_H */
