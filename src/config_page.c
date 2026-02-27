#include "config_page.h"
#include "display.h"
#include "ssd1305.h"
#include <stdio.h>

/*
 * Config page layout (132 x 64):
 *
 *  +-------------------------------+
 *  | CONFIG               12:34   |
 *  +-------------------------------+
 *  |> SPRING  [==========   ] 75%  |   <- '>' cursor on selected row
 *  |                               |
 *  |  STRGTH  [============ ] 90%  |
 *  +-------------------------------+
 *  | BAT [||||||||     ] 85%  :ss  |
 *  +-------------------------------+
 *
 *  Up/Down = move cursor     Left/Right = adjust value
 */

static void draw_labeled_bar(int16_t y, const char *label,
                             uint8_t pct, int selected)
{
    char buf[8];

    if (selected) {
        display_fill_rect(0, y - 1, SSD1305_WIDTH, 17, COLOR_WHITE);
        display_draw_string(1, y, label, COLOR_BLACK, 1);
        display_draw_hbar(1, y + 9, 104, 7, pct, COLOR_BLACK);
        snprintf(buf, sizeof(buf), "%3d%%", pct);
        display_draw_string(108, y + 9, buf, COLOR_BLACK, 1);
    } else {
        display_draw_string(1, y, label, COLOR_WHITE, 1);
        display_draw_hbar(1, y + 9, 104, 7, pct, COLOR_WHITE);
        snprintf(buf, sizeof(buf), "%3d%%", pct);
        display_draw_string(108, y + 9, buf, COLOR_WHITE, 1);
    }
}

void config_page_render(const controller_config_t *cfg)
{
    char buf[24];
    display_clear();

    /* ---- header ---- */
    display_draw_string(1, 1, "CONFIG", COLOR_WHITE, 1);

    snprintf(buf, sizeof(buf), "%02d:%02d", cfg->clock_hour, cfg->clock_min);
    display_draw_string(100, 1, buf, COLOR_WHITE, 1);

    display_draw_line(0, 10, SSD1305_WIDTH - 1, 10, COLOR_WHITE);

    /* ---- spring rate ---- */
    draw_labeled_bar(13, "SPRING", cfg->spring_rate,
                     cfg->selected == CFG_SEL_SPRING);

    /* ---- strength ---- */
    draw_labeled_bar(30, "STRGTH", cfg->strength,
                     cfg->selected == CFG_SEL_STRENGTH);

    /* ---- battery ---- */
    display_draw_line(0, 47, SSD1305_WIDTH - 1, 47, COLOR_WHITE);

    display_draw_string(1, 50, "BAT", COLOR_WHITE, 1);
    display_draw_hbar(22, 49, 70, 9, cfg->battery_pct, COLOR_WHITE);

    snprintf(buf, sizeof(buf), "%3d%%", cfg->battery_pct);
    display_draw_string(96, 50, buf, COLOR_WHITE, 1);

    snprintf(buf, sizeof(buf), ":%02d", cfg->clock_sec);
    display_draw_string(112, 58, buf, COLOR_WHITE, 1);

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
