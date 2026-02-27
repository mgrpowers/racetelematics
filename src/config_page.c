#include "config_page.h"
#include "display.h"
#include "ssd1305.h"
#include <stdio.h>

/*
 * E46-style config page (132 x 64):
 *
 * ┌────────────────────────────────────┐
 * │████████ SETTINGS ████████  12:34 ██│  inverted header
 * ├────────────────────────────────────┤
 * │                                    │
 * │  ▸ SPRING RATE               75%  │  selected = small triangle
 * │    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░         │  segmented bar
 * │  ──────────────────────────────── │
 * │    FF STRENGTH               90%  │
 * │    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░         │
 * ├────────────────────────────────────┤
 * │  BAT  ▓▓▓▓▓▓▓▓░░  85%      :34   │
 * └────────────────────────────────────┘
 */

static void draw_triangle(int16_t x, int16_t y, color_t color)
{
    display_set_pixel(x,     y + 2, color);
    display_set_pixel(x + 1, y + 1, color);
    display_set_pixel(x + 1, y + 2, color);
    display_set_pixel(x + 1, y + 3, color);
    display_set_pixel(x + 2, y,     color);
    display_set_pixel(x + 2, y + 1, color);
    display_set_pixel(x + 2, y + 2, color);
    display_set_pixel(x + 2, y + 3, color);
    display_set_pixel(x + 2, y + 4, color);
    display_set_pixel(x + 3, y + 1, color);
    display_set_pixel(x + 3, y + 2, color);
    display_set_pixel(x + 3, y + 3, color);
    display_set_pixel(x + 4, y + 2, color);
}

static void draw_config_row(int16_t y, const char *label,
                             uint8_t pct, int selected)
{
    char buf[8];
    int W = SSD1305_WIDTH;

    if (selected)
        draw_triangle(3, y, COLOR_WHITE);

    display_draw_string(10, y, label, COLOR_WHITE, 1);

    snprintf(buf, sizeof(buf), "%3d%%", pct);
    display_draw_string(W - 28, y, buf, COLOR_WHITE, 1);

    display_draw_segbar(10, y + 9, W - 22, 5, 16, pct, COLOR_WHITE);
}

void config_page_render(const controller_config_t *cfg)
{
    char buf[24];
    int W = SSD1305_WIDTH;
    display_clear();

    /* ---- outer frame ---- */
    display_draw_rect(0, 0, W, 64, COLOR_WHITE);

    /* ---- header ---- */
    display_draw_string(4, 2, "SETTINGS", COLOR_WHITE, 1);

    snprintf(buf, sizeof(buf), "%02d:%02d", cfg->clock_hour, cfg->clock_min);
    display_draw_string(W - 32, 2, buf, COLOR_WHITE, 1);

    display_draw_line(1, 10, W - 2, 10, COLOR_WHITE);

    /* ---- spring rate ---- */
    draw_config_row(15, "SPRING", cfg->spring_rate,
                    cfg->selected == CFG_SEL_SPRING);

    /* thin separator */
    display_draw_line(6, 30, W - 6, 30, COLOR_WHITE);

    /* ---- strength ---- */
    draw_config_row(33, "STRENGTH", cfg->strength,
                    cfg->selected == CFG_SEL_STRENGTH);

    /* ---- bottom bar ---- */
    display_draw_line(1, 49, W - 2, 49, COLOR_WHITE);

    /* battery */
    display_draw_string(4, 52, "BAT", COLOR_WHITE, 1);
    display_draw_segbar(24, 52, 60, 5, 10, cfg->battery_pct, COLOR_WHITE);

    snprintf(buf, sizeof(buf), "%3d%%", cfg->battery_pct);
    display_draw_string(88, 52, buf, COLOR_WHITE, 1);

    display_flush();
}

void config_page_nav(controller_config_t *cfg, int dir)
{
    int sel = (int)cfg->selected + dir;
    if (sel < 0) sel = CFG_SEL_COUNT - 1;
    if (sel >= CFG_SEL_COUNT) sel = 0;
    cfg->selected = (config_sel_t)sel;
}

void config_page_adjust(controller_config_t *cfg, int delta)
{
    uint8_t *val = NULL;

    switch (cfg->selected) {
    case CFG_SEL_SPRING:   val = &cfg->spring_rate; break;
    case CFG_SEL_STRENGTH: val = &cfg->strength;    break;
    default: return;
    }

    int v = (int)*val + delta;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    *val = (uint8_t)v;
}
