/*
 * ICMP layer — echo (ping) reply only.
 *
 * On receiving an Echo Request (type 8):
 *   - Copy the identifier, sequence number, and payload verbatim.
 *   - Recompute the checksum.
 *   - Send an Echo Reply (type 0) back to the sender.
 */

#include "net.h"
#include "net_icmp.h"
#include "net_ip.h"
#include <inc/string.h>
#include <inc/lib.h>

/* Max ICMP payload we'll reflect (keeps the buffer bounded) */
#define ICMP_MAX_DATA 512

static uint8_t icmp_tx_buf[sizeof(struct icmp_hdr) + ICMP_MAX_DATA];

void
icmp_input(uint32_t src_ip, const uint8_t src_mac[6],
           const void *data, size_t len)
{
    if (len < sizeof(struct icmp_hdr))
        return;

    const struct icmp_hdr *req = (const struct icmp_hdr *)data;
    if (req->type != ICMP_ECHO_REQUEST)
        return;

    /* Reflect payload (everything after the 8-byte ICMP header) */
    size_t data_len = len - sizeof(struct icmp_hdr);
    if (data_len > ICMP_MAX_DATA)
        data_len = ICMP_MAX_DATA;

    struct icmp_hdr *rep = (struct icmp_hdr *)icmp_tx_buf;
    rep->type     = ICMP_ECHO_REPLY;
    rep->code     = 0;
    rep->checksum = 0;
    rep->id       = req->id;
    rep->seq      = req->seq;
    memcpy(icmp_tx_buf + sizeof(*rep),
           (const uint8_t *)data + sizeof(*req), data_len);

    size_t total    = sizeof(*rep) + data_len;
    rep->checksum   = ip_checksum(icmp_tx_buf, total);

    ip_send(src_ip, src_mac, IP_PROTO_ICMP, icmp_tx_buf, total);

    cprintf("net/icmp: echo reply -> %d.%d.%d.%d  id=%d seq=%d\n",
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >>  8) & 0xFF,  src_ip        & 0xFF,
            ntohs(req->id), ntohs(req->seq));
}
