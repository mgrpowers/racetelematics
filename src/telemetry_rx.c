#include "telemetry_rx.h"
#include "telemetry_proto.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSESOCK closesocket
#define INVALID_SOCK INVALID_SOCKET
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
typedef int socket_t;
#define CLOSESOCK close
#define INVALID_SOCK (-1)
#endif

static socket_t rx_sock = INVALID_SOCK;

static int net_init(void)
{
#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        started = 1;
    }
#endif
    return 0;
}

static int set_nonblocking(socket_t s)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    return fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

int telem_rx_init(int port)
{
    if (net_init() != 0) return -1;

    rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_sock == INVALID_SOCK) return -1;

    /* non-blocking so the render loop never stalls */
    if (set_nonblocking(rx_sock) != 0) {
        CLOSESOCK(rx_sock);
        rx_sock = INVALID_SOCK;
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(rx_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSESOCK(rx_sock);
        rx_sock = INVALID_SOCK;
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
    if (rx_sock != INVALID_SOCK) {
        CLOSESOCK(rx_sock);
        rx_sock = INVALID_SOCK;
    }
}
