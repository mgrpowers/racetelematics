#ifndef DIRT_RALLY_RX_H
#define DIRT_RALLY_RX_H

#include "dashboard.h"

/*
 * Dirt Rally UDP telemetry receiver.
 *
 * The game broadcasts 64 floats (256 bytes) to UDP port 20777.
 * Enable in hardware_settings_config.xml:
 *   <udp enabled="true" extradata="3" ip="127.0.0.1" port="20777" delay="1" />
 *
 * macOS path (Steam):
 *   ~/Library/Application Support/Feral Interactive/DiRT Rally/VFS/User/
 *   AppData/Roaming/My Games/DiRT Rally/hardwaresettings/
 *   hardware_settings_config.xml
 */

#define DIRT_RALLY_PORT  20777
#define DIRT_RALLY_PACKET_FLOATS 64
#define DIRT_RALLY_PACKET_SIZE   (DIRT_RALLY_PACKET_FLOATS * sizeof(float))

int  dirt_rx_init(int port);
int  dirt_rx_poll(telemetry_t *out);
void dirt_rx_close(void);

#endif
