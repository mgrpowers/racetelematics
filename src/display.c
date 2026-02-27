#include "display.h"
#include "ssd1305.h"
#include "font5x7.h"
#include <stdlib.h>

static inline int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void display_clear(void)
{
    ssd1305_clear();
}

void display_set_pixel(int16_t x, int16_t y, color_t color)
{
    if (x < 0 || x >= SSD1305_WIDTH || y < 0 || y >= SSD1305_HEIGHT)
        return;

    uint8_t *buf = ssd1305_get_buffer();
    int page = y / 8;
    uint8_t mask = 1 << (y & 7);
    int idx = page * SSD1305_WIDTH + x;

    switch (color) {
    case COLOR_WHITE:  buf[idx] |=  mask; break;
    case COLOR_BLACK:  buf[idx] &= ~mask; break;
    case COLOR_INVERT: buf[idx] ^=  mask; break;
    }
}

void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        display_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color)
{
    display_draw_line(x, y, x + w - 1, y, color);
    display_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    display_draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    display_draw_line(x, y + h - 1, x, y, color);
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color)
{
    int x0 = clamp(x, 0, SSD1305_WIDTH - 1);
    int x1 = clamp(x + w - 1, 0, SSD1305_WIDTH - 1);
    int y0 = clamp(y, 0, SSD1305_HEIGHT - 1);
    int y1 = clamp(y + h - 1, 0, SSD1305_HEIGHT - 1);

    for (int yy = y0; yy <= y1; yy++)
        for (int xx = x0; xx <= x1; xx++)
            display_set_pixel(xx, yy, color);
}

void display_draw_char(int16_t x, int16_t y, char c, color_t color, uint8_t scale)
{
    if (c < FONT_FIRST || c > FONT_LAST)
        c = ' ';

    const uint8_t *glyph = &font5x7[(c - FONT_FIRST) * FONT_WIDTH];

    for (int col = 0; col < FONT_WIDTH; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < FONT_HEIGHT; row++) {
            if (line & (1 << row)) {
                if (scale == 1) {
                    display_set_pixel(x + col, y + row, color);
                } else {
                    display_fill_rect(x + col * scale, y + row * scale,
                                      scale, scale, color);
                }
            }
        }
    }
}

void display_draw_string(int16_t x, int16_t y, const char *str, color_t color, uint8_t scale)
{
    int16_t cx = x;
    while (*str) {
        display_draw_char(cx, y, *str, color, scale);
        cx += (FONT_WIDTH + 1) * scale;
        str++;
    }
}

void display_draw_hbar(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint8_t pct, color_t color)
{
    display_draw_rect(x, y, w, h, color);
    int fill_w = (int)(w - 2) * clamp(pct, 0, 100) / 100;
    if (fill_w > 0)
        display_fill_rect(x + 1, y + 1, fill_w, h - 2, color);
}

void display_draw_segbar(int16_t x, int16_t y, int16_t w, int16_t h,
                         int segs, uint8_t pct, color_t color)
{
    if (segs < 1) segs = 1;
    int gap = 1;
    int seg_w = (w - gap * (segs - 1)) / segs;
    if (seg_w < 2) { seg_w = 2; gap = 1; }

    int filled = (int)segs * clamp(pct, 0, 100) / 100;

    for (int i = 0; i < segs; i++) {
        int sx = x + i * (seg_w + gap);
        if (i < filled)
            display_fill_rect(sx, y, seg_w, h, color);
        else
            display_draw_rect(sx, y, seg_w, h, color);
    }
}

void display_draw_ramp(int16_t x, int16_t y, int16_t w, int16_t h,
                       int bars, uint8_t pct, color_t color)
{
    if (bars < 1) bars = 1;
    int gap = 1;
    int bar_w = (w - gap * (bars - 1)) / bars;
    if (bar_w < 2) { bar_w = 2; gap = 0; }

    int filled = (int)bars * clamp(pct, 0, 100) / 100;

    for (int i = 0; i < bars; i++) {
        int bar_h = (int)h * (i + 1) / bars;
        int bx = x + i * (bar_w + gap);
        int by = y + (h - bar_h);

        if (i < filled)
            display_fill_rect(bx, by, bar_w, bar_h, color);
        else
            display_draw_rect(bx, by, bar_w, bar_h, color);
    }
}

void display_flush(void)
{
    ssd1305_flush();
}
