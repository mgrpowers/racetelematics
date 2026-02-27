#include "telemetry_rx.h"
#include "telemetry_proto.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static int rx_sock = -1;

int telem_rx_init(int port)
{
    rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_sock < 0) return -1;

    /* non-blocking so the render loop never stalls */
    int flags = fcntl(rx_sock, F_GETFL, 0);
    fcntl(rx_sock, F_SETFL, flags | O_NONBLOCK);

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

int telem_rx_poll(telemetry_t *out)
{
    telem_packet_t pkt;
    ssize_t n = recv(rx_sock, &pkt, sizeof(pkt), 0);
    if (n < (ssize_t)sizeof(pkt))
        return 0;

    if (pkt.magic != TELEM_MAGIC || pkt.version != TELEM_VERSION)
        return 0;

    out->speed_kph  = pkt.speed_kph;
    out->rpm        = pkt.rpm;
    out->rpm_max    = pkt.rpm_max;
    out->rpm_shift  = pkt.rpm_shift;
    out->brake_pct  = pkt.brake_pct;
    out->accel_pct  = pkt.accel_pct;
    out->gear       = pkt.gear;
    out->rumble     = pkt.rumble;
    out->abs_active = pkt.abs_active;
    out->tc_active  = pkt.tc_active;
    out->surface    = pkt.surface;
    out->shift_light = pkt.shift_light;
    return 1;
}

void telem_rx_close(void)
{
    if (rx_sock >= 0) {
        close(rx_sock);
        rx_sock = -1;
    }
}
