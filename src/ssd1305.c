#include "ssd1305.h"
#include "ssd1305_hal.h"
#include <string.h>

static uint8_t framebuffer[SSD1305_BUF_SIZE];

static void ssd1305_cmd(uint8_t cmd)
{
    hal_gpio_dc(0);
    hal_spi_write(&cmd, 1);
}

static void ssd1305_cmd2(uint8_t cmd, uint8_t arg)
{
    hal_gpio_dc(0);
    uint8_t buf[2] = { cmd, arg };
    hal_spi_write(buf, 2);
}

void ssd1305_init(void)
{
    hal_display_init();

    hal_gpio_reset(0);
    hal_delay_ms(10);
    hal_gpio_reset(1);
    hal_delay_ms(10);

    ssd1305_cmd(0xAE);             /* display off                       */
    ssd1305_cmd2(0xD5, 0x80);      /* clock div / osc freq              */
    ssd1305_cmd2(0xA8, 0x3F);      /* multiplex 64                      */
    ssd1305_cmd2(0xD3, 0x00);      /* display offset 0                  */
    ssd1305_cmd(0x40);             /* start line 0                      */
    ssd1305_cmd2(0xAD, 0x8E);      /* master config, ext Vcc            */
    ssd1305_cmd(0xA1);             /* segment remap col131->SEG0        */
    ssd1305_cmd(0xC8);             /* COM scan remapped                 */
    ssd1305_cmd2(0xDA, 0x12);      /* COM pins: alternative, no remap   */
    ssd1305_cmd2(0x81, 0xCF);      /* contrast                          */
    ssd1305_cmd2(0xD9, 0xF1);      /* precharge                         */
    ssd1305_cmd2(0xDB, 0x40);      /* VCOMH deselect                    */
    ssd1305_cmd(0xA4);             /* output follows RAM                */
    ssd1305_cmd(0xA6);             /* normal (not inverted)             */
    ssd1305_cmd2(0x20, 0x00);      /* horizontal addressing mode        */
    ssd1305_cmd(0xAF);             /* display on                        */

    ssd1305_clear();
    ssd1305_flush();
}

void ssd1305_clear(void)
{
    memset(framebuffer, 0, SSD1305_BUF_SIZE);
}

void ssd1305_flush(void)
{
    /* set column address 0..131 */
    hal_gpio_dc(0);
    uint8_t col_cmd[3] = { 0x21, 0x00, SSD1305_WIDTH - 1 };
    hal_spi_write(col_cmd, 3);

    /* set page address 0..7 */
    uint8_t page_cmd[3] = { 0x22, 0x00, SSD1305_PAGES - 1 };
    hal_spi_write(page_cmd, 3);

    /* write framebuffer as data */
    hal_gpio_dc(1);
    hal_spi_write(framebuffer, SSD1305_BUF_SIZE);

    hal_display_refresh();
}

uint8_t *ssd1305_get_buffer(void)
{
    return framebuffer;
}
