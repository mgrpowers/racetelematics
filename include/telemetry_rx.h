#ifndef TELEMETRY_RX_H
#define TELEMETRY_RX_H

#include "dashboard.h"

/*
 * Non-blocking UDP receiver for telemetry packets.
 * The dashboard main loop calls telem_rx_init() once, then polls
 * telem_rx_poll() each frame.  Returns 1 when fresh data is written
 * into *out, 0 when nothing new arrived.
 */

int  telem_rx_init(int port);       /* returns 0 on success */
int  telem_rx_poll(telemetry_t *out);
void telem_rx_close(void);

#endif
