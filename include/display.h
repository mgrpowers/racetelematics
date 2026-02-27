#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

typedef enum {
    COLOR_BLACK = 0,
    COLOR_WHITE = 1,
    COLOR_INVERT = 2
} color_t;

void display_clear(void);
void display_set_pixel(int16_t x, int16_t y, color_t color);
void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color);
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color);
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, color_t color);
void display_draw_char(int16_t x, int16_t y, char c, color_t color, uint8_t scale);
void display_draw_string(int16_t x, int16_t y, const char *str, color_t color, uint8_t scale);
void display_draw_hbar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct, color_t color);
void display_draw_segbar(int16_t x, int16_t y, int16_t w, int16_t h,
                         int segs, uint8_t pct, color_t color);
void display_draw_ramp(int16_t x, int16_t y, int16_t w, int16_t h,
                       int bars, uint8_t pct, color_t color);
void display_flush(void);

#endif
