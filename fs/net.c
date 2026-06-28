/*
 * Network stack — runs inside the FS server process (ENV_TYPE_FS).
 *
 * Architecture:
 *   The FS server is the only user-space process allowed to call
 *   sys_map_physical_region(), so the network stack lives here alongside
 *   the e1000 driver.
 *
 *   net_init()  — called once in umain(), after e1000_init().
 *   net_poll()  — called at the top of every serve() loop iteration to
 *                 drain the NIC RX ring before blocking on ipc_recv().
 *
 * Between IPC requests the NIC buffers up to 16 frames in hardware;
 * they are drained when the next IPC wakes the serve loop.
 *
 * Network identity (fixed for QEMU):
 *   IP  : 192.168.56.101
 *   MAC : 52:54:00:12:34:56
 */

#include "net.h"
#include "net_eth.h"
#include "net_tcp.h"
#include "net_udp.h"
#include "e1000.h"
#include <inc/lib.h>

/* Global network identity (read by all protocol layers) */
uint8_t  net_our_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
uint32_t net_our_ip     = NET_OUR_IP;

static uint8_t net_rx_buf[2048];

void
net_init(void)
{
    tcp_init();
    cprintf("net: stack ready  IP=192.168.56.101  MAC=52:54:00:12:34:56\n");
    cprintf("net: HTTP on port 80, UDP echo on ports 7 and %d\n", UDP_PORT_TEST);
}

void
net_poll(void)
{
    /* Drain all pending frames from the NIC RX ring */
    size_t len;
    while (e1000_recv(net_rx_buf, &len) == 0)
        eth_input(net_rx_buf, len);
}
