#include "telemetry_proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SERIAL_BAUD 115200
#define DEFAULT_SEND_HZ 20
#define TELEMETRY_STALE_MS 500

typedef struct {
    const char *serial_path;
    int udp_port;
    int baud;
    int axis_instance;
    int send_hz;
    int verbose;
    int dry_run;
} bridge_cfg_t;

typedef struct {
    int power;
    int idle_spring;
    int damper;
    int friction;
    int inertia;
    int fx_spring;
    int fx_damper;
    int fx_friction;
    int fx_inertia;
} bridge_out_t;

typedef struct {
    bridge_out_t last_sent;
    int have_last;
    uint64_t last_send_ms;
} bridge_state_t;

static volatile int g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int open_udp_rx(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static speed_t baud_to_termios(int baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
    default: return B115200;
    }
}

static int open_serial_port(const char *path, int baud)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    speed_t s = baud_to_termios(baud);
    cfsetispeed(&tio, s);
    cfsetospeed(&tio, s);
    tio.c_cflag |= (CLOCAL | CREAD);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void map_telemetry_to_controls(const telem_packet_t *p, bridge_out_t *out)
{
    int rumble_pct = (int)p->rumble * 100 / 255;
    int shift_pct = (int)p->shift_light * 100 / 255;

    int base_strength = 6500 + (int)p->brake_pct * 35 + (int)p->accel_pct * 20;
    base_strength += (int)p->abs_active * 500;
    base_strength += (int)p->tc_active * 300;
    out->power = clampi(base_strength, 3500, 14000);

    out->idle_spring = clampi(80 - (int)p->speed_kph / 6, 15, 100);
    out->damper = clampi(20 + (int)p->brake_pct / 2 + (int)p->speed_kph / 25, 0, 100);
    out->friction = clampi(rumble_pct / 2 + ((int)p->surface == SURFACE_KERB ? 30 : 0), 0, 100);
    out->inertia = clampi((int)p->abs_active * 25 + (int)p->tc_active * 20 + (int)p->brake_pct / 6, 0, 100);

    out->fx_spring = clampi(40 + shift_pct / 4, 0, 100);
    out->fx_damper = clampi(20 + (int)p->brake_pct / 2, 0, 100);
    out->fx_friction = clampi(30 + rumble_pct / 3, 0, 100);
    out->fx_inertia = clampi(20 + (int)p->accel_pct / 3, 0, 100);
}

static int write_cmd(int fd, int dry_run, const char *cmd, int verbose)
{
    if (verbose || dry_run) {
        printf("TX: %s\n", cmd);
    }
    if (dry_run) return 0;

    size_t len = strlen(cmd);
    ssize_t n = write(fd, cmd, len);
    return (n == (ssize_t)len) ? 0 : -1;
}

static int maybe_send_value(int fd, int dry_run, int axis, const char *name, int val)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "axis.%d.%s=%d;\n", axis, name, val);
    return write_cmd(fd, dry_run, buf, 0);
}

static int maybe_send_fx(int fd, int dry_run, const char *name, int val)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "fx.%s=%d;\n", name, val);
    return write_cmd(fd, dry_run, buf, 0);
}

static void send_updates_if_needed(int fd, const bridge_cfg_t *cfg, bridge_state_t *st, const bridge_out_t *cur)
{
    const int delta_power = 200;
    const int delta_pct = 3;

    if (!st->have_last || abs(cur->power - st->last_sent.power) >= delta_power) {
        maybe_send_value(fd, cfg->dry_run, cfg->axis_instance, "power", cur->power);
        st->last_sent.power = cur->power;
    }
    if (!st->have_last || abs(cur->idle_spring - st->last_sent.idle_spring) >= delta_pct) {
        maybe_send_value(fd, cfg->dry_run, cfg->axis_instance, "idlespring", cur->idle_spring);
        st->last_sent.idle_spring = cur->idle_spring;
    }
    if (!st->have_last || abs(cur->damper - st->last_sent.damper) >= delta_pct) {
        maybe_send_value(fd, cfg->dry_run, cfg->axis_instance, "axisdamper", cur->damper);
        st->last_sent.damper = cur->damper;
    }
    if (!st->have_last || abs(cur->friction - st->last_sent.friction) >= delta_pct) {
        maybe_send_value(fd, cfg->dry_run, cfg->axis_instance, "axisfriction", cur->friction);
        st->last_sent.friction = cur->friction;
    }
    if (!st->have_last || abs(cur->inertia - st->last_sent.inertia) >= delta_pct) {
        maybe_send_value(fd, cfg->dry_run, cfg->axis_instance, "axisinertia", cur->inertia);
        st->last_sent.inertia = cur->inertia;
    }

    if (!st->have_last || abs(cur->fx_spring - st->last_sent.fx_spring) >= delta_pct) {
        maybe_send_fx(fd, cfg->dry_run, "spring", cur->fx_spring);
        st->last_sent.fx_spring = cur->fx_spring;
    }
    if (!st->have_last || abs(cur->fx_damper - st->last_sent.fx_damper) >= delta_pct) {
        maybe_send_fx(fd, cfg->dry_run, "damper", cur->fx_damper);
        st->last_sent.fx_damper = cur->fx_damper;
    }
    if (!st->have_last || abs(cur->fx_friction - st->last_sent.fx_friction) >= delta_pct) {
        maybe_send_fx(fd, cfg->dry_run, "friction", cur->fx_friction);
        st->last_sent.fx_friction = cur->fx_friction;
    }
    if (!st->have_last || abs(cur->fx_inertia - st->last_sent.fx_inertia) >= delta_pct) {
        maybe_send_fx(fd, cfg->dry_run, "inertia", cur->fx_inertia);
        st->last_sent.fx_inertia = cur->fx_inertia;
    }

    st->have_last = 1;
}

static void usage(const char *argv0)
{
    printf("Usage: %s --serial <tty> [options]\n", argv0);
    printf("Options:\n");
    printf("  --serial <path>   Serial device path (required unless --dry-run)\n");
    printf("  --udp-port <n>    UDP telemetry port (default: %d)\n", TELEM_UDP_PORT);
    printf("  --baud <n>        Serial baud (default: %d)\n", DEFAULT_SERIAL_BAUD);
    printf("  --axis <n>        FFBoard axis instance (default: 0)\n");
    printf("  --hz <n>          Max command send rate (default: %d)\n", DEFAULT_SEND_HZ);
    printf("  --verbose         Log telemetry and tx activity\n");
    printf("  --dry-run         Do not write serial, print only\n");
}

int main(int argc, char **argv)
{
    bridge_cfg_t cfg = {
        .serial_path = NULL,
        .udp_port = TELEM_UDP_PORT,
        .baud = DEFAULT_SERIAL_BAUD,
        .axis_instance = 0,
        .send_hz = DEFAULT_SEND_HZ,
        .verbose = 0,
        .dry_run = 0
    };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--serial") && i + 1 < argc) cfg.serial_path = argv[++i];
        else if (!strcmp(argv[i], "--udp-port") && i + 1 < argc) cfg.udp_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--baud") && i + 1 < argc) cfg.baud = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--axis") && i + 1 < argc) cfg.axis_instance = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hz") && i + 1 < argc) cfg.send_hz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) cfg.verbose = 1;
        else if (!strcmp(argv[i], "--dry-run")) cfg.dry_run = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!cfg.serial_path && !cfg.dry_run) {
        fprintf(stderr, "error: --serial is required unless --dry-run\n");
        return 1;
    }

    if (cfg.send_hz < 1) cfg.send_hz = 1;
    if (cfg.send_hz > 100) cfg.send_hz = 100;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int udp_fd = open_udp_rx(cfg.udp_port);
    if (udp_fd < 0) {
        perror("open udp");
        return 1;
    }

    int serial_fd = -1;
    if (!cfg.dry_run) {
        serial_fd = open_serial_port(cfg.serial_path, cfg.baud);
        if (serial_fd < 0) {
            perror("open serial");
            close(udp_fd);
            return 1;
        }
    }

    printf("racepad_bridge\n");
    printf("  telemetry UDP : %d\n", cfg.udp_port);
    printf("  serial        : %s\n", cfg.dry_run ? "(dry-run)" : cfg.serial_path);
    printf("  axis instance : %d\n", cfg.axis_instance);
    printf("  send rate     : %d Hz\n", cfg.send_hz);
    printf("  Ctrl-C to stop\n\n");

    telem_packet_t latest = {0};
    int have_latest = 0;
    uint64_t latest_rx_ms = 0;
    bridge_state_t state;
    memset(&state, 0, sizeof(state));

    uint64_t period_ms = 1000ULL / (uint64_t)cfg.send_hz;

    while (g_running) {
        telem_packet_t pkt;
        ssize_t n = recv(udp_fd, &pkt, sizeof(pkt), 0);
        if (n == (ssize_t)sizeof(pkt) && pkt.magic == TELEM_MAGIC && pkt.version == TELEM_VERSION) {
            latest = pkt;
            have_latest = 1;
            latest_rx_ms = now_ms();
            if (cfg.verbose) {
                printf("RX: %3u kph rpm=%4u brk=%3u acc=%3u rumble=%3u\n",
                       pkt.speed_kph, pkt.rpm, pkt.brake_pct, pkt.accel_pct, pkt.rumble);
            }
        }

        uint64_t now = now_ms();
        if (have_latest && (now - latest_rx_ms) <= TELEMETRY_STALE_MS) {
            if ((now - state.last_send_ms) >= period_ms) {
                bridge_out_t out;
                map_telemetry_to_controls(&latest, &out);
                send_updates_if_needed(serial_fd, &cfg, &state, &out);
                state.last_send_ms = now;
            }
        }

        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }

    if (serial_fd >= 0) close(serial_fd);
    close(udp_fd);
    return 0;
}
