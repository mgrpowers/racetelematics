#include "dashboard.h"
#include "display.h"
#include "ssd1305.h"
#include <stdio.h>

/*
 * Layout (132 x 64):
 *
 *  |  LEFT (0-35)  |  CENTER (38-82)  |  RIGHT (86-131)  |
 *  |               |                  |                   |
 *  |  LAP  3/12    |    +---------+   |  5896             |
 *  |               |    |         |   |                   |
 *  |  POS  P3      |    |    2    |   |  ▁▂▃▄▅▆▇█        |
 *  |               |    |         |   |  RPM ramp         |
 *  |               |    +---------+   |                   |
 */

void dashboard_render(const telemetry_t *t)
{
    char buf[24];
    display_clear();

    /* ---- LEFT COLUMN: lap and position (x 0-35) ---- */

    display_draw_string(1, 2, "LAP", COLOR_WHITE, 1);
    snprintf(buf, sizeof(buf), "%d/%d", t->lap, t->lap_total);
    display_draw_string(1, 12, buf, COLOR_WHITE, 1);

    /* divider */
    display_draw_line(0, 26, 35, 26, COLOR_WHITE);

    display_draw_string(1, 30, "POS", COLOR_WHITE, 1);
    snprintf(buf, sizeof(buf), "P%d", t->position);
    display_draw_string(1, 42, buf, COLOR_WHITE, 2);

    /* vertical separator */
    display_draw_line(37, 0, 37, 63, COLOR_WHITE);

    /* ---- CENTER: gear number in a box (x 38-84) ---- */

    int box_x = 42;
    int box_y = 4;
    int box_w = 40;
    int box_h = 56;
    display_draw_rect(box_x, box_y, box_w, box_h, COLOR_WHITE);
    display_draw_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, COLOR_WHITE);

    snprintf(buf, sizeof(buf), "%d", t->gear);
    /* 5x7 at scale 6 = 30x42, centered in the box */
    int char_w = 5 * 6;
    int char_h = 7 * 6;
    int cx = box_x + (box_w - char_w) / 2;
    int cy = box_y + (box_h - char_h) / 2;
    display_draw_char(cx, cy, buf[0], COLOR_WHITE, 6);

    /* vertical separator */
    display_draw_line(85, 0, 85, 63, COLOR_WHITE);

    /* ---- RIGHT COLUMN: RPM numeric + ramp (x 86-131) ---- */

    snprintf(buf, sizeof(buf), "%d", t->rpm);
    display_draw_string(88, 2, buf, COLOR_WHITE, 1);

    /* RPM ramp bars */
    int rpm_pct = (int)((uint32_t)t->rpm * 100 / t->rpm_max);
    if (rpm_pct > 100) rpm_pct = 100;
    display_draw_ramp(88, 14, 42, 48, 10, (uint8_t)rpm_pct, COLOR_WHITE);

    display_flush();
}
