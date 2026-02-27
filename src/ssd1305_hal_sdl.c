#include "ssd1305_hal.h"
#include "ssd1305.h"
#include <SDL.h>
#include <string.h>

#define SCALE       6
#define WIN_W       (SSD1305_WIDTH  * SCALE)
#define WIN_H       (SSD1305_HEIGHT * SCALE)

#define OLED_ON_R   0xFF
#define OLED_ON_G   0x8C
#define OLED_ON_B   0x00
#define OLED_OFF_R  0x0A
#define OLED_OFF_G  0x04
#define OLED_OFF_B  0x00

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;

static int  dc_state;
static uint8_t gddram[SSD1305_BUF_SIZE];

/* command parser state */
static int  addr_mode;
static int  col_start, col_end;
static int  page_start, page_end;
static int  col_ptr, page_ptr;
static int  cmd_pending;
static uint8_t cmd_byte;
static int  cmd_args_remaining;

static void reset_ptrs(void)
{
    col_ptr  = col_start;
    page_ptr = page_start;
}

static void advance_ptr(void)
{
    if (addr_mode == 0) { /* horizontal */
        col_ptr++;
        if (col_ptr > col_end) {
            col_ptr = col_start;
            page_ptr++;
            if (page_ptr > page_end)
                page_ptr = page_start;
        }
    } else { /* vertical */
        page_ptr++;
        if (page_ptr > page_end) {
            page_ptr = page_start;
            col_ptr++;
            if (col_ptr > col_end)
                col_ptr = col_start;
        }
    }
}

static void process_command(uint8_t byte);
static void process_data(uint8_t byte);

void hal_display_init(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("SSD1305 – Race Dashboard",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              WIN_W, WIN_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 SSD1305_WIDTH, SSD1305_HEIGHT);

    memset(gddram, 0, sizeof(gddram));
    addr_mode  = 0;
    col_start  = 0;
    col_end    = SSD1305_WIDTH - 1;
    page_start = 0;
    page_end   = SSD1305_PAGES - 1;
    cmd_pending = 0;
    cmd_args_remaining = 0;
    reset_ptrs();
}

void hal_display_destroy(void)
{
    if (texture)  SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    SDL_Quit();
}

void hal_gpio_reset(int state)
{
    if (!state) {
        memset(gddram, 0, sizeof(gddram));
        addr_mode = 0;
        col_start = 0;  col_end = SSD1305_WIDTH - 1;
        page_start = 0; page_end = SSD1305_PAGES - 1;
        cmd_pending = 0;
        cmd_args_remaining = 0;
        reset_ptrs();
    }
}

void hal_gpio_dc(int state)
{
    dc_state = state;
}

static void process_command(uint8_t byte)
{
    if (cmd_args_remaining > 0) {
        switch (cmd_byte) {
        case 0x20: addr_mode = byte & 0x03; break;
        case 0x21:
            if (cmd_args_remaining == 2) col_start = byte;
            else                         col_end   = byte;
            break;
        case 0x22:
            if (cmd_args_remaining == 2) page_start = byte;
            else                         page_end   = byte;
            break;
        default: break;
        }
        cmd_args_remaining--;
        if (cmd_args_remaining == 0)
            reset_ptrs();
        return;
    }

    if (byte == 0x20) { cmd_byte = 0x20; cmd_args_remaining = 1; return; }
    if (byte == 0x21) { cmd_byte = 0x21; cmd_args_remaining = 2; return; }
    if (byte == 0x22) { cmd_byte = 0x22; cmd_args_remaining = 2; return; }

    /* two-byte commands we can ignore in emulation */
    if (byte == 0x81 || byte == 0x82 || byte == 0xA8 ||
        byte == 0xAD || byte == 0xD3 || byte == 0xD5 ||
        byte == 0xD8 || byte == 0xD9 || byte == 0xDA ||
        byte == 0xDB) {
        cmd_byte = byte;
        cmd_args_remaining = 1;
        return;
    }
    if (byte == 0x91) { cmd_byte = byte; cmd_args_remaining = 4; return; }
    if (byte == 0x92 || byte == 0x93) { cmd_byte = byte; cmd_args_remaining = 4; return; }
    if (byte == 0xAB) { cmd_byte = byte; cmd_args_remaining = 3; return; }

    /* single-byte commands: segment remap, COM dir, start line, etc.
       All safely ignored in emulation. */
}

static void process_data(uint8_t byte)
{
    if (page_ptr < SSD1305_PAGES && col_ptr < SSD1305_WIDTH)
        gddram[page_ptr * SSD1305_WIDTH + col_ptr] = byte;
    advance_ptr();
}

void hal_spi_write(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (dc_state)
            process_data(data[i]);
        else
            process_command(data[i]);
    }
}

void hal_delay_ms(uint32_t ms)
{
    SDL_Delay(ms);
}

void hal_display_refresh(void)
{
    uint8_t pixels[SSD1305_HEIGHT * SSD1305_WIDTH * 3];

    for (int page = 0; page < SSD1305_PAGES; page++) {
        for (int col = 0; col < SSD1305_WIDTH; col++) {
            uint8_t val = gddram[page * SSD1305_WIDTH + col];
            for (int bit = 0; bit < 8; bit++) {
                int y = page * 8 + bit;
                int idx = (y * SSD1305_WIDTH + col) * 3;
                if (val & (1 << bit)) {
                    pixels[idx + 0] = OLED_ON_R;
                    pixels[idx + 1] = OLED_ON_G;
                    pixels[idx + 2] = OLED_ON_B;
                } else {
                    pixels[idx + 0] = OLED_OFF_R;
                    pixels[idx + 1] = OLED_OFF_G;
                    pixels[idx + 2] = OLED_OFF_B;
                }
            }
        }
    }

    SDL_UpdateTexture(texture, NULL, pixels, SSD1305_WIDTH * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
