#include "bsp_led.h"

#include "bsp_fpga.h"

enum {
    REG_LED_R = 0x10,
    REG_LED_G = 0x11,
    REG_LED_B = 0x12,
    REG_LED_GLOBAL = 0x13,
    REG_LED_ENABLE = 0x14,
};

void bsp_led_set(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t brightness) {
    bsp_fpga_write_register(REG_LED_R, red);
    bsp_fpga_write_register(REG_LED_G, green);
    bsp_fpga_write_register(REG_LED_B, blue);
    bsp_fpga_write_register(REG_LED_GLOBAL, brightness);
    bsp_fpga_write_register(REG_LED_ENABLE, 1);
}

void bsp_led_off(void) {
    bsp_fpga_write_register(REG_LED_ENABLE, 0);
}

bsp_led_state_t bsp_led_get(void) {
    const bsp_led_state_t state = {
        .red = bsp_fpga_read_register(REG_LED_R),
        .green = bsp_fpga_read_register(REG_LED_G),
        .blue = bsp_fpga_read_register(REG_LED_B),
        .brightness = bsp_fpga_read_register(REG_LED_GLOBAL),
        .enabled = (bsp_fpga_read_register(REG_LED_ENABLE) & 1u) != 0,
    };
    return state;
}
