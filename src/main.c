#include "ssd1305.h"
#include "ssd1305_hal.h"
#include "dashboard.h"
#include <SDL.h>
#include <math.h>
#include <stdbool.h>

#define TARGET_FPS   30
#define FRAME_MS     (1000 / TARGET_FPS)

#define RPM_MAX    8500
#define RPM_IDLE   900
#define RPM_SHIFT  7500
#define TOP_SPEED  320

static void sim_telemetry(telemetry_t *t, uint32_t tick_ms)
{
    double s = tick_ms / 1000.0;

    /* lap-like cycle: accelerate then brake */
    double phase = fmod(s, 12.0);
    double accel_f, brake_f, speed_f;

    if (phase < 7.0) {
        /* accelerating */
        speed_f = phase / 7.0;
        accel_f = 0.6 + 0.4 * sin(phase * 0.9);
        brake_f = 0.0;
    } else if (phase < 9.0) {
        /* hard braking */
        double bp = (phase - 7.0) / 2.0;
        speed_f = 1.0 - bp * 0.6;
        brake_f = 0.8 + 0.2 * sin(bp * 3.14);
        accel_f = 0.0;
    } else {
        /* coasting / light accel out of corner */
        double cp = (phase - 9.0) / 3.0;
        speed_f = 0.4 + cp * 0.15;
        accel_f = 0.3 + cp * 0.3;
        brake_f = 0.0;
    }

    if (speed_f < 0.0) speed_f = 0.0;
    if (speed_f > 1.0) speed_f = 1.0;

    t->speed_kph = (uint16_t)(speed_f * TOP_SPEED);
    t->rpm_max   = RPM_MAX;
    t->rpm_shift = RPM_SHIFT;

    /* derive gear and RPM from speed */
    int gear;
    double rpm_f;
    if (t->speed_kph < 40)       { gear = 1; rpm_f = (double)t->speed_kph / 40.0; }
    else if (t->speed_kph < 80)  { gear = 2; rpm_f = (double)(t->speed_kph - 40) / 40.0; }
    else if (t->speed_kph < 140) { gear = 3; rpm_f = (double)(t->speed_kph - 80) / 60.0; }
    else if (t->speed_kph < 210) { gear = 4; rpm_f = (double)(t->speed_kph - 140) / 70.0; }
    else if (t->speed_kph < 280) { gear = 5; rpm_f = (double)(t->speed_kph - 210) / 70.0; }
    else                         { gear = 6; rpm_f = (double)(t->speed_kph - 280) / 40.0; }

    if (rpm_f > 1.0) rpm_f = 1.0;
    t->gear = (uint8_t)gear;
    t->rpm  = (uint16_t)(RPM_IDLE + rpm_f * (RPM_MAX - RPM_IDLE));

    t->accel_pct = (uint8_t)(accel_f * 100.0);
    t->brake_pct = (uint8_t)(brake_f * 100.0);

    /* lap increments every 12-second cycle */
    int lap_cycle = (int)(s / 12.0);
    t->lap_total = 12;
    t->lap = (uint8_t)((lap_cycle % t->lap_total) + 1);

    /* position fluctuates slightly */
    int base_pos = 3;
    if (speed_f > 0.85) base_pos = 2;
    if (speed_f > 0.95) base_pos = 1;
    t->position = (uint8_t)base_pos;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    ssd1305_init();

    telemetry_t telem = {0};
    bool running = true;
    uint32_t start = SDL_GetTicks();

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

        uint32_t elapsed = SDL_GetTicks() - start;
        sim_telemetry(&telem, elapsed);
        dashboard_render(&telem);

        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_MS)
            SDL_Delay(FRAME_MS - frame_time);
    }

    hal_display_destroy();
    return 0;
}
