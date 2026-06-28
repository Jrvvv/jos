/*
 * TCP layer — single-connection server (port 80, HTTP).
 *
 * State machine (simplified for one active connection):
 *
 *   LISTEN       waiting for SYN
 *   SYN_RCVD     SYN received, SYN-ACK sent
 *   ESTABLISHED  3-way handshake complete; data flows
 *   FIN_WAIT_1   we sent FIN, waiting for ACK
 *   FIN_WAIT_2   our FIN ACK'd, waiting for remote FIN
 *   LAST_ACK     received FIN (passive close), sent our FIN, waiting for ACK
 *
 * HTTP processing: once the receive buffer contains a complete HTTP
 * request (ends with \r\n\r\n), http_process() is called.  It builds
 * the HTTP response, calls tcp_send_data() to transmit it, then calls
 * tcp_close() to begin the active-close sequence.
 */

#include "net.h"
#include "net_tcp.h"
#include "net_ip.h"
#include "net_eth.h"
#include "net_http.h"
#include <inc/string.h>
#include <inc/lib.h>

/* -----------------------------------------------------------------------
 * Connection state
 * ----------------------------------------------------------------------- */
enum tcp_state {
    TCP_LISTEN,
    TCP_SYN_RCVD,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_LAST_ACK,
};

#define TCP_RX_BUF 1400

struct tcp_conn {
    enum tcp_state state;
    uint32_t rem_ip;
    uint8_t  rem_mac[6];
    uint16_t rem_port;
    uint32_t snd_nxt;   /* our next sequence number to send */
    uint32_t rcv_nxt;   /* next byte we expect from remote */
    uint8_t  rx_buf[TCP_RX_BUF + 1]; /* +1 for null terminator */
    size_t   rx_len;
};

static struct tcp_conn conn;
static uint32_t isn = 0xC0DE0000;   /* initial sequence number */

/* -----------------------------------------------------------------------
 * TCP checksum (with IPv4 pseudo-header)
 *
 * Pseudo-header layout (12 bytes in network byte order):
 *   src IP (4)  dst IP (4)  zero (1)  proto=6 (1)  TCP length (2)
 * ----------------------------------------------------------------------- */
static uint16_t
tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
             const void *seg, size_t seg_len)
{
    uint8_t pseudo[12];
    pseudo[0]  = (src_ip >> 24) & 0xFF;
    pseudo[1]  = (src_ip >> 16) & 0xFF;
    pseudo[2]  = (src_ip >>  8) & 0xFF;
    pseudo[3]  =  src_ip        & 0xFF;
    pseudo[4]  = (dst_ip >> 24) & 0xFF;
    pseudo[5]  = (dst_ip >> 16) & 0xFF;
    pseudo[6]  = (dst_ip >>  8) & 0xFF;
    pseudo[7]  =  dst_ip        & 0xFF;
    pseudo[8]  = 0;
    pseudo[9]  = IP_PROTO_TCP;
    pseudo[10] = (seg_len >> 8) & 0xFF;
    pseudo[11] =  seg_len       & 0xFF;

    uint32_t sum = 0;
    const uint16_t *p;

    p = (const uint16_t *)pseudo;
    for (int i = 0; i < 6; i++) sum += p[i];

    p = (const uint16_t *)seg;
    size_t n = seg_len;
    while (n > 1) { sum += *p++; n -= 2; }
    if (n)         sum += *(const uint8_t *)p;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* -----------------------------------------------------------------------
 * Send a TCP segment
 * ----------------------------------------------------------------------- */
static uint8_t tcp_tx_buf[sizeof(struct tcp_hdr) + 1460];

static void
tcp_send_seg(uint32_t dst_ip, const uint8_t dst_mac[6],
             uint16_t src_port, uint16_t dst_port,
             uint32_t seq, uint32_t ack_num,
             uint8_t flags,
             const void *payload, size_t payload_len)
{
    if (payload_len > sizeof(tcp_tx_buf) - sizeof(struct tcp_hdr))
        payload_len = sizeof(tcp_tx_buf) - sizeof(struct tcp_hdr);

    struct tcp_hdr *h = (struct tcp_hdr *)tcp_tx_buf;
    h->src_port = htons(src_port);
    h->dst_port = htons(dst_port);
    h->seq      = htonl(seq);
    h->ack      = htonl(ack_num);
    h->data_off = (5 << 4);  /* 20-byte header, no options */
    h->flags    = flags;
    h->window   = htons(4096);
    h->checksum = 0;
    h->urgent   = 0;

    if (payload && payload_len)
        memcpy(tcp_tx_buf + sizeof(*h), payload, payload_len);

    size_t total = sizeof(*h) + payload_len;
    h->checksum  = tcp_checksum(net_our_ip, dst_ip, tcp_tx_buf, total);

    ip_send(dst_ip, dst_mac, IP_PROTO_TCP, tcp_tx_buf, total);
}

/* -----------------------------------------------------------------------
 * Public: send data on the established connection
 * ----------------------------------------------------------------------- */
void
tcp_send_data(const void *data, size_t len)
{
    if (conn.state != TCP_ESTABLISHED && conn.state != TCP_LAST_ACK)
        return;

    /* Fragment if needed (1 MSS = 1460 bytes) */
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        size_t chunk = (len > 1460) ? 1460 : len;
        tcp_send_seg(conn.rem_ip, conn.rem_mac,
                     TCP_HTTP_PORT, conn.rem_port,
                     conn.snd_nxt, conn.rcv_nxt,
                     TCP_ACK | TCP_PSH, p, chunk);
        conn.snd_nxt += (uint32_t)chunk;
        p   += chunk;
        len -= chunk;
    }
}

/* -----------------------------------------------------------------------
 * Public: active close (send FIN)
 * ----------------------------------------------------------------------- */
void
tcp_close(void)
{
    if (conn.state != TCP_ESTABLISHED && conn.state != TCP_LAST_ACK)
        return;
    tcp_send_seg(conn.rem_ip, conn.rem_mac,
                 TCP_HTTP_PORT, conn.rem_port,
                 conn.snd_nxt, conn.rcv_nxt,
                 TCP_FIN | TCP_ACK, NULL, 0);
    conn.snd_nxt++;
    conn.state = TCP_FIN_WAIT_1;
}

/* -----------------------------------------------------------------------
 * Public: initialise (set to LISTEN)
 * ----------------------------------------------------------------------- */
void
tcp_init(void)
{
    memset(&conn, 0, sizeof(conn));
    conn.state = TCP_LISTEN;
}

/* -----------------------------------------------------------------------
 * tcp_input — main state machine
 * ----------------------------------------------------------------------- */
void
tcp_input(uint32_t src_ip, const uint8_t src_mac[6],
          const struct ip_hdr *iphdr, const void *data, size_t len)
{
    if (len < sizeof(struct tcp_hdr))
        return;

    const struct tcp_hdr *h = (const struct tcp_hdr *)data;
    uint16_t dst_port   = ntohs(h->dst_port);
    uint16_t src_port   = ntohs(h->src_port);
    uint8_t  flags      = h->flags;
    uint32_t seq        = ntohl(h->seq);
    uint32_t ack_num    = ntohl(h->ack);
    uint8_t  hlen       = (h->data_off >> 4) * 4;
    const uint8_t *pay  = (const uint8_t *)data + hlen;
    size_t   pay_len    = (len > hlen) ? len - hlen : 0;

    (void)ack_num;  /* used only in FIN_WAIT_1 path */

    /* We only serve port 80 */
    if (dst_port != TCP_HTTP_PORT)
        return;

    switch (conn.state) {

    /* ------------------------------------------------------------------ */
    case TCP_LISTEN:
        if (!(flags & TCP_SYN)) return;

        conn.rem_ip   = src_ip;
        conn.rem_port = src_port;
        memcpy(conn.rem_mac, src_mac, 6);
        conn.rcv_nxt  = seq + 1;
        conn.snd_nxt  = isn;
        conn.rx_len   = 0;

        tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                     conn.snd_nxt, conn.rcv_nxt,
                     TCP_SYN | TCP_ACK, NULL, 0);
        conn.snd_nxt++;
        conn.state = TCP_SYN_RCVD;

        cprintf("net/tcp: SYN from %d.%d.%d.%d:%d\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >>  8) & 0xFF,  src_ip        & 0xFF,
                src_port);
        break;

    /* ------------------------------------------------------------------ */
    case TCP_SYN_RCVD:
        /* Wrong peer — ignore */
        if (src_ip != conn.rem_ip || src_port != conn.rem_port) return;

        if (flags & TCP_RST) { conn.state = TCP_LISTEN; return; }
        if (!(flags & TCP_ACK)) return;

        conn.state = TCP_ESTABLISHED;
        cprintf("net/tcp: ESTABLISHED  %d.%d.%d.%d:%d\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >>  8) & 0xFF,  src_ip        & 0xFF,
                src_port);
        /* Fall through: the ACK may carry data too */
        /* FALL THROUGH */

    /* ------------------------------------------------------------------ */
    case TCP_ESTABLISHED:
        if (src_ip != conn.rem_ip || src_port != conn.rem_port) return;

        if (flags & TCP_RST) {
            cprintf("net/tcp: RST received, back to LISTEN\n");
            conn.state  = TCP_LISTEN;
            conn.rx_len = 0;
            return;
        }

        /* Buffer incoming data */
        if (pay_len > 0) {
            size_t space = TCP_RX_BUF - conn.rx_len;
            if (pay_len > space) pay_len = space;
            memcpy(conn.rx_buf + conn.rx_len, pay, pay_len);
            conn.rx_len  += pay_len;
            conn.rcv_nxt  = seq + (uint32_t)pay_len;

            /* ACK the received data */
            tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                         conn.snd_nxt, conn.rcv_nxt,
                         TCP_ACK, NULL, 0);

            /* Process if we have a complete HTTP request */
            if (http_process(conn.rx_buf, conn.rx_len))
                conn.rx_len = 0;  /* consumed */
        }

        /* Handle FIN (check conn.state: http_process may have moved us) */
        if ((flags & TCP_FIN) && conn.state == TCP_ESTABLISHED) {
            conn.rcv_nxt++;
            /* Passive close: ACK their FIN, send our FIN */
            tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                         conn.snd_nxt, conn.rcv_nxt,
                         TCP_ACK, NULL, 0);
            tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                         conn.snd_nxt, conn.rcv_nxt,
                         TCP_FIN | TCP_ACK, NULL, 0);
            conn.snd_nxt++;
            conn.state = TCP_LAST_ACK;
        }
        break;

    /* ------------------------------------------------------------------ */
    case TCP_FIN_WAIT_1:
        /* Wrong peer */
        if (src_ip != conn.rem_ip || src_port != conn.rem_port) return;

        if (flags & TCP_ACK)
            conn.state = TCP_FIN_WAIT_2;

        if (flags & TCP_FIN) {
            conn.rcv_nxt++;
            tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                         conn.snd_nxt, conn.rcv_nxt,
                         TCP_ACK, NULL, 0);
            conn.state  = TCP_LISTEN;
            conn.rx_len = 0;
            cprintf("net/tcp: connection closed (FIN_WAIT_1)\n");
        }
        break;

    /* ------------------------------------------------------------------ */
    case TCP_FIN_WAIT_2:
        if (src_ip != conn.rem_ip || src_port != conn.rem_port) return;

        if (flags & TCP_FIN) {
            conn.rcv_nxt++;
            tcp_send_seg(src_ip, src_mac, TCP_HTTP_PORT, src_port,
                         conn.snd_nxt, conn.rcv_nxt,
                         TCP_ACK, NULL, 0);
            conn.state  = TCP_LISTEN;
            conn.rx_len = 0;
            cprintf("net/tcp: connection closed (FIN_WAIT_2)\n");
        }
        break;

    /* ------------------------------------------------------------------ */
    case TCP_LAST_ACK:
        if (src_ip != conn.rem_ip || src_port != conn.rem_port) return;

        if (flags & TCP_ACK) {
            conn.state  = TCP_LISTEN;
            conn.rx_len = 0;
            cprintf("net/tcp: connection closed (LAST_ACK)\n");
        }
        break;
    }
}
