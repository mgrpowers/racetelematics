#ifndef ASSETTO_RX_H
#define ASSETTO_RX_H

#include "dashboard.h"

/*
 * Assetto Corsa Remote Telemetry receiver (UDP).
 *
 * Protocol:
 * - Handshake op=0
 * - Subscribe updates op=1
 * - Port default 9996
 *
 * Receiver is non-blocking; poll each frame.
 */

#define ASSETTO_PORT 9996

int  assetto_rx_init(const char *host, int port); /* returns 0 on success */
int  assetto_rx_poll(telemetry_t *out);           /* returns 1 on fresh data */
void assetto_rx_close(void);

#endif
