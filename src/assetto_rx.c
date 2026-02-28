#include "assetto_rx.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum {
    AC_OP_HANDSHAKE = 0,
    AC_OP_SUBSCRIBE_UPDATE = 1,
};

typedef struct {
    int32_t identifier;
    int32_t version;
    int32_t operation_id;
} ac_handshake_t;

static int rx_sock = -1;
static struct sockaddr_in ac_addr;
static int handshake_sent = 0;
static int subscribed = 0;

static int send_handshake(int op)
{
    ac_handshake_t hs;
    hs.identifier = 1;
    hs.version = 1;
    hs.operation_id = op;
    ssize_t n = sendto(rx_sock, &hs, sizeof(hs), 0,
                       (struct sockaddr *)&ac_addr, sizeof(ac_addr));
    return (n == (ssize_t)sizeof(hs)) ? 0 : -1;
}

static int32_t read_i32_le(const uint8_t *p)
{
    return (int32_t)(
        ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24)
    );
}

static float read_f32_le(const uint8_t *p)
{
    float v = 0.0f;
    memcpy(&v, p, sizeof(v));
    return v;
}

int assetto_rx_init(const char *host, int port)
{
    rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_sock < 0) return -1;

    int flags = fcntl(rx_sock, F_GETFL, 0);
    fcntl(rx_sock, F_SETFL, flags | O_NONBLOCK);

    memset(&ac_addr, 0, sizeof(ac_addr));
    ac_addr.sin_family = AF_INET;
    ac_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &ac_addr.sin_addr) != 1) {
        close(rx_sock);
        rx_sock = -1;
        return -1;
    }

    handshake_sent = 0;
    subscribed = 0;

    if (send_handshake(AC_OP_HANDSHAKE) == 0) {
        handshake_sent = 1;
    }
    return 0;
}

int assetto_rx_poll(telemetry_t *out)
{
    uint8_t buf[1024];
    ssize_t n = recv(rx_sock, buf, sizeof(buf), 0);
    if (n <= 0) {
        return 0;
    }

    /* Handshake response is 208 bytes in AC docs. */
    if (!subscribed && n >= 208) {
        /* Confirm subscription after first handshake response packet. */
        if (send_handshake(AC_OP_SUBSCRIBE_UPDATE) == 0) {
            subscribed = 1;
        }
        return 0;
    }

    /*
     * RTCarInfo expected prefix:
     *  char identifier ('a')
     *  int  size
     *  float speed_kmh, speed_mph, speed_ms
     *  bool abs_enabled, abs_in_action, tc_in_action, tc_enabled, in_pit, limiter
     *  ...
     *  float gas, brake, clutch, engineRPM, steer
     *  int gear
     */
    if (n < 90) return 0;
    if (buf[0] != 'a') return 0;

    int32_t size = read_i32_le(&buf[1]);
    (void)size;

    float speed_kmh = read_f32_le(&buf[5]);
    uint8_t abs_in_action = buf[18];
    uint8_t tc_in_action = buf[19];

    /* Offsets derived from documented RTCarInfo order. */
    float gas = read_f32_le(&buf[42]);
    float brake = read_f32_le(&buf[46]);
    float rpm = read_f32_le(&buf[54]);
    int32_t gear = read_i32_le(&buf[62]);

    if (speed_kmh < 0.0f) speed_kmh = 0.0f;
    if (gas < 0.0f) gas = 0.0f;
    if (gas > 1.0f) gas = 1.0f;
    if (brake < 0.0f) brake = 0.0f;
    if (brake > 1.0f) brake = 1.0f;
    if (rpm < 0.0f) rpm = 0.0f;

    out->speed_kph = (uint16_t)(speed_kmh + 0.5f);
    out->rpm = (uint16_t)(rpm + 0.5f);
    if (out->rpm_max == 0) out->rpm_max = 9000;
    out->rpm_shift = (uint16_t)(out->rpm_max * 90 / 100);
    out->accel_pct = (uint8_t)(gas * 100.0f + 0.5f);
    out->brake_pct = (uint8_t)(brake * 100.0f + 0.5f);
    out->abs_active = abs_in_action ? 1 : 0;
    out->tc_active = tc_in_action ? 1 : 0;

    if (gear < 0) out->gear = 0;         /* reverse */
    else if (gear == 0) out->gear = 0;   /* neutral */
    else if (gear > 8) out->gear = 8;
    else out->gear = (uint8_t)gear;

    out->position = 0;
    if (out->lap_total == 0) out->lap_total = 1;
    return 1;
}

void assetto_rx_close(void)
{
    if (rx_sock >= 0) {
        close(rx_sock);
        rx_sock = -1;
    }
    handshake_sent = 0;
    subscribed = 0;
}
