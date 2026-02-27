#ifndef SSD1305_HAL_H
#define SSD1305_HAL_H

#include <stdint.h>

void hal_display_init(void);
void hal_display_destroy(void);
void hal_gpio_reset(int state);
void hal_gpio_dc(int state);
void hal_spi_write(const uint8_t *data, uint16_t len);
void hal_delay_ms(uint32_t ms);

/* Called after framebuffer flush so the emulator can
   refresh the window.  No-op on real hardware. */
void hal_display_refresh(void);

#endif
