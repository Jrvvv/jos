#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <inc/types.h>

/*
 * Examine the bytes accumulated in rx_buf[0..rx_len-1].
 * If they form a complete HTTP request (headers end with \r\n\r\n):
 *   - Build and transmit an HTTP/1.0 response via tcp_send_data().
 *   - Close the connection via tcp_close().
 *   - Return 1 (caller should clear the receive buffer).
 * Otherwise return 0 (not enough data yet).
 */
int http_process(uint8_t *rx_buf, size_t rx_len);

#endif /* NET_HTTP_H */
