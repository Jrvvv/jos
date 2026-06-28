/*
 * UDP layer — echo server.
 *
 * Any datagram arriving on port 7 (RFC 862) or port 10001 (QEMU test port)
 * is reflected back to the sender.  All other ports are silently dropped.
 *
 * The UDP checksum is set to 0 (optional per RFC 768 for IPv4).
 */

#include "net.h"
#include "net_udp.h"
#include "net_ip.h"
#include <inc/string.h>
#include <inc/lib.h>

#define UDP_TX_BUF_SIZE 1472  /* MTU 1500 - IP 20 - UDP 8 */

static uint8_t udp_tx_buf[sizeof(struct udp_hdr) + UDP_TX_BUF_SIZE];

void
udp_send(uint32_t dst_ip, const uint8_t dst_mac[6],
         uint16_t src_port, uint16_t dst_port,
         const void *payload, size_t payload_len)
{
    if (payload_len > UDP_TX_BUF_SIZE)
        payload_len = UDP_TX_BUF_SIZE;

    struct udp_hdr *h = (struct udp_hdr *)udp_tx_buf;
    h->src_port = htons(src_port);
    h->dst_port = htons(dst_port);
    h->length   = htons((uint16_t)(sizeof(*h) + payload_len));
    h->checksum = 0;  /* checksum disabled */

    memcpy(udp_tx_buf + sizeof(*h), payload, payload_len);
    ip_send(dst_ip, dst_mac, IP_PROTO_UDP, udp_tx_buf, sizeof(*h) + payload_len);
}

void
udp_input(uint32_t src_ip, const uint8_t src_mac[6],
          const void *data, size_t len)
{
    if (len < sizeof(struct udp_hdr))
        return;

    const struct udp_hdr *h = (const struct udp_hdr *)data;
    uint16_t dst_port   = ntohs(h->dst_port);
    uint16_t src_port   = ntohs(h->src_port);
    uint16_t udp_len    = ntohs(h->length);
    size_t   payload_len;
    const uint8_t *payload;

    if (udp_len < sizeof(*h))
        return;
    payload_len = udp_len - sizeof(*h);
    if (payload_len > len - sizeof(*h))
        payload_len = len - sizeof(*h);
    payload = (const uint8_t *)data + sizeof(*h);

    cprintf("net/udp: port %d <- %d.%d.%d.%d:%d  %zu bytes\n",
            dst_port,
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >>  8) & 0xFF,  src_ip        & 0xFF,
            src_port, payload_len);

    /* Echo server: reflect the datagram back to the sender */
    if (dst_port == UDP_PORT_ECHO || dst_port == UDP_PORT_TEST) {
        udp_send(src_ip, src_mac, dst_port, src_port, payload, payload_len);
        cprintf("net/udp: echo reply -> %d.%d.%d.%d:%d\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >>  8) & 0xFF,  src_ip        & 0xFF,
                src_port);
    }
}
