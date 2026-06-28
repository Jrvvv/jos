/*
 * IPv4 layer.
 *
 * Responsibilities:
 *   - Compute and verify IP header checksums.
 *   - Build outgoing IP headers (id counter, TTL=64, no fragmentation).
 *   - Strip and validate incoming IP headers, then dispatch to
 *     ICMP / UDP / TCP by protocol number.
 *   - Passively learn IP→MAC mapping from every incoming packet.
 */

#include "net.h"
#include "net_ip.h"
#include "net_eth.h"
#include "net_icmp.h"
#include "net_udp.h"
#include "net_tcp.h"
#include <inc/string.h>

static uint16_t ip_id;        /* monotonic datagram ID */
static uint8_t  ip_tx_buf[1500];

uint16_t
ip_checksum(const void *data, size_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += *(const uint8_t *)p;  /* last byte padded with 0 */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void
ip_send(uint32_t dst_ip, const uint8_t dst_mac[6],
        uint8_t proto, const void *payload, size_t payload_len)
{
    struct ip_hdr *h = (struct ip_hdr *)ip_tx_buf;

    h->ver_ihl    = 0x45;
    h->tos        = 0;
    h->total_len  = htons((uint16_t)(sizeof(*h) + payload_len));
    h->id         = htons(ip_id++);
    h->flags_frag = 0;
    h->ttl        = 64;
    h->proto      = proto;
    h->checksum   = 0;

    h->src[0] = (net_our_ip >> 24) & 0xFF;
    h->src[1] = (net_our_ip >> 16) & 0xFF;
    h->src[2] = (net_our_ip >>  8) & 0xFF;
    h->src[3] =  net_our_ip        & 0xFF;

    h->dst[0] = (dst_ip >> 24) & 0xFF;
    h->dst[1] = (dst_ip >> 16) & 0xFF;
    h->dst[2] = (dst_ip >>  8) & 0xFF;
    h->dst[3] =  dst_ip        & 0xFF;

    h->checksum = ip_checksum(h, sizeof(*h));

    if (payload_len > sizeof(ip_tx_buf) - sizeof(*h))
        payload_len = sizeof(ip_tx_buf) - sizeof(*h);
    memcpy(ip_tx_buf + sizeof(*h), payload, payload_len);

    eth_send(dst_mac, ETHERTYPE_IPV4, ip_tx_buf, sizeof(*h) + payload_len);
}

void
ip_input(const uint8_t src_mac[6], const void *data, size_t len)
{
    if (len < sizeof(struct ip_hdr))
        return;

    const struct ip_hdr *h = (const struct ip_hdr *)data;

    if ((h->ver_ihl >> 4) != 4)
        return;  /* not IPv4 */

    uint8_t ihl = (h->ver_ihl & 0x0F) * 4;
    if (len < ihl)
        return;

    /* Accept only packets destined for us */
    uint32_t dst = ((uint32_t)h->dst[0] << 24) | ((uint32_t)h->dst[1] << 16)
                 | ((uint32_t)h->dst[2] <<  8) |  (uint32_t)h->dst[3];
    if (dst != net_our_ip)
        return;

    uint32_t src = ((uint32_t)h->src[0] << 24) | ((uint32_t)h->src[1] << 16)
                 | ((uint32_t)h->src[2] <<  8) |  (uint32_t)h->src[3];

    /* Passively learn sender's MAC */
    arp_update(src, src_mac);

    uint16_t tot   = ntohs(h->total_len);
    size_t   pay_off = ihl;
    size_t   pay_len = (tot > ihl) ? (size_t)(tot - ihl) : 0;
    if (pay_len > len - pay_off)
        pay_len = len - pay_off;

    const void *payload = (const uint8_t *)data + pay_off;

    switch (h->proto) {
    case IP_PROTO_ICMP:
        icmp_input(src, src_mac, payload, pay_len);
        break;
    case IP_PROTO_UDP:
        udp_input(src, src_mac, payload, pay_len);
        break;
    case IP_PROTO_TCP:
        tcp_input(src, src_mac, h, payload, pay_len);
        break;
    }
}
