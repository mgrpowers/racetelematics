#include "dashboard.h"
#include "display.h"
#include "ssd1305.h"
#include <stdio.h>

/*
 * E46-style race dashboard (132 x 64):
 *
 * ┌────────────────────────────────────┐
 * │█████████████ RACE ████████████████ │  inverted header
 * ├────────────────────────────────────┤
 * │                                    │
 * │  LAP  3/12  │      │     5896     │
 * │             │  2   │  ▓▓▓▓▓▓░░░  │  gear center, segmented RPM
 * │  POS   P3   │      │             │
 * │             │      │             │
 * ├────────────────────────────────────┤
 * │  ▸ 187 km/h               SHIFT  │  speed + shift indicator
 * └────────────────────────────────────┘
 */

static uint32_t frame_count;

void dashboard_render(const telemetry_t *t)
{
    char buf[24];
    display_clear();

    int W = SSD1305_WIDTH;

    /* ---- outer frame ---- */
    display_draw_rect(0, 0, W, 64, COLOR_WHITE);

    /* === left column: lap & position (x 2-42) === */

    display_draw_string(4, 3, "LAP", COLOR_WHITE, 1);
    snprintf(buf, sizeof(buf), "%2d/%d", t->lap, t->lap_total);
    display_draw_string(4, 12, buf, COLOR_WHITE, 1);

    display_draw_line(4, 24, 40, 24, COLOR_WHITE);

    display_draw_string(4, 27, "POS", COLOR_WHITE, 1);
    snprintf(buf, sizeof(buf), "P%d", t->position);
    display_draw_string(4, 37, buf, COLOR_WHITE, 1);

    /* vertical separator left */
    display_draw_line(44, 1, 44, 52, COLOR_WHITE);

    /* === center: gear (x 45-86) === */

    snprintf(buf, sizeof(buf), "%d", t->gear);
    display_draw_char(50, 5, buf[0], COLOR_WHITE, 6);

    /* vertical separator right */
    display_draw_line(88, 1, 88, 52, COLOR_WHITE);

    /* === right column: RPM (x 89-130) === */

    snprintf(buf, sizeof(buf), "%d", t->rpm);
    display_draw_string(92, 3, buf, COLOR_WHITE, 1);

    /* segmented RPM bar (E46 block style) */
    int rpm_pct = (int)((uint32_t)t->rpm * 100 / t->rpm_max);
    if (rpm_pct > 100) rpm_pct = 100;
    display_draw_segbar(92, 14, 36, 5, 12, (uint8_t)rpm_pct, COLOR_WHITE);

    /* RPM ramp underneath */
    display_draw_ramp(92, 22, 36, 28, 8, (uint8_t)rpm_pct, COLOR_WHITE);

    /* ---- bottom bar divider ---- */
    display_draw_line(1, 53, W - 2, 53, COLOR_WHITE);

    /* === footer: speed + shift === */

    snprintf(buf, sizeof(buf), "%3d km/h", t->speed_kph);
    display_draw_string(4, 56, buf, COLOR_WHITE, 1);

    if (t->rpm >= t->rpm_shift) {
        int blink = (frame_count / 4) & 1;
        if (blink) {
            display_fill_rect(90, 55, 39, 8, COLOR_WHITE);
            display_draw_string(92, 56, "SHIFT", COLOR_BLACK, 1);
        } else {
            display_draw_string(92, 56, "SHIFT", COLOR_WHITE, 1);
        }
    }

    display_flush();
    frame_count++;
}
