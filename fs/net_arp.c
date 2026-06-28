/*
 * ARP layer.
 *
 * Responsibilities:
 *   - Maintain a small ARP table (IP → MAC) that is populated from
 *     incoming ARP requests and IP packets (passive learning).
 *   - Reply to ARP requests targeting our IP address.
 *
 * Sending ARP requests is not needed: remote hosts learn our MAC from
 * our ARP replies and from the source MAC in our IP packets.
 */

#include "net.h"
#include "net_arp.h"
#include "net_eth.h"
#include <inc/string.h>
#include <inc/lib.h>

/* -----------------------------------------------------------------------
 * ARP table
 * ----------------------------------------------------------------------- */
struct arp_entry {
    uint32_t ip;      /* host byte order */
    uint8_t  mac[6];
    int      valid;
};

static struct arp_entry arp_table[ARP_TABLE_SIZE];
static int arp_count;

void
arp_update(uint32_t ip, const uint8_t mac[6])
{
    for (int i = 0; i < arp_count; i++) {
        if (arp_table[i].ip == ip) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    if (arp_count < ARP_TABLE_SIZE) {
        arp_table[arp_count].ip = ip;
        memcpy(arp_table[arp_count].mac, mac, 6);
        arp_table[arp_count].valid = 1;
        arp_count++;
    }
}

int
arp_lookup(uint32_t ip, uint8_t mac_out[6])
{
    for (int i = 0; i < arp_count; i++) {
        if (arp_table[i].ip == ip) {
            memcpy(mac_out, arp_table[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * ARP input
 * ----------------------------------------------------------------------- */
void
arp_input(const uint8_t src_mac[6], const void *data, size_t len)
{
    if (len < sizeof(struct arp_pkt))
        return;

    const struct arp_pkt *p = (const struct arp_pkt *)data;

    if (ntohs(p->htype) != 1)      return;  /* not Ethernet */
    if (ntohs(p->ptype) != 0x0800) return;  /* not IPv4 */
    if (ntohs(p->oper)  != ARP_OP_REQUEST) return;

    /* Is someone asking for our MAC? */
    uint32_t tpa = ((uint32_t)p->tpa[0] << 24) | ((uint32_t)p->tpa[1] << 16)
                 | ((uint32_t)p->tpa[2] <<  8) |  (uint32_t)p->tpa[3];
    if (tpa != net_our_ip) return;

    /* Learn sender's IP → MAC before replying */
    uint32_t spa = ((uint32_t)p->spa[0] << 24) | ((uint32_t)p->spa[1] << 16)
                 | ((uint32_t)p->spa[2] <<  8) |  (uint32_t)p->spa[3];
    arp_update(spa, p->sha);

    /* Build ARP reply */
    struct arp_pkt reply;
    reply.htype = htons(1);
    reply.ptype = htons(0x0800);
    reply.hlen  = 6;
    reply.plen  = 4;
    reply.oper  = htons(ARP_OP_REPLY);

    memcpy(reply.sha, net_our_mac, 6);
    reply.spa[0] = (net_our_ip >> 24) & 0xFF;
    reply.spa[1] = (net_our_ip >> 16) & 0xFF;
    reply.spa[2] = (net_our_ip >>  8) & 0xFF;
    reply.spa[3] =  net_our_ip        & 0xFF;

    memcpy(reply.tha, p->sha, 6);
    memcpy(reply.tpa, p->spa, 4);

    eth_send(p->sha, ETHERTYPE_ARP, &reply, sizeof(reply));

    cprintf("net/arp: reply -> %d.%d.%d.%d  MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            p->spa[0], p->spa[1], p->spa[2], p->spa[3],
            p->sha[0], p->sha[1], p->sha[2],
            p->sha[3], p->sha[4], p->sha[5]);
}
