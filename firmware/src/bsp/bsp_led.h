#ifndef FORGIX_BSP_LED_H
#define FORGIX_BSP_LED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    bool enabled;
} bsp_led_state_t;

void bsp_led_set(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t brightness);
void bsp_led_off(void);
bsp_led_state_t bsp_led_get(void);

#endif
