#include "dirt_rally_rx.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <math.h>

/*
 * Dirt Rally / Dirt Rally 2.0 UDP packet (extradata=3):
 *
 *  Index  Field
 *  ─────  ──────────────────────────────
 *    0    Time (total elapsed)
 *    1    Lap time
 *    2    Lap distance
 *    3    Total distance
 *    7    Speed  (m/s)
 *   29    Throttle  (0.0 – 1.0)
 *   31    Brake     (0.0 – 1.0)
 *   33    Gear  (0=N, 1-9 fwd, 10=R)
 *   36    Current lap
 *   37    Engine RPM / 10
 *   60    Total laps
 *   63    Max RPM / 10
 */

enum {
    DR_TIME          =  0,
    DR_LAP_TIME      =  1,
    DR_LAP_DIST      =  2,
    DR_TOTAL_DIST    =  3,
    DR_SPEED         =  7,
    DR_THROTTLE      = 29,
    DR_BRAKE         = 31,
    DR_GEAR          = 33,
    DR_CUR_LAP       = 36,
    DR_RPM_DIV10     = 37,
    DR_TOTAL_LAPS    = 60,
    DR_TRACK_LEN     = 61,
    DR_MAX_RPM_DIV10 = 63,
};

static int rx_sock = -1;

int dirt_rx_init(int port)
{
    rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_sock < 0) return -1;

    int flags = fcntl(rx_sock, F_GETFL, 0);
    fcntl(rx_sock, F_SETFL, flags | O_NONBLOCK);

    int reuse = 1;
    setsockopt(rx_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(rx_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(rx_sock);
        rx_sock = -1;
        return -1;
    }
    return 0;
}

int dirt_rx_poll(telemetry_t *out)
{
    float pkt[DIRT_RALLY_PACKET_FLOATS];
    ssize_t n = recv(rx_sock, pkt, sizeof(pkt), 0);

    /* accept both the 256-byte (extradata=3) and smaller packets */
    if (n < 64)
        return 0;

    int nfloats = (int)(n / sizeof(float));

    float speed_ms = (nfloats > DR_SPEED) ? pkt[DR_SPEED] : 0.0f;
    out->speed_kph = (uint16_t)(fabsf(speed_ms) * 3.6f);

    if (nfloats > DR_MAX_RPM_DIV10) {
        out->rpm     = (uint16_t)(pkt[DR_RPM_DIV10] * 10.0f);
        out->rpm_max = (uint16_t)(pkt[DR_MAX_RPM_DIV10] * 10.0f);
    }
    if (out->rpm_max == 0) out->rpm_max = 8000;
    out->rpm_shift = (uint16_t)(out->rpm_max * 90 / 100);

    if (nfloats > DR_GEAR) {
        int gear_raw = (int)pkt[DR_GEAR];
        if (gear_raw == 10) gear_raw = 0;      /* reverse shown as R/0 */
        out->gear = (uint8_t)gear_raw;
    }

    if (nfloats > DR_THROTTLE)
        out->accel_pct = (uint8_t)(pkt[DR_THROTTLE] * 100.0f);
    if (nfloats > DR_BRAKE)
        out->brake_pct = (uint8_t)(pkt[DR_BRAKE] * 100.0f);

    if (nfloats > DR_CUR_LAP)
        out->lap = (uint8_t)pkt[DR_CUR_LAP];
    if (nfloats > DR_TOTAL_LAPS) {
        int total = (int)pkt[DR_TOTAL_LAPS];
        out->lap_total = (total > 0) ? (uint8_t)total : 1;
    }

    out->position = 0;  /* Dirt Rally doesn't broadcast race position */

    return 1;
}

void dirt_rx_close(void)
{
    if (rx_sock >= 0) {
        close(rx_sock);
        rx_sock = -1;
    }
}
