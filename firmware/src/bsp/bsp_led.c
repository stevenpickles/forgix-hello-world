#include "bsp_led.h"

#include "bsp_fpga.h"

/* Addresses in the FPGA register map this module writes and reads back: the
   three colour channels, the global brightness, and the enable bit that
   latches them into the physical LED. */
typedef enum
{
    REG_LED_R = 0x10,
    REG_LED_G = 0x11,
    REG_LED_B = 0x12,
    REG_LED_GLOBAL = 0x13,
    REG_LED_ENABLE = 0x14,
} bsp_led_register_t;

void BSP_LedSet(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t brightness)
{
    BSP_FpgaWriteRegister(REG_LED_R, red);
    BSP_FpgaWriteRegister(REG_LED_G, green);
    BSP_FpgaWriteRegister(REG_LED_B, blue);
    BSP_FpgaWriteRegister(REG_LED_GLOBAL, brightness);
    BSP_FpgaWriteRegister(REG_LED_ENABLE, 1);
}

void BSP_LedOff(void)
{
    BSP_FpgaWriteRegister(REG_LED_ENABLE, 0);
}

bsp_led_state_t BSP_LedGet(void)
{
    const bsp_led_state_t state =
    {
        .red = BSP_FpgaReadRegister(REG_LED_R),
        .green = BSP_FpgaReadRegister(REG_LED_G),
        .blue = BSP_FpgaReadRegister(REG_LED_B),
        .brightness = BSP_FpgaReadRegister(REG_LED_GLOBAL),
        .enabled = (BSP_FpgaReadRegister(REG_LED_ENABLE) & 1u) != 0,
    };
    return state;
}
