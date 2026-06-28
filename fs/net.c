/*
 * Network stack entry point.
 *
 * net_serve() is called in a child process (forked from the FS server
 * after all hardware initialisation is complete).  It re-initialises the
 * e1000 NIC to take exclusive DMA ownership, then spins polling for
 * incoming Ethernet frames and dispatching them up the protocol stack.
 *
 * Global network identity:
 *   IP  : 192.168.56.101   (must match QEMU -netdev user,net=...,hostfwd=)
 *   MAC : 52:54:00:12:34:56 (fixed by QEMU -device e1000,mac=...)
 */

#include "net.h"
#include "net_eth.h"
#include "net_tcp.h"
#include "net_udp.h"
#include "e1000.h"
#include <inc/lib.h>

/* Global network identity (read by all protocol layers) */
uint8_t  net_our_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
uint32_t net_our_ip     = NET_OUR_IP;   /* host byte order */

/* Receive scratch buffer (max Ethernet frame) */
static uint8_t net_rx_buf[2048];

void
net_serve(void)
{
    /*
     * Re-initialise the NIC so this child process owns the DMA rings.
     * The parent still has the old DMA physical pages mapped (COW), but
     * the NIC hardware now writes into our newly allocated pages.
     */
    e1000_init();

    tcp_init();

    cprintf("net: stack started  IP=192.168.56.101  MAC=52:54:00:12:34:56\n");
    cprintf("net: HTTP server on port 80\n");
    cprintf("net: UDP echo on port 7 and %d\n", UDP_PORT_TEST);

    while (1) {
        size_t len;
        if (e1000_recv(net_rx_buf, &len) == 0)
            eth_input(net_rx_buf, len);
        else
            sys_yield();   /* no packet — give other environments a turn */
    }
}
