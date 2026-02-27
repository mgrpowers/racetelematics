#ifndef SSD1305_H
#define SSD1305_H

#include <stdint.h>

#define SSD1305_WIDTH   132
#define SSD1305_HEIGHT  64
#define SSD1305_PAGES   (SSD1305_HEIGHT / 8)
#define SSD1305_BUF_SIZE (SSD1305_WIDTH * SSD1305_PAGES)

void ssd1305_init(void);
void ssd1305_flush(void);
void ssd1305_clear(void);

uint8_t *ssd1305_get_buffer(void);

#endif
