#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdint.h>

typedef struct {
    uint16_t speed_kph;
    uint16_t rpm;
    uint16_t rpm_max;
    uint16_t rpm_shift;
    uint8_t  brake_pct;     /* 0-100 */
    uint8_t  accel_pct;     /* 0-100 */
    uint8_t  gear;          /* 0=N, 1-8 */
    uint8_t  lap;           /* current lap */
    uint8_t  lap_total;     /* total laps in race */
    uint8_t  position;      /* race position: 1=P1, 2=P2, etc. */

    /* haptic feedback channels */
    uint8_t  rumble;        /* 0-255  chassis vibration intensity */
    uint8_t  abs_active;    /* 1 when ABS is intervening */
    uint8_t  tc_active;     /* 1 when traction control is intervening */
    uint8_t  surface;       /* 0=tarmac 1=kerb 2=gravel 3=grass */
    uint8_t  shift_light;   /* 0-255  ramps up approaching shift point */
} telemetry_t;

void dashboard_render(const telemetry_t *t);

#endif
