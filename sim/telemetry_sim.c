/*
 * telemetry_sim  –  generate realistic GT3-style race telemetry over UDP
 *
 * A simulated car drives a ~4 km circuit in a loop, producing speed, RPM,
 * gear, brake/accel, and haptic data at a configurable rate (default 60 Hz).
 *
 * Usage:
 *   ./telemetry_sim [-v] [--hz 60] [--host 127.0.0.1] [--port 5100]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>

#include "telemetry_proto.h"

/* ── vehicle parameters ─────────────────────────────────────────────── */

#define TIRE_RADIUS     0.33        /* m */
#define FINAL_DRIVE     3.73
#define NUM_GEARS       6
#define CAR_MASS        1400.0      /* kg (GT3-class) */
#define PEAK_TORQUE_NM  450.0

#define RPM_MAX_VAL     9000
#define RPM_SHIFT_VAL   8500
#define RPM_IDLE        950
#define RPM_DOWNSHIFT   4000

#define MAX_BRAKE_G     1.5
#define MAX_LAT_G       1.8
#define MAX_SPEED_MPS   83.3        /* ~300 km/h */

static const double gear_ratio[NUM_GEARS] = {
    3.82, 2.36, 1.69, 1.31, 1.05, 0.87
};

static double engine_accel(int gear, double rpm)
{
    double torque_frac;
    if (rpm < RPM_IDLE)
        torque_frac = 0.0;
    else if (rpm < 3000)
        torque_frac = 0.6 + 0.4 * (rpm - RPM_IDLE) / (3000.0 - RPM_IDLE);
    else if (rpm < 6000)
        torque_frac = 1.0;
    else if (rpm < RPM_MAX_VAL)
        torque_frac = 1.0 - 0.3 * (rpm - 6000.0) / (RPM_MAX_VAL - 6000.0);
    else
        torque_frac = 0.0;

    double force = torque_frac * PEAK_TORQUE_NM
                 * gear_ratio[gear] * FINAL_DRIVE / TIRE_RADIUS;
    return force / CAR_MASS;
}

/* ── track definition ───────────────────────────────────────────────── */

typedef enum { SEG_STRAIGHT, SEG_CORNER } seg_kind_t;

typedef struct {
    seg_kind_t kind;
    double     length;   /* metres */
    double     radius;   /* corner radius (0 for straights) */
    int        kerbed;   /* kerbs at entry / exit */
} segment_t;

static double corner_vmax(double radius)
{
    return sqrt(MAX_LAT_G * 9.81 * radius);
}

static const segment_t track[] = {
    { SEG_STRAIGHT, 780,   0, 0 },     /* main straight          */
    { SEG_CORNER,    90,  45, 1 },     /* T1  heavy braking      */
    { SEG_STRAIGHT, 180,   0, 0 },
    { SEG_CORNER,    60,  28, 1 },     /* T2  tight chicane in   */
    { SEG_STRAIGHT,  50,   0, 0 },
    { SEG_CORNER,    55,  32, 1 },     /* T3  chicane exit       */
    { SEG_STRAIGHT, 380,   0, 0 },     /* medium straight        */
    { SEG_CORNER,   160, 110, 0 },     /* T4  fast sweeper       */
    { SEG_STRAIGHT, 550,   0, 0 },     /* back straight          */
    { SEG_CORNER,    70,  22, 1 },     /* T5  hairpin            */
    { SEG_STRAIGHT, 140,   0, 0 },
    { SEG_CORNER,    85,  55, 0 },     /* T6  medium right       */
    { SEG_CORNER,   120, 140, 0 },     /* T7  fast kink          */
    { SEG_STRAIGHT, 280,   0, 0 },     /* run to start / finish  */
};

#define N_SEG ((int)(sizeof(track) / sizeof(track[0])))

static double seg_vmax(int idx)
{
    return (track[idx].kind == SEG_CORNER)
         ? corner_vmax(track[idx].radius)
         : MAX_SPEED_MPS;
}

/* ── simulation state ───────────────────────────────────────────────── */

typedef struct {
    int    seg;          /* current segment index */
    double seg_d;        /* distance into segment (m) */
    double v;            /* speed  (m/s) */
    int    gear;         /* 0-based index into gear_ratio[] */
    double rpm;
    double brake;        /* 0.0 – 1.0 */
    double throttle;     /* 0.0 – 1.0 */
    int    abs_on;
    int    tc_on;
    int    on_kerb;
    int    lap;
    int    shifting;
    int    shift_to_gear;
    double shift_t;
    double shift_dt;
    double shift_rpm_from;
    double shift_rpm_to;
} car_t;

static double speed_to_rpm(double v, int gear)
{
    double wheel_rps = v / (2.0 * M_PI * TIRE_RADIUS);
    return wheel_rps * gear_ratio[gear] * FINAL_DRIVE * 60.0;
}

static void start_shift(car_t *c, int to_gear, double shift_time)
{
    if (to_gear < 0) to_gear = 0;
    if (to_gear >= NUM_GEARS) to_gear = NUM_GEARS - 1;
    if (to_gear == c->gear) return;

    c->shifting = 1;
    c->shift_to_gear = to_gear;
    c->shift_t = 0.0;
    c->shift_dt = shift_time;
    c->shift_rpm_from = c->rpm;
    c->shift_rpm_to = speed_to_rpm(c->v, to_gear);
}

/*
 * Look ahead through the track and decide whether to brake.
 * Returns 1 and writes *v_target if braking is required.
 */
static int should_brake(const car_t *c, double *v_target)
{
    double gap = track[c->seg].length - c->seg_d;

    for (int i = 1; i <= N_SEG; i++) {
        int idx   = (c->seg + i) % N_SEG;
        double vt = seg_vmax(idx);

        if (vt < c->v) {
            double d_brake = (c->v * c->v - vt * vt)
                           / (2.0 * MAX_BRAKE_G * 9.81);
            d_brake *= 1.08;            /* safety margin */
            if (gap <= d_brake) {
                *v_target = vt;
                return 1;
            }
        }
        gap += track[idx].length;
        if (gap > 600.0) break;
    }
    *v_target = MAX_SPEED_MPS;
    return 0;
}

static void sim_step(car_t *c, double dt)
{
    double v_limit = seg_vmax(c->seg);
    double v_target;

    c->abs_on  = 0;
    c->tc_on   = 0;
    c->on_kerb = 0;

    int braking = (c->v > v_limit + 0.5) || should_brake(c, &v_target);

    if (c->shifting) {
        /* During shifts, briefly cut torque and blend RPM to target gear. */
        c->throttle = 0.0;
        c->brake = 0.0;
        c->v -= 0.45 * dt;
        if (c->v < 5.0) c->v = 5.0;

        c->shift_t += dt;
        double a = c->shift_t / c->shift_dt;
        if (a > 1.0) a = 1.0;
        c->rpm = c->shift_rpm_from + (c->shift_rpm_to - c->shift_rpm_from) * a;

        if (c->shift_t >= c->shift_dt) {
            c->gear = c->shift_to_gear;
            c->shifting = 0;
            c->rpm = speed_to_rpm(c->v, c->gear);
        }
    } else if (braking) {
        if (c->v > v_limit + 0.5)
            v_target = v_limit;

        double intensity = (c->v - v_target) / (c->v > 1.0 ? c->v : 1.0);
        intensity = fmin(1.0, fmax(0.25, intensity));

        c->brake    = intensity;
        c->throttle = 0.0;
        c->v       -= MAX_BRAKE_G * 9.81 * intensity * dt;
        if (c->v < v_target) c->v = v_target;

        if (intensity > 0.88)
            c->abs_on = 1;
    } else {
        c->brake = 0.0;
        if (c->v < v_limit - 1.0 && c->v < MAX_SPEED_MPS) {
            double a = engine_accel(c->gear, c->rpm);

            /* reduce traction in low gears through corners */
            if (track[c->seg].kind == SEG_CORNER && c->gear < 3) {
                a *= 0.7;
                if (a > 0.5 * 9.81)
                    c->tc_on = 1;
            }
            c->throttle = fmin(1.0, a / (MAX_BRAKE_G * 9.81));
            c->v += a * dt;
        } else {
            c->throttle = 0.08;
        }
        if (c->v > MAX_SPEED_MPS) c->v = MAX_SPEED_MPS;
    }

    if (c->v < 5.0) c->v = 5.0;

    /* ── gearbox ── */
    if (!c->shifting) {
        c->rpm = speed_to_rpm(c->v, c->gear);

        if (c->rpm >= RPM_SHIFT_VAL && c->gear < NUM_GEARS - 1) {
            /* Upshift: quick ignition cut and RPM drop. */
            start_shift(c, c->gear + 1, 0.090);
        } else if (c->rpm < (RPM_DOWNSHIFT - 300) && c->gear > 0) {
            /* Downshift: slightly longer transition. */
            start_shift(c, c->gear - 1, 0.120);
        }
    }
    if (c->rpm > RPM_MAX_VAL) c->rpm = RPM_MAX_VAL;
    if (c->rpm < RPM_IDLE)    c->rpm = RPM_IDLE;

    /* ── kerb detection ── */
    if (track[c->seg].kerbed) {
        double pct = c->seg_d / track[c->seg].length;
        if (pct < 0.15 || pct > 0.85)
            c->on_kerb = 1;
    }

    /* ── advance position ── */
    c->seg_d += c->v * dt;
    while (c->seg_d >= track[c->seg].length) {
        c->seg_d -= track[c->seg].length;
        c->seg = (c->seg + 1) % N_SEG;
        if (c->seg == 0)
            c->lap++;
    }
}

/* ── packet builder ─────────────────────────────────────────────────── */

static void build_packet(const car_t *c, telem_packet_t *p)
{
    memset(p, 0, sizeof(*p));
    p->magic     = TELEM_MAGIC;
    p->version   = TELEM_VERSION;
    p->speed_kph = (uint16_t)(c->v * 3.6);
    p->rpm       = (uint16_t)c->rpm;
    p->rpm_max   = RPM_MAX_VAL;
    p->rpm_shift = RPM_SHIFT_VAL;
    p->gear      = (uint8_t)(c->gear + 1);     /* 1-based for display */
    p->brake_pct = (uint8_t)(c->brake * 100.0);
    p->accel_pct = (uint8_t)(c->throttle * 100.0);

    /* haptics */
    double rumble = (c->v / MAX_SPEED_MPS) * 80.0;
    if (c->on_kerb)
        rumble += 120.0 + 50.0 * sin(c->seg_d * 20.0);
    if (c->abs_on)
        rumble += 40.0;

    p->rumble      = (uint8_t)fmin(255.0, fmax(0.0, rumble));
    p->abs_active  = (uint8_t)c->abs_on;
    p->tc_active   = (uint8_t)c->tc_on;
    p->surface     = c->on_kerb ? SURFACE_KERB : SURFACE_TARMAC;

    double shift_ramp = (c->rpm - (RPM_SHIFT_VAL - 1500)) / 1500.0;
    p->shift_light = (uint8_t)fmin(255.0, fmax(0.0, shift_ramp * 255.0));
}

/* ── main ───────────────────────────────────────────────────────────── */

static volatile int running = 1;

static void on_signal(int sig) { (void)sig; running = 0; }

int main(int argc, char *argv[])
{
    int         hz   = 60;
    const char *host = "127.0.0.1";
    int         port = TELEM_UDP_PORT;
    int         verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            verbose = 1;
        else if (!strcmp(argv[i], "--hz")   && i + 1 < argc) hz   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else {
            fprintf(stderr,
                "Usage: %s [-v] [--hz N] [--host IP] [--port N]\n", argv[0]);
            return (strcmp(argv[i], "-h") && strcmp(argv[i], "--help"));
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(port);
    inet_pton(AF_INET, host, &dest.sin_addr);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    double total_km = 0;
    for (int i = 0; i < N_SEG; i++) total_km += track[i].length;
    total_km /= 1000.0;

    printf("Race telemetry simulator\n");
    printf("  target : %s:%d @ %d Hz\n", host, port, hz);
    printf("  track  : %d segments, %.2f km\n", N_SEG, total_km);
    printf("  vehicle: GT3 class, %d RPM redline, %d gears\n",
           RPM_MAX_VAL, NUM_GEARS);
    printf("  Ctrl-C to stop\n\n");

    car_t car = {
        .seg  = 0,
        .v    = 30.0,       /* rolling start ~108 km/h */
        .gear = 2,
    };
    car.rpm = speed_to_rpm(car.v, car.gear);

    double dt = 1.0 / hz;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = (long)(dt * 1e9) };
    int frame = 0;

    while (running) {
        sim_step(&car, dt);

        telem_packet_t pkt;
        build_packet(&car, &pkt);

        sendto(sock, &pkt, sizeof(pkt), 0,
               (struct sockaddr *)&dest, sizeof(dest));

        if (verbose && (frame % hz == 0)) {
            printf("L%d S%02d | %3d kph %5d rpm G%d | "
                   "brk %3d%% thr %3d%% | rmbl %3d%s%s%s\n",
                   car.lap, car.seg,
                   pkt.speed_kph, pkt.rpm, pkt.gear,
                   pkt.brake_pct, pkt.accel_pct,
                   pkt.rumble,
                   pkt.abs_active ? " ABS" : "",
                   pkt.tc_active  ? " TC"  : "",
                   car.on_kerb    ? " KERB" : "");
        }
        frame++;
        nanosleep(&ts, NULL);
    }

    printf("\nStopped – %d frames, %d laps completed\n", frame, car.lap);
    close(sock);
    return 0;
}
