#ifndef FORGIX_BSP_LED_H
#define FORGIX_BSP_LED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    bool enabled;
} bsp_led_state_t;

void BSP_LedSet(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t brightness);
void BSP_LedOff(void);
bsp_led_state_t BSP_LedGet(void);

#endif
