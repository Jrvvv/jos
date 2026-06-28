/*
 * HTTP/1.0 server — serves a single static page.
 *
 * Called after TCP has buffered a complete request.  We parse only the
 * method (GET / HEAD); everything else is ignored.  The response is sent
 * via tcp_send_data() and the connection is closed with tcp_close().
 */

#include "net.h"
#include "net_http.h"
#include "net_tcp.h"
#include <inc/string.h>
#include <inc/stdio.h>
#include <inc/lib.h>

/* Fixed HTML page returned for any GET request */
static const char http_body[] =
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "<head><title>JOS Network Stack</title></head>\r\n"
    "<body>\r\n"
    "<h1>JOS Network Stack</h1>\r\n"
    "<p>Running on the JOS teaching OS with a hand-written TCP/IP stack.</p>\r\n"
    "<h2>Supported protocols</h2>\r\n"
    "<ul>\r\n"
    "  <li><b>Ethernet</b> &mdash; frame parsing, ARP dispatch, IP dispatch</li>\r\n"
    "  <li><b>ARP</b> &mdash; responds to requests for 192.168.56.101</li>\r\n"
    "  <li><b>ICMP</b> &mdash; echo (ping) reply</li>\r\n"
    "  <li><b>UDP</b> &mdash; echo server on port 7 and 10001</li>\r\n"
    "  <li><b>TCP</b> &mdash; this HTTP server on port 80</li>\r\n"
    "</ul>\r\n"
    "<p><em>IP: 192.168.56.101 &nbsp; MAC: 52:54:00:12:34:56</em></p>\r\n"
    "</body>\r\n"
    "</html>\r\n";

/* Response header buffer */
static char http_hdr_buf[256];

/* Search for a 4-byte pattern in a byte buffer (replacement for strstr) */
static int
has_end_of_headers(const uint8_t *buf, size_t len)
{
    /* Look for \r\n\r\n */
    if (len >= 4) {
        for (size_t i = 0; i <= len - 4; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' &&
                buf[i+2] == '\r' && buf[i+3] == '\n')
                return 1;
        }
    }
    /* Also accept \n\n (some clients) */
    if (len >= 2) {
        for (size_t i = 0; i <= len - 2; i++) {
            if (buf[i] == '\n' && buf[i+1] == '\n')
                return 1;
        }
    }
    return 0;
}

int
http_process(uint8_t *rx_buf, size_t rx_len)
{
    if (!has_end_of_headers(rx_buf, rx_len))
        return 0;  /* headers not complete yet */

    int is_get  = (rx_len >= 4 && strncmp((char *)rx_buf, "GET ",  4) == 0);
    int is_head = (rx_len >= 5 && strncmp((char *)rx_buf, "HEAD ", 5) == 0);

    if (!is_get && !is_head) {
        const char *bad =
            "HTTP/1.0 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        tcp_send_data(bad, strlen(bad));
        tcp_close();
        return 1;
    }

    cprintf("net/http: %s request received\n", is_get ? "GET" : "HEAD");

    size_t body_len = strlen(http_body);
    int hdr_len = snprintf(http_hdr_buf, sizeof(http_hdr_buf),
                           "HTTP/1.0 200 OK\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           body_len);
    if (hdr_len > 0)
        tcp_send_data(http_hdr_buf, (size_t)hdr_len);
    if (is_get)
        tcp_send_data(http_body, body_len);
    tcp_close();

    cprintf("net/http: response sent (%d hdr + %zu body bytes)\n",
            hdr_len, is_get ? body_len : (size_t)0);
    return 1;
}
