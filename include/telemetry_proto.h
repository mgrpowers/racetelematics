#ifndef TELEMETRY_PROTO_H
#define TELEMETRY_PROTO_H

#include <stdint.h>

#define TELEM_UDP_PORT  5100
#define TELEM_MAGIC     0x5243      /* "RC" */
#define TELEM_VERSION   1

typedef enum {
    SURFACE_TARMAC = 0,
    SURFACE_KERB   = 1,
    SURFACE_GRAVEL = 2,
    SURFACE_GRASS  = 3
} surface_t;

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;

    /* core motion */
    uint16_t speed_kph;
    uint16_t rpm;
    uint16_t rpm_max;
    uint16_t rpm_shift;
    uint8_t  gear;          /* 0=N, 1-6 */
    uint8_t  brake_pct;     /* 0-100 */
    uint8_t  accel_pct;     /* 0-100 */

    /* haptic / feedback */
    uint8_t  rumble;        /* 0-255 chassis vibration intensity */
    uint8_t  abs_active;    /* 0 or 1 */
    uint8_t  tc_active;     /* 0 or 1 */
    uint8_t  surface;       /* surface_t */
    uint8_t  shift_light;   /* 0-255, ramps up near shift point */
} telem_packet_t;
#pragma pack(pop)

#endif
