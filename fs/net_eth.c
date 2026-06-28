/*
 * Ethernet (MAC) layer.
 *
 * Responsibilities:
 *   - Add/strip the 14-byte Ethernet header.
 *   - Dispatch received frames by EtherType to ARP or IP handlers.
 *   - Enforce the 60-byte minimum frame length on transmit.
 */

#include "net.h"
#include "net_eth.h"
#include "net_arp.h"
#include "net_ip.h"
#include "e1000.h"
#include <inc/string.h>

/* Scratch buffer for outgoing frames (header + payload). */
static uint8_t eth_tx_buf[2048];

void
eth_send(const uint8_t dst[6], uint16_t ethertype,
         const void *payload, size_t payload_len)
{
    struct eth_hdr *h = (struct eth_hdr *)eth_tx_buf;
    memcpy(h->dst, dst, 6);
    memcpy(h->src, net_our_mac, 6);
    h->ethertype = htons(ethertype);

    if (payload_len > sizeof(eth_tx_buf) - sizeof(*h))
        payload_len = sizeof(eth_tx_buf) - sizeof(*h);

    memcpy(eth_tx_buf + sizeof(*h), payload, payload_len);

    size_t total = sizeof(*h) + payload_len;
    if (total < 60) total = 60;  /* pad to Ethernet minimum */

    e1000_send(eth_tx_buf, total);
}

void
eth_input(const uint8_t *frame, size_t len)
{
    if (len < sizeof(struct eth_hdr))
        return;

    const struct eth_hdr *h   = (const struct eth_hdr *)frame;
    const uint8_t        *pay = frame + sizeof(*h);
    size_t                psz = len   - sizeof(*h);
    uint16_t              et  = ntohs(h->ethertype);

    switch (et) {
    case ETHERTYPE_ARP:
        arp_input(h->src, pay, psz);
        break;
    case ETHERTYPE_IPV4:
        ip_input(h->src, pay, psz);
        break;
    /* silently drop everything else */
    }
}
