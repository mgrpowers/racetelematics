#ifndef CONFIG_PAGE_H
#define CONFIG_PAGE_H

#include <stdint.h>

typedef enum {
    CFG_SEL_SPRING,
    CFG_SEL_STRENGTH,
    CFG_SEL_COUNT
} config_sel_t;

typedef struct {
    uint8_t     spring_rate;    /* 0-100 % */
    uint8_t     strength;       /* 0-100 % */
    uint8_t     battery_pct;    /* 0-100 % */
    uint8_t     clock_hour;     /* 0-23 */
    uint8_t     clock_min;      /* 0-59 */
    uint8_t     clock_sec;      /* 0-59 */
    config_sel_t selected;      /* which row the cursor is on */
} controller_config_t;

void config_page_render(const controller_config_t *cfg);
void config_page_adjust(controller_config_t *cfg, int delta);
void config_page_nav(controller_config_t *cfg, int dir);

#endif
