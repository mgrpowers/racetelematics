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
    int send_hz;
    int verbose;
    int dry_run;
} bridge_cfg_t;

typedef struct {
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

static int write_serial_frame(int fd, int dry_run, const telem_packet_t *p, int verbose)
{
    char buf[160];
    int n = snprintf(
        buf, sizeof(buf),
        "T,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
        p->speed_kph,
        p->rpm,
        p->rpm_max,
        p->gear,
        p->brake_pct,
        p->accel_pct,
        p->rumble,
        p->abs_active,
        p->tc_active,
        p->surface,
        p->shift_light
    );

    if (verbose || dry_run) {
        printf("TX: %s", buf);
    }
    if (dry_run) return 0;

    ssize_t wr = write(fd, buf, (size_t)n);
    return (wr == n) ? 0 : -1;
}

static void poll_serial_input(int fd, int verbose)
{
    char rx[128];
    ssize_t n = read(fd, rx, sizeof(rx) - 1);
    if (n <= 0) return;

    rx[n] = '\0';
    if (verbose) {
        printf("PICO: %s", rx);
    }
}

static void usage(const char *argv0)
{
    printf("Usage: %s --serial <tty> [options]\n", argv0);
    printf("Options:\n");
    printf("  --serial <path>   USB serial device (required unless --dry-run)\n");
    printf("  --udp-port <n>    UDP telemetry port (default: %d)\n", TELEM_UDP_PORT);
    printf("  --baud <n>        Serial baud (default: %d)\n", DEFAULT_SERIAL_BAUD);
    printf("  --hz <n>          Max serial send rate (default: %d)\n", DEFAULT_SEND_HZ);
    printf("  --verbose, -v     Log serial RX/TX\n");
    printf("  --dry-run         Do not write serial, print TX lines only\n");
}

int main(int argc, char **argv)
{
    bridge_cfg_t cfg = {
        .serial_path = NULL,
        .udp_port = TELEM_UDP_PORT,
        .baud = DEFAULT_SERIAL_BAUD,
        .send_hz = DEFAULT_SEND_HZ,
        .verbose = 0,
        .dry_run = 0
    };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--serial") && i + 1 < argc) cfg.serial_path = argv[++i];
        else if (!strcmp(argv[i], "--udp-port") && i + 1 < argc) cfg.udp_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--baud") && i + 1 < argc) cfg.baud = atoi(argv[++i]);
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

    printf("pico_scroll_bridge\n");
    printf("  telemetry UDP : %d\n", cfg.udp_port);
    printf("  serial        : %s\n", cfg.dry_run ? "(dry-run)" : cfg.serial_path);
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
                printf("RX: %3u kph rpm=%4u brk=%3u acc=%3u gear=%u shift=%3u\n",
                       pkt.speed_kph, pkt.rpm, pkt.brake_pct, pkt.accel_pct, pkt.gear, pkt.shift_light);
            }
        }

        uint64_t now = now_ms();
        if (have_latest && (now - latest_rx_ms) <= TELEMETRY_STALE_MS) {
            if ((now - state.last_send_ms) >= period_ms) {
                if (write_serial_frame(serial_fd, cfg.dry_run, &latest, cfg.verbose) != 0 && !cfg.dry_run) {
                    perror("serial write");
                    break;
                }
                state.last_send_ms = now;
            }
        }

        if (!cfg.dry_run) {
            poll_serial_input(serial_fd, cfg.verbose);
        }

        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }

    if (serial_fd >= 0) close(serial_fd);
    close(udp_fd);
    return 0;
}
